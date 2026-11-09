# 🚂 DCC_CAN_RMT-Booster — Chemins de Fer du Chablais (CFC)

Module **Convertisseur DCC → CAN** à **capture matérielle RMT**.
L'ESP32 relit les durées du signal DCC enregistrées par le périphérique
**RMT** (réception, résolution 1 µs, filtre anti-glitch intégré) et les
retransmet sur le bus CAN (TWAI natif, TX GPIO 5 / RX GPIO 4, 500 kbit/s).
Le signal DCC provient de la sortie **EXT1/EXT2 (0–3.3 V)** de la centrale
[LaBox / CommandStation-EX](https://github.com/Locoduino/CommandStation-EX-LaBox),
via un XOR SN74LVC1G86 sur GPIO 27.

Basé sur les bibliothèques :
- [prune74/CanUniversal](https://github.com/prune74/CanUniversal) (bus CAN)
- [prune74/Ws2812Universal](https://github.com/prune74/Ws2812Universal) (LED WS2812, architecture CFC)

Version « ISR » (GPIO + interruption) : [prune74/DCC_CAN-Booster](https://github.com/prune74/DCC_CAN-Booster).

## Pourquoi le RMT ?

| | Version ISR (`DCC_CAN-Booster`) | Version RMT (ce projet) |
| --- | --- | --- |
| Mesure des demi-bits | CPU (`micros()` dans une ISR GPIO) | **Matérielle** (périphérique RMT, tick 1 µs) |
| Gigue de mesure | 2–3 µs (latence ISR, WiFi/BT…) | **Nulle** (horloge matérielle) |
| Filtre anti-glitch | Logiciel (dt < 25 µs ignoré) | **Matériel** (rejeté avant le CPU) |
| Charge CPU | ISR à chaque front (~30 000/s) | Une tâche qui relit des blocs déjà mesurés |
| Cutout | Détecté par seuil sur le trou | Idem, **avec la durée réelle mesurée** |

Pour un signal DCC « très précis », le RMT est la voie royale sur ESP32 :
le CPU ne participe plus à la mesure, donc plus aucune source de gigue
logicielle ne peut fausser les demi-bits.

## Architecture

| Élément | Rôle |
| --- | --- |
| RMT RX (canal 7, matériel) | Enregistre la durée de chaque demi-période du signal DCC (tick 1 µs, filtre 25 µs) |
| `rmtTask` (core 0) | Vide le ringbuffer RMT, classifie les demi-bits, produit des bits complets + cutouts |
| `taskCan` (core 1) | Empaquette les bits (16 max/trame) et les envoie sur le bus CAN |
| `taskCanRx` (core 1) | Sniffer CAN (commande `can on`) |
| Failsafe | Perte DCC > 500 ms → `BOOSTER_OFF` (télémétrie) + bloc vidé |
| Télémétrie | Toutes les 1 s sur l'ID 0x102 |
| **Ws2812Universal (RMT TX, canal 0)** | Pilotage non bloquant des 3 LED d'état via la bibliothèque CFC |

```
LaBox (CommandStation-EX)                ESP32                        Bus CAN
┌─────────────────────┐   EXT1/EXT2    ┌───────────────────────────┐
│  Sortie booster     │  0–3.3 V  ──►  │ XOR SN74LVC1G86 → GPIO27  │
│  DCC + cutout       │                │         │                 │
└─────────────────────┘                │   RMT RX (matériel, 1µs)  │
                                       │ rmtTask → queue → taskCan │ ──► 500 kbit/s
                                       │ TWAI : TX=5, RX=4         │      vers boosters aval
                                       │ Ws2812Universal: TX canal0│      (3 LED d'état → GPIO 26)
                                       └───────────────────────────┘
```

## Câblage

| Signal | Origine | ESP32 | Remarques |
| --- | --- | --- | --- |
| DCC logique | LaBox EXT1/EXT2 (0–3.3 V) via XOR SN74LVC1G86 | GPIO 27 | **Masse commune** LaBox ↔ ESP32 obligatoire |
| CAN TX | TWAI natif | GPIO 5 | Vers transceiver CAN (TJA1050/SN65HVD230…) |
| CAN RX | TWAI natif | GPIO 4 | |
| LED d'état WS2812 ×3 | — | GPIO 26 | Strip 3 LED : LED1 = DCC, LED2 = CAN, LED3 = boosters aval |
| Injection arrêt d'urgence | DAC natif | GPIO 25 | Vers entrée intensité LaBox (GPIO 35 = EXT1, GPIO 39 = EXT2) via résistance 4,7–10 kΩ |

Le filtre anti-glitch est ici **matériel** (dans le RMT) : un RC n'est plus
indispensable, mais reste recommandé en environnement électrique bruyant
(proximité de moteurs, boosters de puissance).

## Principe du décodage (NMRA S-9.1)

Le RMT fournit la durée `dt` de chaque demi-période (mesure matérielle) :

| `dt` mesuré | Interprétation |
| --- | --- |
| `< 25 µs` | Rejeté par le **filtre matériel** RMT (jamais vu par le CPU) |
| `≤ 85 µs` | Demi-bit « 1 » (nominal 58 µs) |
| `85–300 µs` | Demi-bit « 0 » (nominal 100 µs, étiré accepté) |
| `≥ 300 µs`, demi-bit précédent = « 1 » | **Cutout** (durée réelle mesurée, transportée dans `dt_us`) |
| `≥ 300 µs`, demi-bit précédent = « 0 » | Zéro-stretching (traction DC) : demi-bit « 0 » |

Un bit DCC = **deux demi-bits identiques** : un seul événement est émis par
bit complet. Le cutout prolonge le second demi-bit du dernier « 1 » :
la paire est clôturée, `CUTOUT_START` puis `CUTOUT_END` sont émis
consécutivement, et la re-synchronisation se fait sur la demi-période
suivante — **aucun demi-bit n'est jamais perdu**.

**RailCom** : ce module ne distribue pas le RailCom — il relaie uniquement la
présence du cutout (ID 0x101) pour que le booster aval le régénère
(nominal 454 µs) ou l'ignore. L'ID 0x103 est réservé à un usage futur.

## Protocole CAN (IDs 11 bits) — identique à DCC_CAN-Booster

| ID | Contenu |
| --- | --- |
| `0x100` | Bloc de bits DCC : `data[0]` = N bits (1..16), puis 4 bits/octet, chaque nibble = `(bit << 1) \| phase` — DLC = `1 + ceil(N/2)` |
| `0x101` | Cutout **réel** : `{ local, global }` — jamais émis en failsafe |
| `0x102` | Télémétrie : `{ ID, TYPE, mA/10, mV/100, état }` (DLC 5) — ID = numéro du module dans son type (0 = convertisseur **ID_C**, 1..N = boosters **ID_B_xx**) ; TYPE : 0=C, 1=B, 2=S, 3=D, 5=H (hub de distribution), 4/6..255 libres ; état : 0=OK, 1=FAULT, 2=OVERHEAT, 3=OFF, 4=RETRY (réservé). DLC 4 accepté (anciens boosters → type B implicite) |
| `0x103` | RailCom : adresse little-endian (réservé, non distribué) |

### Nommage des modules — ID_X_nn (adressage complet)

Chaque module du réseau est identifié par **deux octets** dans la trame
0x102 : son **numéro** et son **type**. Le nom complet est reconstruit par
le convertisseur : le type devient la **lettre**, le numéro le suffixe :

| TYPE | Lettre | Nom | Cartes |
| --- | --- | --- | --- |
| 0 | `C` | `ID_C` | Convertisseur DCC → CAN (unique, numéro 0) |
| 1 | `B` | `ID_B_01`…`ID_B_nn` | Boosters de puissance |
| 2 | `S` | `ID_S_nn` | Aiguillages / accessoires (réservé, futures cartes) |
| 3 | `D` | `ID_D_nn` | Détection d'occupation (réservé, futures cartes) |
| 5 | `H` | `ID_H_01`…`ID_H_nn` | Hubs de distribution (projet DCC_HUB_DISTRIB-Booster) |
| 4, 6–255 | `?` | — | Extensions futures |

Chaque type dispose de **254 numéros** : le réseau peut accueillir 254
boosters ET 254 aiguillages ET 254 détecteurs, etc., sans qu'aucun ID ne
se recoupe. Le surcoût est d'**un octet par trame de télémétrie** (~0,2 %
de charge bus supplémentaire). Les anciens boosters émettant en DLC 4
(sans octet TYPE) restent compris — interprétés type `B`.

Côté firmware des cartes : un simple couple (type, numéro) à configurer.
Côté convertisseur : `BoosterWatch_name()` reconstruit `ID_X_nn`, visible
via la commande série `boosters` :

```
> boosters
ID_B_01  OK       vu il y a 12 ms
ID_B_02  FAULT    vu il y a 8 ms  [DEFAUT CONFIRME]
ID_B_03  OFF      vu il y a 240 ms
ID_H_01  OK       vu il y a 45 ms
```

Les modules non-booster (hubs `ID_H_xx`, futures cartes) sont suivis pour
l'affichage uniquement : ils ne participent ni à la surveillance
d'injection, ni au pire état aval (LED 3). Table dimensionnée par
`DCCB_MAX_OTHERS` dans `Config.h` (au-delà : trames ignorées).

La sortie JSON (`boosters json`) utilise le tableau `"modules"` avec un
champ `"type"` explicite par entrée :

```
{"modules":[{"type":"B","id":3,"name":"ID_B_03","st":"OK","fault":0,
"ok":0,"last_ms":123},{"type":"H","id":1,"name":"ID_H_01","st":"OK",
"last_ms":56}],"injecting":false}
```

Le bloc part : à 16 bits (trame pleine), sur cutout, ou après 500 µs de
silence DCC. Les boosters aval voient **exactement le même protocole** que
la version ISR : les deux modules sont interchangeables sur le bus.

## LED d'état WS2812 ×3 (GPIO 26)

Trois LED WS2812 (strip, data sur GPIO 26) — un rôle chacune, donc **aucune
priorité exclusive** : tout est visible simultanément :

| LED | Couleur | État |
| --- | --- | --- |
| **LED 1 — DCC** (ce que la carte reçoit) | Bleu | Initialisation (boot) |
| | **Vert battement de cœur** (toutes les 1,5 s) | Signal DCC présent |
| | **Rouge clignotant lent** (0,5 Hz) | Failsafe : DCC perdu > 500 ms |
| | **Jaune** | Erreurs de timing (`bad > 0` — signal dégradé) |
| **LED 2 — CAN** (ce que la carte émet) | Bleu faible | Initialisation |
| | **Vert fixe discret** | Bus CAN valide |
| | **Orange clignotant rapide** (5 Hz) | Bus CAN invalide (transceiver) |
| | **Cyan discret** | Sniffer CAN actif (`can on` — le vert revient à `can off`) |
| **LED 3 — boosters aval** (télémétrie 0x102) | Blanc faible | Aucune télémétrie reçue (ou stale > 3 s) |
| | **Vert fixe discret** | Boosters aval OK |
| | **Rouge clignotant rapide** (5 Hz) | Défaut confirmé → injection auto active |
| | **Magenta clignotant lent** (1 Hz) | Injection **forcée manuelle** (`inject on`) — impossible à confondre avec le rouge auto, ni à oublier : tant qu'elle clignote, le mode test est actif |
| | **Orange fixe** | Surchauffe signalée par un booster |
| | **Éteint** | Boosters à l'arrêt (BOOSTER_OFF) |

Le pilotage se fait par la bibliothèque
**[prune74/Ws2812Universal](https://github.com/prune74/Ws2812Universal)**
(canal **RMT TX n° 0**, via le provider `DccbWs2812Config.h`) : le signal
800 kHz est généré par le matériel, le CPU ne fait que mettre à jour le
framebuffer via `Ws2812Bus` — `setPixel` + `show()` **non bloquant**
(< 0,2 % CPU). La gradation (`DCCB_WS2812_BRIGHTNESS`) est appliquée par
la bibliothèque elle-même. Le canal 7 reste dédié à la capture DCC, et le
CAN (TWAI) est un périphérique totalement indépendant : **aucun impact sur
le décodage ni sur le bus**. Câblage : data → GPIO 26, alimentation 5 V ou
3,3 V (masse commune), condensateur 100 nF près du strip recommandé.

## Arrêt d'urgence : injection vers LaBox (repli global du segment)

Les boosters aval publient leur état sur l'ID `0x102` :
`{ ID, TYPE, mA/10, mV/100, état }`. **Chaque booster a un couple unique
(TYPE=B, ID 1..32)**, configuré dans son firmware, nommé `ID_B_xx`
(voir « Nommage des modules ») — le convertisseur le suit
individuellement : les trames OK des boosters sains ne perturbent pas le
compteur de confirmation d'un booster défaillant. L'état :
0=OK, 1=FAULT, 2=OVERHEAT, 3=OFF (4=RETRY, réservé pour les essais
locaux — ignoré). Le convertisseur émet sa propre télémétrie avec l'ID 0.

1. **Niveau 1 (local, normal)** : chaque booster coupe sa propre zone
   en cas de défaut — le trafic continue ailleurs.
2. **Niveau 2 (constat)** : la LED 3 reflète l'état aval.
3. **Niveau 3 (repli global)** : après **3 trames FAULT/OVERHEAT
   consécutives**, le convertisseur injecte via le **DAC (GPIO 25)** une
   tension ~3,04 V sur l'entrée intensité de la sortie EXT de LaBox
   (seuil LaBox : 2500 mA ÷ 0,80 = 2,52 V). LaBox détecte une
   "surintensité" et **coupe le DCC du segment entier**.
4. L'injection est **maintenue** tant que le défaut persiste (sinon
   LaBox réessaie de rétablir le DCC avec backoff 40 ms → 10 s), et
   **relâchée** après 3 trames OK consécutives : LaBox rétablit le DCC
   automatiquement.

Câblage : GPIO 25 → résistance série **4,7–10 kΩ** → broche intensité
de LaBox (GPIO 35 pour EXT1, GPIO 39 pour EXT2). Ne jamais dépasser
3,3 V. La lecture d'intensité réelle de cette sortie EXT devient
inutilisable (elle voit l'injection) — sans importance si cette sortie
n'alimente que le convertisseur.

Seuils réglables dans `Config.h` : `DCCB_INJECT_DAC_VALUE`,
`DCCB_INJECT_CONFIRM_FRAMES`, `DCCB_INJECT_RELEASE_FRAMES`.

## Failsafe

- Perte du signal DCC > 500 ms → bloc vidé, télémétrie `BOOSTER_OFF` (0x101
  reste réservé au cutout réel).
- Retour du DCC → télémétrie `BOOSTER_OK`.
- `bad > 0` en régime établi → état `BOOSTER_FAULT`.

## 📚 Documentation compagnon (hors convertisseur)

Tout ce qui ne concerne pas ce convertisseur — cahier des charges de la
future carte booster aval (dont l'état RETRY 4), carte de gestion de
canton (« pont JSON »), configuration par le bus (0x104/0x105 +
persistance NVS), chiffres de dimensionnement et feuille de route du
réseau — est regroupé dans le **document compagnon CFC** :

> `CFC-Objectifs-Reseau.pdf` (généré depuis « CFC — Objectifs futurs &
> spécifications du bus »).

Pour l'architecture à hubs (LaBox ↔ ID_H_01/02 ↔ convertisseurs,
injections TOR, timings coordonnés), voir **`LIAISON_CARTE.md`** à la
racine du dépôt — le document d'interface multi-cartes du projet
DCC_HUB_DISTRIB-Booster.

Ce README ne documente donc que le convertisseur lui-même.

## Compilation (VSCode + PlatformIO)

```
pio run
pio run -t upload
pio device monitor
```

`platformio.ini` épingle `espressif32@6.9.0` (arduino-esp32 2.0.x) pour
garantir le driver RMT legacy (`driver/rmt.h`). **Dépendances** :

```ini
lib_deps =
    prune74/CanUniversal
    prune74/Ws2812Universal  # Pilotage des LED WS2812 (architecture CFC)
```

## Commandes série (115200 bauds)

```
stats            statistiques DCC + événements perdus + état CAN
stats json       idem au format JSON (une ligne, pour outil PC)
boosters         modules vus sur le bus (ID_B_xx + ID_H_xx..., état, dernière trame)
boosters json    idem au format JSON (tableau "modules"), pour supervision PC
inject on|off    test manuel de l'injection DAC → LaBox (voltmètre sur GPIO 25)
reset            redémarrage ESP32
can on|off       sniffer CAN
can filter 0x100 filtre le sniffer sur un ID (can filter off pour retirer)
```

**Diagnostic sain** : `dropped=0` et `bad` stable (quelques unités au
démarrage le temps de la première synchronisation, puis plus rien).

## Limites connues

- Le convertisseur ne mesure ni courant ni tension : sa télémétrie 0x102
  envoie `mA=0, mV=0` — seul son ÉTAT (OK/OFF) est significatif.
- Le canal RMT en réception est dédié au DCC (canal 7) : pas de
  réutilisation possible pour d'autres protocoles infrarouge/LED.
- Le cutout est transporté borné (START+END consécutifs avec la durée
  mesurée) : le booster aval régénère un cutout nominal — aucun contenu
  RailCom n'est transporté.

## Auteur

Bruno Rivera (prune74) — Projet Chemins de Fer du Chablais (CFC)