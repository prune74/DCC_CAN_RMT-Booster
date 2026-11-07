#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// LED d'état WS2812 ×3 — trois rôles, une information chacun
// ---------------------------------------------------------------------------
// LED 1 (DCC)     : ce que la carte REÇOIT
// LED 2 (CAN)     : ce que la carte ÉMET
// LED 3 (AVAL)    : état des boosters aval (télémétrie 0x102)
// ---------------------------------------------------------------------------
enum LedDccState : uint8_t {
    DCCLED_BOOT,      // bleu            : initialisation
    DCCLED_OK,        // vert battement  : signal DCC présent
    DCCLED_NO_DCC,    // rouge clignot.  : failsafe, signal DCC perdu
    DCCLED_WARNING    // jaune           : erreurs de timing (bad > 0)
};

enum LedCanState : uint8_t {
    CANLED_INIT,      // bleu faible     : initialisation
    CANLED_OK,        // vert fixe      : bus CAN valide
    CANLED_FAULT,     // orange clignot. : bus CAN invalide (transceiver)
    CANLED_MONITOR    // cyan fixe       : sniffer CAN actif ("can on")
};

enum LedBoostState : uint8_t {
    BLED_INIT,        // blanc faible    : aucune télémétrie reçue
    BLED_OK,          // vert fixe       : boosters aval OK
    BLED_FAULT,       // rouge clignot.  : défaut confirmé → injection auto
    BLED_OVERHEAT,    // orange fixe     : surchauffe signalée par un booster
    BLED_OFF,         // éteint          : boosters à l'arrêt (BOOSTER_OFF)
    BLED_INJECT_TEST  // magenta clignot.: injection FORCÉE manuelle ("inject on")
};

void StatusLed_begin();                    // configure le canal RMT TX (canal 0)
void StatusLed_setDcc(LedDccState st);    // état LED 1 (DCC)
void StatusLed_setCan(LedCanState st);    // état LED 2 (CAN)
void StatusLed_setBoost(LedBoostState st); // état LED 3 (boosters aval)
void StatusLed_task();                    // à appeler dans loop() — auto-limitée à 20 Hz