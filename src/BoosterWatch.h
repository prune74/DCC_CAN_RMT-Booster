#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Surveillance des boosters aval (télémétrie 0x102) + arrêt d'urgence LaBox
// ---------------------------------------------------------------------------
// Trame 0x102 : { ID, TYPE, mA/10, mV/100, état } (DLC 5) — chaque module
// est identifié par SON TYPE et son numéro DANS ce type (adressage complet
// pour les futures cartes du réseau) :
//   type 0 = C convertisseur, 1 = B booster, 2 = S aiguillage (réservé),
//   3 = D détection (réservé), 5 = H hub de distribution
//   (DCC_HUB_DISTRIB-Booster), 4/6..255 = extensions futures
//   état : 0 = OK, 1 = FAULT, 2 = OVERHEAT, 3 = OFF, (4 = RETRY, réservé :
//   essais locaux en cours, ignoré par le convertisseur)
// Anciennes trames DLC 4 (sans TYPE) : interprétées type BOOSTER.
//
// Le convertisseur ne SURVEILLE que les modules de type BOOSTER :
// chacun est suivi individuellement (table en RAM), l'injection ne se
// déclenche que si UN booster donné atteint
// DCCB_INJECT_CONFIRM_FRAMES trames FAULT/OVERHEAT consécutives — les trames
// OK des autres boosters ne remettent PAS son compteur à zéro.
//
// Après confirmation, le convertisseur INJECTE via le DAC (GPIO 25) une
// tension ~3,04 V lue comme NIVEAU HAUT (TOR) sur le GPIO d'entrée direct
// du hub du district (DCC_HUB_DISTRIB-Booster) : le hub COUPE LE DISTRICT
// (DS_COUPE) — repli global, la protection locale reste assurée par chaque
// booster. (Câblage autonome hors hub : broche intensité LaBox GPIO 35/39,
// voir README — le firmware est identique dans les deux cas.)
//
// L'injection est MAINTENUE tant que le défaut persiste, et RELÂCHÉE quand
// TOUS les boosters défaillants ont envoyé DCCB_INJECT_RELEASE_FRAMES trames
// OK consécutives : le hub rétablit le district automatiquement.
// ---------------------------------------------------------------------------

void BoosterWatch_begin();                          // DAC à 0 V au démarrage
void BoosterWatch_onTelemetry(uint8_t id, uint8_t type, uint8_t st); // trame 0x102
void BoosterWatch_loop();                           // à appeler dans loop()
void BoosterWatch_onFailsafe(bool active);          // failsafe DCC : relâcher l'injection
bool BoosterWatch_injecting();                      // injection active ?
bool BoosterWatch_forced();                         // mode test "inject on" ?
void BoosterWatch_forceInjection(bool on);          // TEST : forcer on/off
                                                    // (prioritaire à l'auto)
uint8_t BoosterWatch_worstState();                  // pire état aval : 0..3, 0xFF = inconnu

// Nom complet reconstruit depuis (type, numéro) : (B,3) → "ID_B_03",
// (C,0) → "ID_C", (D,7) → "ID_D_07". Renvoie la longueur (10 max).
uint8_t BoosterWatch_name(uint8_t type, uint8_t id, char *name);

// État lisible d'un booster (pour l'affichage) : "OK", "FAULT",
// "OVERHEAT", "OFF", "RETRY", "INCONNU" — renvoie la longueur écrite.
uint8_t BoosterWatch_stateName(uint8_t st, char *buf);

// Liste des modules vus sur le bus (affichage CLI) — boosters ID_B_xx
// ET modules non-booster (hubs ID_H_xx, tag [STALE] si silencieux) :
//   ID_B_01  OK       dernier msg il y a 123 ms
void BoosterWatch_dump(Print &out);

// Même liste au format JSON (pour un outil PC branché sur le port série),
// tableau "modules" avec champ "type" explicite par entrée :
//   {"modules":[{"type":"B","id":3,"name":"ID_B_03","st":"OK","fault":0,
//    "ok":0,"last_ms":123},{"type":"H","id":1,"name":"ID_H_01","st":"OK",
//    "last_ms":56}],"injecting":false}
void BoosterWatch_dumpJson(Print &out);