#pragma once
#include <Ws2812Universal.h>
#include "Pins.h"        // Pour PIN_WS2812
#include "Config.h"      // Pour DCCB_WS2812_BRIGHTNESS
#include <driver/gpio.h> // Pour gpio_num_t
#include <driver/rmt.h>  // Pour RMT_CHANNEL_0

class DccbWs2812Config : public Ws2812ConfigProvider {
public:
    uint8_t busCount() const override { return 1; }

    const Ws2812BusConfig &busConfig(uint8_t index) const override {
        static const Ws2812BusConfig cfg = {
            true,                  // enabled
            PIN_WS2812,            // broche
            3,                     // nombre de LED
            RMT_CHANNEL_0,         // canal RMT (différent de RMT_CHANNEL_7 pour le DCC)
            DCCB_WS2812_BRIGHTNESS // luminosité
        };
        return cfg;
    }
};