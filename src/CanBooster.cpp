#include "CanBooster.h"
#include "Config.h"

// -----------------------------------------------------------------------------
// Empaquetage des bits DCC — 16 bits max par trame
// -----------------------------------------------------------------------------
//   data[0]    = N (nombre de bits réellement emballés, 1..16)
//   data[1..8] = bits, 4 par octet, chaque nibble = (bit << 1) | phase
//   DLC        = 1 + ceil(N / 2)
//
// Le bloc part quand il est plein, sur cutout, ou après un idle
// (DCCB_BLOCK_FLUSH_TIMEOUT_US) pour ne pas ajouter de latence en fin de trame.
// -----------------------------------------------------------------------------
static uint8_t s_blockN = 0;        // nibbles remplis (1 nibble = 1 bit, 0..16)
static uint8_t s_block[9];          // 1 octet compteur + 8 octets de bits

bool CanBooster_flushDccBits() {
    if (s_blockN == 0) return true;           // rien à envoyer

    s_block[0] = s_blockN;                    // N bits réellement emballés

    // DLC : 1 octet compteur + ceil(nibbles / 2) octets
    uint8_t dlc = 1 + ((s_blockN + 1) / 2);

    CanMsg msg((uint16_t)DCCB_CAN_ID_DCC_BLOCK,
               { s_block[0], s_block[1], s_block[2], s_block[3],
                 s_block[4], s_block[5], s_block[6], s_block[7] });
    msg.dlc = dlc;

    bool ok = CanBus::bus(0).send(msg);

    s_blockN = 0;                             // bloc réinitialisé
    for (uint8_t i = 1; i < 9; i++) s_block[i] = 0;

    return ok;
}

bool CanBooster_packDccBit(uint8_t bit, uint8_t phase) {
    uint8_t nib = ((bit & 1) << 1) | (phase & 1);

    if (s_blockN & 1) {
        s_block[1 + (s_blockN >> 1)] |= (nib << 4);   // nibble haut
    } else {
        s_block[1 + (s_blockN >> 1)] = nib;            // nibble bas
    }
    s_blockN++;

    // 16 nibbles = 8 octets de données : capacité maximale de la trame
    if (s_blockN >= DCCB_BITS_PER_BLOCK) {
        return CanBooster_flushDccBits();              // bloc plein → envoi
    }
    return true;
}

// -----------------------------------------------------------------------------
// Mode brut : une trame par bit (debug uniquement)
// -----------------------------------------------------------------------------
bool CanBooster_sendDccBit(uint8_t bit, uint8_t phase) {
    CanMsg msg((uint16_t)DCCB_CAN_ID_DCC_BLOCK, { bit, phase });
    return CanBus::bus(0).send(msg);
}

// -----------------------------------------------------------------------------
// Envoi du cutout (cutout RÉEL uniquement — jamais en failsafe)
// -----------------------------------------------------------------------------
bool CanBooster_sendCutout(bool local, bool global) {
    // Ne jamais laisser un bloc de bits en attente derrière un cutout
    CanBooster_flushDccBits();

    CanMsg msg((uint16_t)DCCB_CAN_ID_CUTOUT,
               { uint8_t(local), uint8_t(global) });
    return CanBus::bus(0).send(msg);
}

// -----------------------------------------------------------------------------
// Envoi de la télémétrie (DLC 5) : { ID, TYPE, mA/10, mV/100, état }
// Ce module s'annonce comme type CONVERTER (ID_C). Les anciens boosters
// émettant en DLC 4 restent compris (interprétés type BOOSTER).
// -----------------------------------------------------------------------------
bool CanBooster_sendTelemetry(uint16_t mA, uint16_t mV, BoosterState st) {
    CanMsg msg((uint16_t)DCCB_CAN_ID_TELEMETRY,
               { DCCB_MODULE_ID, DCCB_MODULE_TYPE,
                 uint8_t(mA / 10), uint8_t(mV / 100), uint8_t(st) });
    return CanBus::bus(0).send(msg);
}

// -----------------------------------------------------------------------------
// Envoi de l'adresse RailCom (réservé — RailCom non distribué)
// -----------------------------------------------------------------------------
bool CanBooster_sendRailcomAddress(uint16_t addr) {
    CanMsg msg((uint16_t)DCCB_CAN_ID_RAILCOM,
               { uint8_t(addr & 0xFF), uint8_t(addr >> 8) });
    return CanBus::bus(0).send(msg);
}

bool CanBooster_hasPendingBits() { return s_blockN > 0; }