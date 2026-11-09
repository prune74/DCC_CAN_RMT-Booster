#pragma once
#include <Arduino.h>

// Entrée DCC logique (sortie XOR SN74LVC1G86) — capturée par le RMT
static const gpio_num_t PIN_DCC_IN = GPIO_NUM_27;

// CAN natif ESP32
static const gpio_num_t PIN_CAN_TX = GPIO_NUM_5;
static const gpio_num_t PIN_CAN_RX = GPIO_NUM_4;

// LED d'état WS2812 ×3 (pilotée par le RMT TX canal 0 — indépendante du canal 7 RX DCC)
// LED 1 = DCC, LED 2 = CAN, LED 3 = boosters aval (télémétrie 0x102)
static const gpio_num_t PIN_WS2812 = GPIO_NUM_26;

// Sortie DAC : injection d'arrêt d'urgence vers le hub du district
// (DCC_HUB_DISTRIB-Booster) — lue sur un GPIO d'entrée DIRECT du hub
// (une des 8 entrées TOR HUB_INJ_PINS, polling 2 ms, anti-rebond 20 ms),
// le hub coupe alors le district concerné. En utilisation autonome hors
// hub, câblage direct possible vers l'entrée intensité de LaBox
// (GPIO 35 = EXT1, GPIO 39 = EXT2) via résistance 4,7–10 kΩ.
// DAC1 de l'ESP32 — ne pas utiliser sur S2/S3/C3 (pas de DAC).
static const gpio_num_t PIN_DAC_INJECT = GPIO_NUM_25;