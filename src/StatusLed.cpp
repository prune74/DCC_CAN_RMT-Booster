#include "StatusLed.h"
#include "Pins.h"
#include "Config.h"
#include <driver/rmt.h>

// ---------------------------------------------------------------------------
// 3 LED WS2812 pilotées par le RMT TX (canal DCCB_RMT_TX_CHANNEL).
// La séquence 800 kHz est générée PAR LE MATÉRIEL : le CPU ne fait que
// remplir les entrées RMT au changement d'état (rafraîchi à 20 Hz max).
// Coût CPU < 0,2 % — aucun impact sur le décodage DCC (canal 7 RX) ni le CAN.
// ---------------------------------------------------------------------------

#define DCCB_WS2812_COUNT 3   // LED 1 = DCC, LED 2 = CAN, LED 3 = boosters aval

static LedDccState   s_dccState   = DCCLED_BOOT;
static LedCanState   s_canState   = CANLED_INIT;
static LedBoostState s_boostState = BLED_INIT;

// 3 LED × 24 bits + reset
static rmt_item32_t s_items[DCCB_WS2812_COUNT * 24 + 1];

// Timings WS2812 — tick RMT = 0,1 µs (clk_div 8 sur 80 MHz)
#define WS_T1H 8      // "1" : 0,8 µs haut
#define WS_T1L 4      //       0,4 µs bas
#define WS_T0H 4      // "0" : 0,4 µs haut
#define WS_T0L 8      //       0,8 µs bas
#define WS_RESET 600  // reset : 60 µs bas

// Écrit les items RMT d'une couleur (24 bits, GRB, MSB en premier)
static int buildColor(int idx, uint8_t r, uint8_t g, uint8_t b) {
    // Gradation globale
    r = (uint8_t)((r * DCCB_WS2812_BRIGHTNESS) >> 8);
    g = (uint8_t)((g * DCCB_WS2812_BRIGHTNESS) >> 8);
    b = (uint8_t)((b * DCCB_WS2812_BRIGHTNESS) >> 8);

    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;

    for (int i = 0; i < 24; i++) {
        bool one = (grb >> (23 - i)) & 1;
        s_items[idx].duration0 = one ? WS_T1H : WS_T0H;
        s_items[idx].level0    = 1;
        s_items[idx].duration1 = one ? WS_T1L : WS_T0L;
        s_items[idx].level1    = 0;
        idx++;
    }
    return idx;
}

static void writeFrame(uint8_t r1, uint8_t g1, uint8_t b1,
                       uint8_t r2, uint8_t g2, uint8_t b2,
                       uint8_t r3, uint8_t g3, uint8_t b3) {
    int idx = 0;
    idx = buildColor(idx, r1, g1, b1);   // LED 1 : DCC
    idx = buildColor(idx, r2, g2, b2);   // LED 2 : CAN
    idx = buildColor(idx, r3, g3, b3);   // LED 3 : boosters aval

    s_items[idx].duration0 = WS_RESET;   // > 50 µs bas = latch
    s_items[idx].level0    = 0;
    s_items[idx].duration1 = 0;
    s_items[idx].level1    = 0;

    // 73 entrées > 64 : occupe les blocs mémoire des canaux RMT 0 et 1
    rmt_write_items(DCCB_RMT_TX_CHANNEL, s_items, idx + 1, true);
}

void StatusLed_begin() {
    rmt_config_t tx = RMT_DEFAULT_CONFIG_TX(PIN_WS2812, DCCB_RMT_TX_CHANNEL);
    tx.clk_div = 8;        // tick = 0,1 µs
    tx.mem_block_num = 2;  // 73 entrées > 64 → blocs des canaux 0 et 1
    rmt_config(&tx);
    rmt_driver_install(tx.channel, 0, 0);

    // LED1 bleu, LED2 bleu faible, LED3 blanc faible : initialisation
    writeFrame(0, 0, 40, 0, 0, 20, 20, 20, 20);
}

void StatusLed_setDcc(LedDccState st) { s_dccState = st; }
void StatusLed_setCan(LedCanState st) { s_canState = st; }
void StatusLed_setBoost(LedBoostState st) { s_boostState = st; }

void StatusLed_task() {
    static uint32_t last = 0;
    uint32_t now = millis();

    if (now - last < 50) return;   // 20 Hz maximum
    last = now;

    // -----------------------------------------------------------------
    // LED 1 : DCC (ce que la carte reçoit)
    // -----------------------------------------------------------------
    uint8_t r1 = 0, g1 = 0, b1 = 0;
    switch (s_dccState) {
        case DCCLED_BOOT:
            b1 = 40;
            break;
        case DCCLED_OK:
            // Battement de cœur vert : bref pulse brillant toutes les 1,5 s
            if ((now % 1500) < 60) { g1 = 255; } else { g1 = 25; }
            break;
        case DCCLED_NO_DCC:
            // Rouge clignotant lent (0,5 Hz)
            r1 = ((now % 1000) < 500) ? 255 : 0;
            break;
        case DCCLED_WARNING:
            // Jaune : erreurs de timing DCC
            r1 = 255; g1 = 150;
            break;
    }

    // -----------------------------------------------------------------
    // LED 2 : CAN (ce que la carte émet)
    // -----------------------------------------------------------------
    uint8_t r2 = 0, g2 = 0, b2 = 0;
    switch (s_canState) {
        case CANLED_INIT:
            b2 = 20;
            break;
        case CANLED_OK:
            g2 = 40;   // vert discret et fixe
            break;
        case CANLED_FAULT:
            // Orange clignotant rapide (5 Hz)
            if ((now % 200) < 100) { r2 = 255; g2 = 60; }
            break;
        case CANLED_MONITOR:
            // Cyan discret : sniffer CAN actif ("can on") — se voit sans
            // éblouir ; le vert réapparaît dès "can off"
            g2 = 30; b2 = 30;
            break;
    }

    // -----------------------------------------------------------------
    // LED 3 : boosters aval (télémétrie 0x102)
    // -----------------------------------------------------------------
    uint8_t r3 = 0, g3 = 0, b3 = 0;
    switch (s_boostState) {
        case BLED_INIT:
            r3 = 20; g3 = 20; b3 = 20;   // blanc faible : pas encore de retour
            break;
        case BLED_OK:
            g3 = 40;   // vert discret et fixe
            break;
        case BLED_FAULT:
            // Rouge clignotant rapide (5 Hz) : injection d'arrêt active
            r3 = ((now % 200) < 100) ? 255 : 0;
            break;
        case BLED_OVERHEAT:
            r3 = 255; g3 = 80;   // orange fixe
            break;
        case BLED_OFF:
            // Éteint : boosters à l'arrêt (BOOSTER_OFF)
            break;
        case BLED_INJECT_TEST:
            // Magenta clignotant lent (1 Hz) : injection FORCÉE manuelle
            // ("inject on") — nettement distinct du rouge "défaut auto",
            // impossible de confondre les deux ni d'oublier le mode test
            r3 = 255; b3 = 255;
            if ((now % 1000) >= 700) { r3 = 0; b3 = 0; }
            break;
    }

    writeFrame(r1, g1, b1, r2, g2, b2, r3, g3, b3);
}