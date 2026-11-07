#include "BoosterWatch.h"
#include "Pins.h"
#include "Config.h"
#include "CanBooster.h"   // enum BoosterState : OK=0, FAULT=1, OVERHEAT=2, OFF=3

// ---------------------------------------------------------------------------
// Suivi individuel de chaque booster aval (table en RAM) :
//   - un booster atteint CONFIRM trames FAULT consécutives → injection ON
//   - injection relâchée quand plus AUCUN booster n'est en défaut confirmé
//     (chaque booster défaillant doit envoyer RELEASE trames OK consécutives)
//   - un booster silencieux depuis STALE_MS passe en état "inconnu" (0xFF)
//     SANS relâcher l'injection (en cas de doute, on reste prudent)
//   - état 4 (RETRY, essais locaux) : ignoré
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t  faultCount;   // trames FAULT/OVERHEAT consécutives
    uint8_t  okCount;      // trames OK consécutives (relâchement)
    uint8_t  state;        // dernier état reçu (0..3, 0xFF = inconnu)
    uint32_t lastMs;       // dernière trame reçue de CE booster
    bool     used;         // slot actif
} BoosterInfo;

static BoosterInfo s_boosters[DCCB_MAX_BOOSTERS];
static bool         s_injecting = false;
static bool         s_forced    = false;   // inject on/off manuel (test)

static void setInjection(bool on) {
    if (on == s_injecting) return;
    s_injecting = on;
    // DAC GPIO 25 : 235/255 × 3,3 V ≈ 3,04 V > seuil LaBox (~2,52 V)
    dacWrite((uint8_t)PIN_DAC_INJECT, on ? DCCB_INJECT_DAC_VALUE : 0);
}

void BoosterWatch_begin() {
    memset(s_boosters, 0, sizeof(s_boosters));
    for (uint8_t i = 0; i < DCCB_MAX_BOOSTERS; i++) s_boosters[i].state = 0xFF;
    dacWrite((uint8_t)PIN_DAC_INJECT, 0);   // injection OFF au démarrage
}

void BoosterWatch_onTelemetry(uint8_t id, uint8_t type, uint8_t st) {
    // Seuls les BOOSTERS sont surveillés (autres types : futures cartes,
    // gérées ailleurs). ID hors table (0 ou > max) : trame ignorée.
    if (type != DCCB_TYPE_BOOSTER) return;
    if (id == 0 || id > DCCB_MAX_BOOSTERS) return;
    BoosterInfo &b = s_boosters[id - 1];

    b.used   = true;
    b.lastMs = millis();
    b.state  = st;

    switch (st) {
        case BOOSTER_FAULT:
        case BOOSTER_OVERHEAT:
            b.okCount = 0;
            if (b.faultCount < 255) b.faultCount++;
            break;

        case BOOSTER_OK:
            b.faultCount = 0;
            if (s_injecting) {
                if (b.okCount < 255) b.okCount++;
            } else {
                b.okCount = 0;
            }
            break;

        case BOOSTER_OFF:
            // Arrêt volontaire : ni défaut, ni preuve de guérison
            break;

        default:
            // 4 = RETRY (essais locaux) ou valeur inconnue : ignoré
            b.state = 0xFF;
            break;
    }

    // Décision d'injection : un booster quelconque a-t-il confirmé son défaut ?
    bool needInject = false;
    bool allCleared = true;
    for (uint8_t i = 0; i < DCCB_MAX_BOOSTERS; i++) {
        if (!s_boosters[i].used) continue;
        if (s_boosters[i].faultCount >= DCCB_INJECT_CONFIRM_FRAMES) {
            needInject = true;
        }
        // Un booster défaillant relâche seulement après RELEASE trames OK
        if (s_boosters[i].state == BOOSTER_FAULT ||
            s_boosters[i].state == BOOSTER_OVERHEAT ||
            s_boosters[i].okCount < DCCB_INJECT_RELEASE_FRAMES) {
            allCleared = false;
        }
    }

    if (needInject) setInjection(true);
    else if (s_injecting && !s_forced && allCleared) setInjection(false);
}

// ---------------------------------------------------------------------------
// Injection MANUELLE (commande CLI "inject on|off") : test du câblage
// DAC → entrée intensité LaBox SANS attendre un vrai défaut aval.
// Tant que s_forced est posé, la logique automatique ne peut pas relâcher
// l'injection — on teste dans des conditions maîtrisées.
// ---------------------------------------------------------------------------
void BoosterWatch_forceInjection(bool on) {
    s_forced = on;
    setInjection(on);
    Serial.printf("[TEST] Injection %s (%s) — DAC GPIO25 %s\n",
                  on ? "FORCEE ON" : "relâchée",
                  s_forced ? "mode manuel" : "retour mode auto",
                  on ? "≈ 3,04 V" : "0 V");
}

void BoosterWatch_loop() {
    // Boosters silencieux : état "inconnu" (sans relâcher l'injection)
    uint32_t now = millis();
    for (uint8_t i = 0; i < DCCB_MAX_BOOSTERS; i++) {
        if (s_boosters[i].used && (now - s_boosters[i].lastMs > DCCB_TELEMETRY_STALE_MS)) {
            s_boosters[i].state = 0xFF;
        }
    }
}

bool BoosterWatch_injecting() { return s_injecting; }
bool BoosterWatch_forced()    { return s_forced; }

// ---------------------------------------------------------------------------
// Nommage ID_X_nn : le module envoie son numéro et son type (deux octets de
// la trame 0x102) ; le convertisseur reconstruit le nom complet pour
// l'affichage. Le type devient la LETTRE, le numéro le suffixe :
//   (C, 0)  → "ID_C"       (le convertisseur lui-même)
//   (B, 3)  → "ID_B_03"
//   (B, 12) → "ID_B_12"
//   (S, 1)  → "ID_S_01"    (future carte aiguillage)
//   (D, 7)  → "ID_D_07"    (future carte détection)
// Types inconnus → lettre '?'. Renvoie la longueur de la chaîne.
// ---------------------------------------------------------------------------
uint8_t BoosterWatch_name(uint8_t type, uint8_t id, char *name) {
    char letter;
    switch (type) {
        case DCCB_TYPE_CONVERTER: letter = 'C'; break;
        case DCCB_TYPE_BOOSTER:    letter = 'B'; break;
        case DCCB_TYPE_SWITCH:     letter = 'S'; break;
        case DCCB_TYPE_DETECTOR:    letter = 'D'; break;
        default:                   letter = '?'; break;
    }

    // Le convertisseur est unique : pas de numéro
    if (type == DCCB_TYPE_CONVERTER) {
        strcpy(name, "ID_C");
        return 4;
    }

    // ID_X_nn : numéro en décimal, zéro initial si < 10
    name[0] = 'I'; name[1] = 'D'; name[2] = '_';
    name[3] = letter; name[4] = '_';
    uint8_t len;
    if (id >= 100) {
        name[5] = '0' + (id / 100) % 10;
        name[6] = '0' + (id / 10) % 10;
        name[7] = '0' + id % 10;
        name[8] = 0;
        len = 9;
    } else {
        name[5] = '0' + (id / 10) % 10;
        name[6] = '0' + id % 10;
        name[7] = 0;
        len = 8;
    }
    return len;
}

uint8_t BoosterWatch_stateName(uint8_t st, char *buf) {
    const char *s;
    switch (st) {
        case BOOSTER_OK:       s = "OK";       break;
        case BOOSTER_FAULT:    s = "FAULT";    break;
        case BOOSTER_OVERHEAT: s = "OVERHEAT"; break;
        case BOOSTER_OFF:      s = "OFF";      break;
        case 4:                s = "RETRY";    break;
        default:               s = "INCONNU"; break;
    }
    strcpy(buf, s);
    return strlen(s);
}

void BoosterWatch_dump(Print &out) {
    bool any = false;
    char name[10], st[9];
    for (uint8_t i = 0; i < DCCB_MAX_BOOSTERS; i++) {
        if (!s_boosters[i].used) continue;
        any = true;
        BoosterWatch_name(DCCB_TYPE_BOOSTER, i + 1, name);
        BoosterWatch_stateName(s_boosters[i].state, st);
        out.printf("%s  %-8s  vu il y a %lu ms%s\n", name, st,
                   (unsigned long)(millis() - s_boosters[i].lastMs),
                   s_boosters[i].faultCount >= DCCB_INJECT_CONFIRM_FRAMES
                       ? "  [DEFAUT CONFIRME]" : "");
    }
    if (!any) out.println("Aucun booster aval vu sur le bus (0x102).");
}

// ---------------------------------------------------------------------------
// Variante JSON — destinée aux outils PC (journalisation, supervision) :
//   {"boosters":[{"id":3,"name":"ID_B_03","st":"OK","fault":0,"ok":0,
//    "last_ms":123}],"injecting":false}
// Une ligne compacte et stable : facile à parser, sans fioritures.
// ---------------------------------------------------------------------------
void BoosterWatch_dumpJson(Print &out) {
    char name[10], st[9];
    uint32_t now = millis();
    bool first = true;

    out.print("{\"boosters\":[");
    for (uint8_t i = 0; i < DCCB_MAX_BOOSTERS; i++) {
        if (!s_boosters[i].used) continue;
        if (!first) out.print(",");
        first = false;

        BoosterWatch_name(DCCB_TYPE_BOOSTER, i + 1, name);
        BoosterWatch_stateName(s_boosters[i].state, st);

        out.printf("{\"id\":%u,\"name\":\"%s\",\"st\":\"%s\","
                   "\"fault\":%u,\"ok\":%u,\"last_ms\":%lu}",
                   i + 1, name, st,
                   s_boosters[i].faultCount, s_boosters[i].okCount,
                   (unsigned long)(now - s_boosters[i].lastMs));
    }
    out.printf("],\"injecting\":%s}\n", s_injecting ? "true" : "false");
}

// Pire état aval, pour la LED 3 : FAULT > OVERHEAT > OFF > OK > inconnu
uint8_t BoosterWatch_worstState() {
    bool anyUsed = false;
    uint8_t worst = 0xFF;
    for (uint8_t i = 0; i < DCCB_MAX_BOOSTERS; i++) {
        if (!s_boosters[i].used) continue;
        anyUsed = true;
        uint8_t st = s_boosters[i].state;
        if (st == BOOSTER_FAULT)  return BOOSTER_FAULT;   // rien de pire
        if (st == BOOSTER_OVERHEAT) worst = BOOSTER_OVERHEAT;
        else if (st == BOOSTER_OFF && worst != BOOSTER_OVERHEAT) worst = BOOSTER_OFF;
        else if (st == BOOSTER_OK && worst == 0xFF)      worst = BOOSTER_OK;
    }
    return anyUsed ? worst : 0xFF;
}