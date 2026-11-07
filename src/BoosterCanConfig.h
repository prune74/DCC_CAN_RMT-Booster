#pragma once

/**
 * ============================================================================
 *  🚂 BoosterCanConfig — configuration matérielle CAN du booster (RMT)
 * ============================================================================
 *  Provider pour la bibliothèque prune74/CanUniversal.
 *  Le CAN est natif sur l'ESP32 (TWAI), aucune puce externe.
 *  Broches (voir Pins.h) : TX = GPIO 5, RX = GPIO 4.
 * ============================================================================
 */

#include <CanUniversal.h>

struct BoosterCanConfig : public CanConfigProvider {

    CanBusConfig buses[1] = {
        // enabled, speed, tx, rx, cs, int, sck, mosi, miso, quartz, tolérance, loopback
        CanBusConfig(true, DCCB_CAN_BITRATE,
                     PIN_CAN_TX, PIN_CAN_RX,   // TWAI interne de l'ESP32
                     GPIO_NUM_NC, GPIO_NUM_NC,
                     GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC,
                     0, 0,
                     false)
    };

    uint8_t busCount() const override { return 1; }

    const CanBusConfig& bus(uint8_t index) const override { return buses[0]; }
};