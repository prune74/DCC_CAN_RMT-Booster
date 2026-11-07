#include "DccRmtDecoder.h"
#include "Pins.h"
#include "Config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/ringbuf.h>
#include <driver/rmt.h>

// ---------------------------------------------------------------------------
// Décodeur DCC par capture matérielle RMT
// ---------------------------------------------------------------------------
// Le périphérique RMT (mode réception, tick = 1 µs) enregistre en matériel la
// durée de chaque niveau du signal sur PIN_DCC_IN, avec son filtre
// anti-glitch intégré (pulses < DCCB_RMT_FILTER_US rejetés AVANT le CPU).
// Le CPU ne mesure RIEN : il relit des durées horodatées par le matériel.
// Aucune ISR GPIO, aucune gigue logicielle.
//
// La tâche rmtTask (core 0) vide le ringbuffer du driver RMT et applique la
// classification NMRA (identique à la version ISR de DCC_CAN-Booster) :
//   - demi-bit "1" : dt <= 85 µs
//   - demi-bit "0" : 85 < dt < 300 µs (étiré accepté)
//   - dt >= 300 µs : cutout si le demi-bit précédent était un "1",
//                   sinon zéro-stretching (demi-bit "0" étiré)
//   - UN SEUL événement DCC_EVT_BIT par bit COMPLET (paire de demi-bits)
//   - Cutout : clôture de la paire en cours, START+END consécutifs,
//     re-synchronisation sur la demi-période suivante (rien n'est perdu)
// ---------------------------------------------------------------------------

// État de classification
static bool    pairOpen       = false; // 1er demi-bit reçu, 2e attendu
static uint8_t pairBit        = 0;     // classe du 1er demi-bit (0/1)
static bool    lastHalfWasOne = false; // garde anti faux-cutout
static uint8_t bitPhase       = 0;     // parité de debug (par bit émis)

static QueueHandle_t  dccQueue   = nullptr;
static RingbufHandle_t rmtRingbuf = nullptr;

// Stats
#if DCCB_MEASURE_STATS
static volatile uint32_t s_bit0Count   = 0;
static volatile uint32_t s_bit1Count   = 0;
static volatile uint32_t s_cutoutCount = 0;
static volatile uint32_t s_badTiming   = 0;
#endif

// Événements perdus : queue pleine → événement jeté
static volatile uint32_t s_droppedEvents = 0;

static void sendEvent(const DccEvent &ev) {
    if (xQueueSend(dccQueue, (void*)&ev, 0) != pdTRUE) {
        s_droppedEvents++;
    }
}

// Émission d'un bit complet (paire fermée)
static void emitBit(uint8_t bit, uint32_t durUs) {
    DccEvent ev;
    ev.type  = DCC_EVT_BIT;
    ev.bit   = bit;
    ev.phase = bitPhase;
    ev.dt_us = (uint16_t)durUs;
    bitPhase ^= 1;

#if DCCB_MEASURE_STATS
    if (bit) s_bit1Count++; else s_bit0Count++;
#endif
    sendEvent(ev);
}

// Classification d'un demi-bit et fermeture de paire le cas échéant
static void processHalf(uint8_t half, uint32_t durUs) {
    lastHalfWasOne = (half == 1);

    if (!pairOpen) {
        pairOpen = true;              // mémoriser la classe du 1er demi-bit
        pairBit  = half;
        return;
    }

    // Second demi-bit : identique au premier par construction DCC.
    if (half != pairBit) {
#if DCCB_MEASURE_STATS
        s_badTiming++;                // désynchronisation (bruit)
#endif
    }
    pairOpen = false;
    emitBit(pairBit, durUs);
}

// Traitement d'une demi-périée mesurée par le RMT
static void processHalfDuration(uint32_t durUs) {

    // Garde logique (le filtre matériel RMT a déjà rejeté la plupart)
    if (durUs < DCCB_TIMING_GLITCH_US) {
#if DCCB_MEASURE_STATS
        s_badTiming++;
#endif
        return;
    }

    // Gros trou : cutout (après un demi-bit "1") ou zéro-stretching
    if (durUs >= DCCB_TIMING_CUTOUT_START_US) {

        if (lastHalfWasOne) {
            // Cutout NMRA : prolonge le second demi-bit du dernier "1".
            if (pairOpen) {
                pairOpen = false;
                emitBit(pairBit, durUs);
            }

            // La durée RÉELLE du cutout est mesurée par le matériel —
            // transportée dans dt_us (le booster aval régénère un cutout
            // nominal 454 µs, la valeur sert au diagnostic).
            DccEvent ev;
            ev.type  = DCC_EVT_CUTOUT_START;
            ev.bit   = 0;
            ev.phase = bitPhase;
            ev.dt_us = (uint16_t)durUs;
            sendEvent(ev);

            ev.type = DCC_EVT_CUTOUT_END;
            sendEvent(ev);

#if DCCB_MEASURE_STATS
            s_cutoutCount++;
#endif
            lastHalfWasOne = false;
            return;   // la demi-période suivante ouvre le bit suivant
        }

        // Zéro-stretching (traction DC) : demi-bit "0" étiré
        processHalf(0, durUs);
        return;
    }

    // Demi-bit normal : classification par point médian
    processHalf(durUs <= DCCB_TIMING_MIDPOINT_US ? 1 : 0, durUs);
}

// ---------------------------------------------------------------------------
// Tâche de vidage du ringbuffer RMT (core 0 — le core 1 reste dédié au CAN)
// ---------------------------------------------------------------------------
static void rmtTask(void *pvParameters) {
    for (;;) {
        size_t rx_size = 0;
        rmt_item32_t *items =
            (rmt_item32_t *) xRingbufferReceive(rmtRingbuf, &rx_size, pdMS_TO_TICKS(100));

        if (items) {
            size_t n = rx_size / sizeof(rmt_item32_t);
            for (size_t i = 0; i < n; i++) {
                // Chaque entrée RMT contient deux impulsions (durées 0 et 1)
                if (items[i].duration0) processHalfDuration(items[i].duration0);
                if (items[i].duration1) processHalfDuration(items[i].duration1);
            }
            vRingbufferReturnItem(rmtRingbuf, items);
        }
    }
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------
void DccDecoder_begin() {
    dccQueue = xQueueCreate(DCC_EVENT_QUEUE_SIZE, sizeof(DccEvent));

    pinMode(PIN_DCC_IN, INPUT);

    // Configuration RMT en réception : tick = 1 µs, filtre anti-glitch,
    // fin de réception après 30 ms de silence (ou bloc mémoire plein).
    rmt_config_t rxconfig = RMT_DEFAULT_CONFIG_RX(PIN_DCC_IN, DCCB_RMT_CHANNEL);
    rxconfig.clk_div = DCCB_RMT_CLK_DIV;
    rxconfig.mem_block_num = DCCB_RMT_MEM_BLOCKS;
    rxconfig.rx_config.filter_en = true;
    rxconfig.rx_config.filter_ticks_thresh = DCCB_RMT_FILTER_US;
    rxconfig.rx_config.idle_threshold = DCCB_RMT_IDLE_US;

    rmt_config(&rxconfig);
    rmt_driver_install(rxconfig.channel, 1024, 0);
    rmt_get_ringbuf_handle(rxconfig.channel, &rmtRingbuf);

    // La capture est matérielle : la tâche ne fait que relire des durées
    // déjà mesurées. Core 0 pour laisser le core 1 entièrement au CAN.
    xTaskCreatePinnedToCore(rmtTask, "RMT_DCC", 4096, nullptr, 4, nullptr, 0);
}

bool DccDecoder_getEvent(DccEvent &ev) {
    if (dccQueue == nullptr) return false;
    return xQueueReceive(dccQueue, &ev, 0) == pdTRUE;
}

uint32_t DccDecoder_getDroppedEvents() {
    return s_droppedEvents;
}

void DccDecoder_getStats(uint32_t &bit0Count, uint32_t &bit1Count,
                         uint32_t &cutoutCount, uint32_t &badTiming) {
#if DCCB_MEASURE_STATS
    bit0Count   = s_bit0Count;
    bit1Count   = s_bit1Count;
    cutoutCount = s_cutoutCount;
    badTiming   = s_badTiming;
#else
    bit0Count = bit1Count = cutoutCount = badTiming = 0;
#endif
}