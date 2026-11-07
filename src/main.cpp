#include <Arduino.h>
#include "Pins.h"
#include "Config.h"
#include "BoosterCanConfig.h"
#include "DccRmtDecoder.h"
#include "CanBooster.h"
#include "Cli.h"
#include "StatusLed.h"
#include "BoosterWatch.h"
#include <CanUniversal.h>

TaskHandle_t taskCanHandle    = nullptr;
TaskHandle_t taskCanRxHandle  = nullptr;

volatile bool canMonitorEnabled = false;
volatile int32_t canMonitorFilter = -1;

BoosterCanConfig boosterCanConfig;

// ---------------------------------------------------------------------------
// Tâche CAN (core 1) : événements DCC → bus CAN + failsafe + télémétrie
// ---------------------------------------------------------------------------
// Le décodage est MATÉRIEL (RMT) : la tâche rmtTask (core 0) produit des
// événements (1 = 1 bit complet, ou 1 cutout START+END) dans une queue ;
// cette tâche les empaquette et les envoie sur le bus CAN.
// Le core 1 est entièrement dédié au CAN ; le WiFi/BT n'est jamais
// initialisé (aucune perturbation RMT/CAN possible).
// ---------------------------------------------------------------------------
void taskCan(void *pvParameters) {
    DccEvent ev;
    uint32_t lastDccMs   = millis();
    uint32_t lastStatsMs = millis();
    uint32_t lastBitUs   = micros();

    bool failsafeActive = false;

    for (;;) {

        // -------------------------------------------------------------------
        // 1) Événements DCC disponibles
        // -------------------------------------------------------------------
        bool hadEvent = false;

        while (DccDecoder_getEvent(ev)) {
            hadEvent = true;
            lastDccMs = millis();
            lastBitUs = micros();

            if (failsafeActive) {
                failsafeActive = false;
                CanBooster_sendTelemetry(0, 0, BOOSTER_OK);
                StatusLed_setDcc(DCCLED_OK);
            }

            switch (ev.type) {
                case DCC_EVT_BIT:
#if DCCB_SEND_RAW_BITS
                    CanBooster_sendDccBit(ev.bit, ev.phase);
#else
                    CanBooster_packDccBit(ev.bit, ev.phase);
#endif
                    break;

                case DCC_EVT_CUTOUT_START:
                    CanBooster_sendCutout(true, true);
                    break;

                case DCC_EVT_CUTOUT_END:
                    CanBooster_sendCutout(false, false);
                    break;
            }
        }

        if (hadEvent) {
            taskYIELD();   // ne pas affamer taskCanRx (prio inférieure)
        }

        // -------------------------------------------------------------------
        // 2) Anti-latence : bloc partiel envoyé si le DCC se tait un instant
        // -------------------------------------------------------------------
#if !DCCB_SEND_RAW_BITS
        if (CanBooster_hasPendingBits()) {
            uint32_t nowUs = micros();
            if (nowUs - lastBitUs > DCCB_BLOCK_FLUSH_TIMEOUT_US) {
                CanBooster_flushDccBits();
            }
        }
#endif

        // -------------------------------------------------------------------
        // 3) Failsafe : perte du signal DCC
        // -------------------------------------------------------------------
        // Pas de trame cutout ici — l'ID 0x101 est réservé au cutout réel.
        // L'état "rails coupés" est porté par la télémétrie (BOOSTER_OFF).
        // -------------------------------------------------------------------
        uint32_t now = millis();

        if (!failsafeActive && (now - lastDccMs > DCCB_FAILSAFE_TIMEOUT_MS)) {
            failsafeActive = true;
            CanBooster_flushDccBits();                    // ne rien perdre
            CanBooster_sendTelemetry(0, 0, BOOSTER_OFF);
            StatusLed_setDcc(DCCLED_NO_DCC);
        }

        // -------------------------------------------------------------------
        // 4) Télémétrie périodique
        // -------------------------------------------------------------------
        // Le convertisseur ne mesure RIEN (ni courant, ni tension) : il
        // envoie mA=0 et mV=0, honnêtement. Seul l'ÉTAT est significatif :
        // une carte de gestion du réseau saura ainsi si le DCC est présent
        // (OK) ou coupé (OFF, failsafe).
        // -------------------------------------------------------------------
#if DCCB_MEASURE_STATS
        if (now - lastStatsMs > DCCB_TELEMETRY_PERIOD_MS) {
            uint32_t b0, b1, co, bad;
            DccDecoder_getStats(b0, b1, co, bad);

            BoosterState st = (bad > 0) ? BOOSTER_FAULT : BOOSTER_OK;

            CanBooster_sendTelemetry(0, 0, st);   // mA/mV : non mesurés

            // LED 1 (DCC) : jaune si erreurs de timing, vert sinon
            StatusLed_setDcc(st == BOOSTER_FAULT ? DCCLED_WARNING : DCCLED_OK);

            lastStatsMs = now;
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ---------------------------------------------------------------------------
// Tâche CAN RX (sniffer)
// ---------------------------------------------------------------------------
void taskCanRx(void *pvParameters) {
    CanMsg msg;

    for (;;) {
        if (CanBus::bus(0).receive(msg)) {

            // -------------------------------------------------------------
            // Télémétrie des boosters aval (0x102) : { ID, mA/10, mV/100, état }
            // surveillance LED 3 + arrêt d'urgence LaBox (voir BoosterWatch)
            // -------------------------------------------------------------
            if (msg.id == DCCB_CAN_ID_TELEMETRY && msg.dlc >= 4) {
                // DLC 5 : { ID, TYPE, mA/10, mV/100, état }
                // DLC 4 : ancien format sans TYPE → type BOOSTER implicite
                uint8_t type = (msg.dlc >= 5) ? msg.data[1] : DCCB_TYPE_BOOSTER;
                uint8_t st   = (msg.dlc >= 5) ? msg.data[4] : msg.data[3];
                BoosterWatch_onTelemetry(msg.data[0], type, st);
            }

            if (canMonitorEnabled) {
                if (canMonitorFilter == -1 || msg.id == (uint32_t)canMonitorFilter) {

                    Serial.printf("[CAN RX] ID=0x%03X LEN=%u DATA=",
                                  msg.id, msg.dlc);

                    for (uint8_t i = 0; i < msg.dlc; i++)
                        Serial.printf("%02X ", msg.data[i]);

                    Serial.println();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ---------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\nDCC_CAN_RMT-Booster — capture matérielle RMT");

    // LED d'état WS2812 ×3 (RMT TX canal 0 — indépendant du RMT RX DCC canal 7)
    // LED 1 = DCC, LED 2 = CAN, LED 3 = boosters aval
    StatusLed_begin();

    // Arrêt d'urgence : DAC d'injection (GPIO 25) à 0 V au démarrage
    BoosterWatch_begin();

    // Décodeur RMT (canal 7, tick 1 µs, filtre matériel 25 µs)
    DccDecoder_begin();

    // Bibliothèque CanUniversal (registre) via le provider du booster
    CanInit::begin(boosterCanConfig);
    if (!CanBus::bus(0).isValid()) {
        Serial.println("[ERREUR] CAN0 invalide — vérifiez le transceiver.");
        StatusLed_setCan(CANLED_FAULT);
    } else {
        StatusLed_setCan(CANLED_OK);
    }

    Cli_begin();

    xTaskCreatePinnedToCore(taskCan,   "CAN",    4096, nullptr, 3, &taskCanHandle,   1);
    xTaskCreatePinnedToCore(taskCanRx, "CAN_RX", 4096, nullptr, 1, &taskCanRxHandle, 1);
}

// ---------------------------------------------------------------------------
// LOOP (CLI)
// ---------------------------------------------------------------------------
void loop() {
    Cli_task();

    // Surveillance des boosters aval (timeout "inconnu", etc.)
    BoosterWatch_loop();

    // LED 3 : état des boosters aval (télémétrie 0x102)
    if (BoosterWatch_forced()) {
        StatusLed_setBoost(BLED_INJECT_TEST);    // "inject on" actif (test)
    } else if (BoosterWatch_injecting()) {
        StatusLed_setBoost(BLED_FAULT);          // défaut confirmé → injection
    } else {
        switch (BoosterWatch_worstState()) {
            case BOOSTER_OK:       StatusLed_setBoost(BLED_OK);       break;
            case BOOSTER_OVERHEAT: StatusLed_setBoost(BLED_OVERHEAT);  break;
            case BOOSTER_OFF:      StatusLed_setBoost(BLED_OFF);      break;
            default:               StatusLed_setBoost(BLED_INIT);     break; // 0xFF
        }
    }

    // LED 2 : état CAN + modes CLI visibles (sniffer actif → cyan)
    if (canMonitorEnabled) {
        StatusLed_setCan(CANLED_MONITOR);
    } else {
        StatusLed_setCan(CanBus::bus(0).isValid() ? CANLED_OK : CANLED_FAULT);
    }

    StatusLed_task();   // auto-limitée à 20 Hz
    vTaskDelay(pdMS_TO_TICKS(10));
}