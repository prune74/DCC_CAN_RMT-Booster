#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Décodeur DCC — capture matérielle RMT
// ---------------------------------------------------------------------------
// L'API est volontairement identique à celle du DccDecoder de DCC_CAN-Booster
// (mêmes noms de fonctions) : main.cpp et Cli.cpp restent interchangeables.
// ---------------------------------------------------------------------------

enum DccEventType : uint8_t {
    DCC_EVT_BIT,
    DCC_EVT_CUTOUT_START,
    DCC_EVT_CUTOUT_END
};

struct DccEvent {
    DccEventType type;
    uint8_t bit;      // 0/1 si BIT
    uint8_t phase;    // parité de debug (alterne par bit émis)
    uint16_t dt_us;   // demi-durée mesurée par le RMT (debug/stats)
};

void DccDecoder_begin();
bool DccDecoder_getEvent(DccEvent &ev);

void DccDecoder_getStats(uint32_t &bit0Count, uint32_t &bit1Count,
                         uint32_t &cutoutCount, uint32_t &badTiming);

// Nombre d'événements perdus (queue pleine) depuis le démarrage
uint32_t DccDecoder_getDroppedEvents();