#pragma once

// ============================================================================
//  Profil standardisé "Convertisseur DCC → CAN" — VERSION RMT
// ============================================================================
//  Capture matérielle du signal DCC par le périphérique RMT (réception).
//  Seuils de classification alignés NMRA S-9.1, protocole CAN identique
//  à la version ISR (DCC_CAN-Booster).
// ============================================================================

// ---------------------------------------------------------------------------
// CAN IDs (identiques à DCC_CAN-Booster)
// ---------------------------------------------------------------------------
#define DCCB_CAN_ID_DCC_BLOCK 0x100 // BLOC de bits DCC (jusqu'à 16 bits)
                                    // data[0]  = nombre de bits N (1..16)
                                    // data[1..]= bits, 4 par octet,
                                    //            chaque nibble = (bit << 1) | phase
#define DCCB_CAN_ID_CUTOUT 0x101    // cutout { local, global } — cutout RÉEL uniquement
#define DCCB_CAN_ID_TELEMETRY 0x102 // { ID, TYPE, mA/10, mV/100, état } — DLC 5
                                    // ID (1..254)   : numéro du module DANS son type
                                    // TYPE          : type de carte (voir module
                                    //                 types ci-dessous)
                                    // Compatibilité : DLC 4 accepté (anciens
                                    // boosters sans octet TYPE → type BOOSTER)
#define DCCB_CAN_ID_RAILCOM 0x103   // réservé (RailCom non distribué)

// ---------------------------------------------------------------------------
// Mode d'envoi des bits DCC
// ---------------------------------------------------------------------------
#define DCCB_SEND_RAW_BITS 0        // 0 = empaquetage (défaut), 1 = brut (debug)

// Nombre maximum de bits DCC par trame CAN : 1 bit = 1 nibble,
// 16 bits = 8 octets de données (DLC max). Ne pas dépasser.
#define DCCB_BITS_PER_BLOCK 16

// Anti-latence : si aucun bit n'arrive pendant ce délai (µs), le bloc
// partiel est envoyé tel quel (fin de trame DCC, etc.)
#define DCCB_BLOCK_FLUSH_TIMEOUT_US 500

// ---------------------------------------------------------------------------
// Capture RMT (matérielle)
// ---------------------------------------------------------------------------
#define DCCB_RMT_CHANNEL RMT_CHANNEL_7 // canal RMT en réception (le DÉRNIER :
                                       // avec mem_block_num=1, aucun bloc mémoire
                                       // au-delà du canal 7 n'est nécessaire)
#define DCCB_RMT_CLK_DIV 80            // 80 MHz / 80 = 1 tick = 1 µs (résolution)
#define DCCB_RMT_FILTER_US 25          // filtre matériel : pulses < 25 µs rejetés
#define DCCB_RMT_IDLE_US 30000         // fin de réception après 30 ms de silence
#define DCCB_RMT_MEM_BLOCKS 1          // 1 bloc = 64 entrées = 128 demi-périodes
                                       // (~64 bits par tranche — largement suffisant)

// ---------------------------------------------------------------------------
// Seuils de classification (durées mesurées PAR LE MATÉRIEL, demi-bits)
// ---------------------------------------------------------------------------
//   dt <= DCCB_TIMING_MIDPOINT_US     → demi-bit "1" (nominal 58 µs)
//   dt <  DCCB_TIMING_CUTOUT_START_US → demi-bit "0" (nominal 100 µs, étiré OK)
//   dt >= DCCB_TIMING_CUTOUT_START_US :
//     - demi-bit précédent = "1" → CUTOUT (prolonge le dernier bit "1")
//     - sinon               → demi-bit "0" étiré (zéro-stretching, DC)
// Le filtre matériel RMT élimine déjà dt < 25 µs ; la garde logique reste
// pour les durées aberrantes restantes.
#define DCCB_TIMING_GLITCH_US 25
#define DCCB_TIMING_MIDPOINT_US 85
#define DCCB_TIMING_CUTOUT_START_US 300

// ---------------------------------------------------------------------------
// CAN Booster
// ---------------------------------------------------------------------------
#define DCCB_CAN_BITRATE 500000 // 500 kbps standard

// ---------------------------------------------------------------------------
// Failsafe
// ---------------------------------------------------------------------------
#define DCCB_FAILSAFE_TIMEOUT_MS 500   // perte DCC → BOOSTER_OFF
#define DCCB_FAILSAFE_COOLDOWN_MS 1000 // réarmement

// ---------------------------------------------------------------------------
// Télémétrie
// ---------------------------------------------------------------------------
#define DCCB_TELEMETRY_PERIOD_MS 1000

// ---------------------------------------------------------------------------
// Options debug
// ---------------------------------------------------------------------------
// DCCB_MEASURE_STATS active les compteurs b0/b1/cutout/bad + la télémétrie.
// (DCCB_DEBUG_SERIAL et DCCB_SCOPE_MODE ont été supprimés : aucun code ne
//  les lisait, tout comme les commandes CLI "debug"/"scope" associées.)
#define DCCB_MEASURE_STATS 1

// ---------------------------------------------------------------------------
// Queue d'événements DCC (1 événement = 1 bit complet ou 1 cutout)
// ---------------------------------------------------------------------------
#define DCC_EVENT_QUEUE_SIZE 64

// ---------------------------------------------------------------------------
// LED d'état WS2812
// ---------------------------------------------------------------------------
// Pilotée par la bibliothèque prune74/Ws2812Universal (canal RMT TX
// DCCB_RMT_TX_CHANNEL via DccbWs2812Config.h : génération matérielle du
// signal 800 kHz, émission non bloquante, gradation gérée par la
// bibliothèque — coût CPU quasi nul, aucune ISR, aucun impact sur le CAN).
// Le canal 7 reste dédié à la capture DCC en réception.
#define DCCB_RMT_TX_CHANNEL RMT_CHANNEL_0
#define DCCB_WS2812_BRIGHTNESS 64 // gradation globale, 0..255

// ---------------------------------------------------------------------------
// Arrêt d'urgence par injection (repli global vers LaBox)
// ---------------------------------------------------------------------------
// Après DCCB_INJECT_CONFIRM_FRAMES trames FAULT/OVERHEAT consécutives reçues
// en télémétrie (0x102) de boosters aval, le convertisseur injecte via le DAC
// (GPIO 25) une tension > seuil de LaBox sur l'entrée intensité de la sortie
// EXT : LaBox détecte une "surintensité" et coupe le DCC du segment.
// L'injection est maintenue tant que le défaut persiste, relâchée après
// DCCB_INJECT_RELEASE_FRAMES trames OK consécutives (LaBox rétablit seule).
#define DCCB_INJECT_DAC_VALUE 235      // ~3,04 V (seuil LaBox : ~2,52 V)
#define DCCB_INJECT_CONFIRM_FRAMES 3   // trames FAULT consécutives → injection
#define DCCB_INJECT_RELEASE_FRAMES 3  // trames OK consécutives → relâchement
#define DCCB_TELEMETRY_STALE_MS 3000   // au-delà : état boosters "inconnu"

// ID de CE module convertisseur dans les trames 0x102. Les boosters aval
// utilisent des ID 1..DCCB_MAX_BOOSTERS ; le convertisseur émet avec l'ID 0
// (il n'est pas suivi par sa propre surveillance).
//
// ---------------------------------------------------------------------------
// TYPES DE MODULES (adressage complet pour les futures cartes du réseau)
// ---------------------------------------------------------------------------
// Depuis la DLC 5, chaque trame 0x102 porte { ID, TYPE, ... } : le numéro est
// libre DANS chaque type (1..254 par type), et le type ouvre le bus aux
// futures cartes (aiguillages, détection, etc.). Le nom complet ID_X_nn est
// reconstruit par le convertisseur à partir des DEUX octets :
//   type 0 (C) → ID_C            : convertisseur DCC → CAN (numéro 0)
//   type 1 (B) → ID_B_01..ID_B_nn : boosters de puissance
//   type 2 (S) → ID_S_nn          : aiguillages / accessoires (réservé)
//   type 3 (D) → ID_D_nn          : détection d'occupation (réservé)
//   type 5 (H) → ID_H_nn          : hubs de distribution
//                                 (EXT1/EXT2, projet DCC_HUB_DISTRIB-Booster)
//   types 4, 6..255 : libres pour extensions futures
// Une ancienne trame DLC 4 (sans octet TYPE) est interprétée type BOOSTER.
// ---------------------------------------------------------------------------
#define DCCB_TYPE_CONVERTER 0 // C — ce module
#define DCCB_TYPE_BOOSTER    1 // B — boosters de puissance (surveillés)
#define DCCB_TYPE_SWITCH     2 // S — aiguillages / accessoires (réservé)
#define DCCB_TYPE_DETECTOR   3 // D — détection d'occupation (réservé)
#define DCCB_TYPE_HUB        5 // H — hubs de distribution (EXT1/EXT2,
                               //     projet DCC_HUB_DISTRIB-Booster)

#define DCCB_MODULE_ID   0 // numéro du convertisseur (dans son type)
#define DCCB_MODULE_TYPE DCCB_TYPE_CONVERTER
#define DCCB_MODULE_ID 0

// Nombre maximum de boosters aval suivis individuellement (table en RAM).
// Un booster dont l'ID dépasse cette valeur est ignoré.
// L'ID étant un octet, le plafond théorique est 254 boosters ; chaque slot
// coûte ~12 octets de RAM — ajuster librement selon le réseau.
#define DCCB_MAX_BOOSTERS 32

// Nombre maximum de modules NON-booster suivis pour l'affichage (hubs
// ID_H_xx, futures cartes ID_S_nn/ID_D_nn...). Ils ne participent ni à
// la surveillance d'injection, ni au pire état aval ; au-delà de cette
// valeur, leurs trames sont ignorées silencieusement.
#define DCCB_MAX_OTHERS 8