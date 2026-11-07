#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Surveillance des boosters aval (télémétrie 0x102) + arrêt d'urgence LaBox
// ---------------------------------------------------------------------------
// Trame 0x102 : { ID, TYPE, mA/10, mV/100, état } (DLC 5) — chaque module
// est identifié par SON TYPE et son numéro DANS ce type (adressage complet
// pour les futures cartes du réseau) :
//   type 0 = C convertisseur, 1 = B booster, 2 = S aiguillage (réservé),
//   3 = D détection (réservé), 4..255 = extensions futures
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
// tension supérieure au seuil de LaBox sur l'entrée intensité de la sortie
// EXT (GPIO 35 = EXT1, GPIO 39 = EXT2 côté LaBox) : LaBox détecte une
// "surintensité" et coupe le DCC du segment — arrêt d'urgence global du
// segment CAN (repli, la protection locale reste assurée par chaque booster).
//
// L'injection est MAINTENUE tant que le défaut persiste (LaBox réessaie
// sinon toutes les 40 ms → 10 s), et RELÂCHÉE quand TOUS les boosters
// défaillants ont envoyé DCCB_INJECT_RELEASE_FRAMES trames OK consécutives :
// LaBox rétablit le DCC automatiquement.
// ---------------------------------------------------------------------------

void BoosterWatch_begin();                          // DAC à 0 V au démarrage
void BoosterWatch_onTelemetry(uint8_t id, uint8_t type, uint8_t st); // trame 0x102
void BoosterWatch_loop();                           // à appeler dans loop()
bool BoosterWatch_injecting();                      // injection active ?
bool BoosterWatch_forced();                        // mode test "inject on" ?
void BoosterWatch_forceInjection(bool on);         // TEST : forcer on/off
                                                   // (prioritaire à l'auto)
uint8_t BoosterWatch_worstState();                  // pire état aval : 0..3, 0xFF = inconnu

// Nom complet reconstruit depuis (type, numéro) : (B,3) → "ID_B_03",
// (C,0) → "ID_C", (D,7) → "ID_D_07". Renvoie la longueur (10 max).
uint8_t BoosterWatch_name(uint8_t type, uint8_t id, char *name);

// État lisible d'un booster (pour l'affichage) : "OK", "FAULT",
// "OVERHEAT", "OFF", "RETRY", "INCONNU" — renvoie la longueur écrite.
uint8_t BoosterWatch_stateName(uint8_t st, char *buf);

// Liste des boosters vus sur le bus (affichage CLI) :
//   ID_B_01  OK       dernier msg il y a 123 ms
void BoosterWatch_dump(Print &out);

// Même liste au format JSON (pour un outil PC branché sur le port série) :
//   {"boosters":[{"id":3,"name":"ID_B_03","st":"OK","fault":0,"ok":0,
//    "last_ms":123}],"injecting":false}
void BoosterWatch_dumpJson(Print &out);