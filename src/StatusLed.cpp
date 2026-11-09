#include "StatusLed.h"
#include "Pins.h"
#include "Config.h"
#include "DccbWs2812Config.h"  // Notre configurateur
#include <Ws2812Universal.h>

static LedDccState   s_dccState   = DCCLED_BOOT;
static LedCanState   s_canState   = CANLED_INIT;
static LedBoostState s_boostState = BLED_INIT;

// Pointeur vers le bus (initialisé dans StatusLed_begin)
static Ws2812Bus *strip = nullptr;

static void writeFrame(uint8_t r1, uint8_t g1, uint8_t b1,
                       uint8_t r2, uint8_t g2, uint8_t b2,
                       uint8_t r3, uint8_t g3, uint8_t b3) {
    if (!strip) return; // Sécurité
    strip->setPixel(0, (r1 << 16) | (g1 << 8) | b1);
    strip->setPixel(1, (r2 << 16) | (g2 << 8) | b2);
    strip->setPixel(2, (r3 << 16) | (g3 << 8) | b3);
    strip->show();
}

void StatusLed_begin() {
    static DccbWs2812Config config;
    Ws2812Init::begin(config);
    strip = &Ws2812Bus::bus(0);
    writeFrame(0, 0, 40, 0, 0, 20, 20, 20, 20); // État initial
}

void StatusLed_setDcc(LedDccState st) { s_dccState = st; }
void StatusLed_setCan(LedCanState st) { s_canState = st; }
void StatusLed_setBoost(LedBoostState st) { s_boostState = st; }

void StatusLed_task() {
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < 50) return;
    last = now;

    // LED 1 : DCC
    uint8_t r1 = 0, g1 = 0, b1 = 0;
    switch (s_dccState) {
        case DCCLED_BOOT:      b1 = 40; break;
        case DCCLED_OK:        g1 = ((now % 1500) < 60) ? 255 : 25; break;
        case DCCLED_NO_DCC:    r1 = ((now % 1000) < 500) ? 255 : 0; break;
        case DCCLED_WARNING:   r1 = 255; g1 = 150; break;
    }

    // LED 2 : CAN
    uint8_t r2 = 0, g2 = 0, b2 = 0;
    switch (s_canState) {
        case CANLED_INIT:      b2 = 20; break;
        case CANLED_OK:        g2 = 40; break;
        case CANLED_FAULT:     if ((now % 200) < 100) { r2 = 255; g2 = 60; } break;
        case CANLED_MONITOR:   g2 = 30; b2 = 30; break;
    }

    // LED 3 : Boost
    uint8_t r3 = 0, g3 = 0, b3 = 0;
    switch (s_boostState) {
        case BLED_INIT:        r3 = 20; g3 = 20; b3 = 20; break;
        case BLED_OK:          g3 = 40; break;
        case BLED_FAULT:       r3 = ((now % 200) < 100) ? 255 : 0; break;
        case BLED_OVERHEAT:    r3 = 255; g3 = 80; break;
        case BLED_OFF:         break;
        case BLED_INJECT_TEST: r3 = b3 = ((now % 1000) < 700) ? 255 : 0; break;
    }

    writeFrame(r1, g1, b1, r2, g2, b2, r3, g3, b3);
}