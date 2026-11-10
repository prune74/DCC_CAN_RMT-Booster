#include "Cli.h"
#include "Config.h"
#include "DccRmtDecoder.h"
#include "BoosterWatch.h"
#include <CanUniversal.h>
#include <ESP.h>

extern volatile bool canMonitorEnabled;
extern volatile int32_t canMonitorFilter;

static String input;

// ---------------------------------------------------------------------------
// Définition d'un handler de commande
// ---------------------------------------------------------------------------
struct CliCommand {
    const char *name;
    void (*handler)(const String &args);
};

// ---------------------------------------------------------------------------
// Handlers des commandes
// ---------------------------------------------------------------------------
void cmd_stats(const String &args) {
    uint32_t b0, b1, co, bad;
    DccDecoder_getStats(b0, b1, co, bad);
    uint32_t dropped = DccDecoder_getDroppedEvents();

    if (args == "json") {
        // Flux JSON pour outil PC : une ligne, format stable
        Serial.printf("{\"b0\":%lu,\"b1\":%lu,\"cutout\":%lu,\"bad\":%lu,"
                      "\"dropped\":%lu,\"can0\":\"%s\"}\n",
                      b0, b1, co, bad, dropped,
                      CanBus::bus(0).isValid() ? "ok" : "invalid");
        return;
    }

    Serial.printf("STATS: b0=%lu b1=%lu cutout=%lu bad=%lu\n",
                  b0, b1, co, bad);
    Serial.printf("Dropped events: %lu\n",
                  (unsigned long)dropped);
    Serial.printf("CAN0: %s\n", CanBus::bus(0).isValid() ? "valid" : "INVALID");
}

void cmd_reset(const String&) {
    Serial.println("Resetting ESP32...");
    delay(100);
    ESP.restart();
}

// ---------------- CAN MONITOR ----------------

void cmd_can_on(const String&) {
    canMonitorEnabled = true;
    Serial.println("CAN monitor ON");
}

void cmd_can_off(const String&) {
    canMonitorEnabled = false;
    Serial.println("CAN monitor OFF");
}

void cmd_can_filter(const String &args) {
    if (args == "off") {
        canMonitorFilter = -1;
        Serial.println("CAN filter disabled");
    } else {
        int id = (int) strtol(args.c_str(), nullptr, 16);
        canMonitorFilter = id;
        Serial.printf("CAN filter set to 0x%03X\n", id);
    }
}

// ---------------- BOOSTERS AVAL ----------------

void cmd_boosters(const String &args) {
    if (args == "json") {
        BoosterWatch_dumpJson(Serial);   // flux pour outil PC
    } else {
        BoosterWatch_dump(Serial);        // affichage humain
    }
}

// ---------------- INJECTION (TEST) ----------------

void cmd_inject(const String &args) {
    if (args == "on") {
        BoosterWatch_forceInjection(true);
    } else if (args == "off") {
        BoosterWatch_forceInjection(false);
    } else {
        Serial.println("Usage: inject on|off  (test du câblage DAC -> hub)");
        Serial.printf("Injection actuelle: %s\n",
                      BoosterWatch_injecting() ? "ACTIVE" : "inactive");
    }
}

// ---------------- MENU ----------------

void cmd_menu(const String&) {
    Serial.println("Commandes disponibles :");
    Serial.println("  stats [json]       - Affiche les statistiques DCC");
    Serial.println("  boosters [json]    - Affiche l'état des boosters");
    Serial.println("  inject on|off      - Test du câblage DAC -> hub");
    Serial.println("  reset              - Redémarre l'ESP32");
    Serial.println("  can on             - Active le moniteur CAN");
    Serial.println("  can off            - Désactive le moniteur CAN");
    Serial.println("  can filter <hex>   - Filtre les messages CAN (ex: 1A3)");
    Serial.println("  can filter off     - Désactive le filtre CAN");
    Serial.println("  menu               - Affiche ce menu");
}

// ---------------------------------------------------------------------------
// Tableau des commandes
// ---------------------------------------------------------------------------
static const CliCommand commands[] = {
    { "stats",        cmd_stats },
    { "boosters",     cmd_boosters },
    { "inject",       cmd_inject },
    { "reset",        cmd_reset },
    { "can on",       cmd_can_on },
    { "can off",      cmd_can_off },
    { "can filter",   cmd_can_filter },
    { "menu",         cmd_menu },
};

// ---------------------------------------------------------------------------
// Dispatcher : recherche la commande et appelle son handler
// ---------------------------------------------------------------------------
static bool Cli_dispatch(const String &cmd) {
    for (auto &c : commands) {
        if (cmd.startsWith(c.name)) {
            String args = cmd.substring(strlen(c.name));
            args.trim();
            c.handler(args);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------
void Cli_begin() {
    input.reserve(64);
}

// ---------------------------------------------------------------------------
// Boucle CLI
// ---------------------------------------------------------------------------
void Cli_task() {
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\n' || c == '\r') {
            if (input.length() == 0) return;

            String cmd = input;
            input = "";
            cmd.trim();

            if (!Cli_dispatch(cmd)) {
                Serial.printf("Unknown command: %s\n", cmd.c_str());
            }

            return;
        }

        input += c;
    }
}