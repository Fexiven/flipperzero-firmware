/* Toyota tires TPMS. Usually 443.92 Mhz FSK (In Europe).
 *
 * Preamble + sync + 64 bits of data. ~48us short pulse length.
 *
 * The preamble + sync is something like:
 *
 *   10101010101 (preamble) + 001111[1] (sync)
 *
 * Note: the final [1] means that sometimes it is four 1s, sometimes
 * five, depending on the short pulse length detection and the exact
 * duration of the high long pulse. After the sync, a differential
 * Manchester encoded payload follows. However the Flipper's CC1101
 * often can't decode correctly the initial alternating pattern 101010101,
 * so what we do is to seek just the sync, that is "001111" or "0011111",
 * however we now that it must be followed by one differenitally encoded
 * bit, so we can use also the first symbol of data to force a more robust
 * detection, and look for one of the following:
 *
 * [001111]00
 * [0011111]00
 * [001111]01
 * [0011111]01
 */

#include "../../app.h"

static bool decode(uint8_t* bits, uint32_t numbytes, uint32_t numbits, ProtoViewMsgInfo* info) {
    if(numbits - 6 < 64 * 2)
        return false; /* Ask for 64 bit of data (each bit
                                           is two symbols in the bitmap). */

    char* sync[] = {"00111100", "001111100", "00111101", "001111101", NULL};

    int j;
    uint32_t off = 0;
    for(j = 0; sync[j]; j++) {
        off = bitmap_seek_bits(bits, numbytes, 0, numbits, sync[j]);
        if(off != BITMAP_SEEK_NOT_FOUND) {
            off += strlen(sync[j]) - 2;
            break;
        }
    }
    if(off == BITMAP_SEEK_NOT_FOUND) return false;

    FURI_LOG_E(TAG, "Toyota TPMS sync[%s] found", sync[j]);

    uint8_t raw[9];
    uint32_t decoded = convert_from_diff_manchester(raw, sizeof(raw), bits, numbytes, off, true);
    FURI_LOG_E(TAG, "Toyota TPMS decoded bits: %lu", decoded);

    if(decoded < 8 * 9) return false; /* Require the full 8 bytes. */
    if(crc8(raw, 8, 0x80, 7) != raw[8]) return false; /* Require sane CRC. */

    float kpa = (float)((raw[4] & 0x7f) << 1 | raw[5] >> 7) * 0.25 - 7;
    int temp = ((raw[5] & 0x7f) << 1 | raw[6] >> 7) - 40;

    snprintf(info->name, sizeof(info->name), "%s", "Toyota TPMS");
    snprintf(
        info->raw,
        sizeof(info->raw),
        "%02X%02X%02X%02X%02X%02X%02X%02X%02X",
        raw[0],
        raw[1],
        raw[2],
        raw[3],
        raw[4],
        raw[5],
        raw[6],
        raw[7],
        raw[8]);
    snprintf(
        info->info1,
        sizeof(info->info1),
        "Tire ID %02X%02X%02X%02X",
        raw[0],
        raw[1],
        raw[2],
        raw[3]);
    snprintf(info->info1, sizeof(info->info1), "Pressure %.2f psi", (double)kpa);
    snprintf(info->info2, sizeof(info->info2), "Temperature %d C", temp);
    return true;
}

ProtoViewDecoder ToyotaTPMSDecoder = {"Toyota TPMS", decode};
