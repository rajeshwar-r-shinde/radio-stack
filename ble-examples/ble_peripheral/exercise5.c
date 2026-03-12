/*
 * exercise5.c — nRF Connect Observation Checklist
 *
 * No BLE code here. Prints a detailed guide of what to look for
 * in nRF Connect while running exercises 2, 3, and 4.
 *
 * Also decodes example raw advertising bytes so you understand
 * the BLE packet structure.
 *
 * Build:  make exercise5
 * Run:    ./exercise5   (no sudo needed)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ── Decode one AD structure ─────────────────────────────────────── */
typedef struct {
    uint8_t  length;
    uint8_t  type;
    uint8_t  data[31];
    int      data_len;
} ADStruct;

static const char *ad_type_name(uint8_t type)
{
    switch (type) {
        case 0x01: return "Flags";
        case 0x02: return "Incomplete 16-bit UUIDs";
        case 0x03: return "Complete 16-bit UUIDs";
        case 0x06: return "Incomplete 128-bit UUIDs";
        case 0x07: return "Complete 128-bit UUIDs";
        case 0x08: return "Shortened Local Name";
        case 0x09: return "Complete Local Name";
        case 0x0A: return "TX Power Level";
        case 0xFF: return "Manufacturer Specific Data";
        default:   return "Unknown";
    }
}

static void decode_flags(uint8_t flags)
{
    printf("         Bit 0: LE Limited Discoverable  = %s\n", (flags & 0x01) ? "YES" : "no");
    printf("         Bit 1: LE General Discoverable  = %s\n", (flags & 0x02) ? "YES" : "no");
    printf("         Bit 2: BR/EDR Not Supported     = %s\n", (flags & 0x04) ? "YES (BLE only)" : "no");
}

static void decode_ad_packet(const uint8_t *payload, int len)
{
    printf("\n  ┌─ Raw advertising payload (%d bytes) ─────────────────────┐\n", len);
    printf("  │  ");
    for (int i = 0; i < len; i++) printf("%02X ", payload[i]);
    printf("\n  └────────────────────────────────────────────────────────────┘\n\n");

    int i = 0;
    while (i < len) {
        uint8_t ad_len  = payload[i];
        if (ad_len == 0 || i + ad_len >= len) break;
        uint8_t ad_type = payload[i + 1];

        printf("  AD Structure at byte %d:\n", i);
        printf("    Length : 0x%02X (%d bytes of payload)\n", ad_len, ad_len - 1);
        printf("    Type   : 0x%02X  → %s\n", ad_type, ad_type_name(ad_type));
        printf("    Data   :");
        for (int j = 0; j < ad_len - 1; j++)
            printf(" %02X", payload[i + 2 + j]);
        printf("\n");

        /* Decode known types */
        if (ad_type == 0x01 && ad_len >= 2) {
            uint8_t flags = payload[i + 2];
            decode_flags(flags);
        } else if (ad_type == 0x09 || ad_type == 0x08) {
            printf("    ASCII  : \"");
            for (int j = 0; j < ad_len - 1; j++) {
                char c = payload[i + 2 + j];
                printf("%c", (c >= 0x20 && c < 0x7F) ? c : '?');
            }
            printf("\"\n");
        } else if (ad_type == 0x0A && ad_len >= 2) {
            int8_t tx = (int8_t)payload[i + 2];
            printf("    TX Power: %d dBm\n", tx);
        }
        printf("\n");
        i += ad_len + 1;
    }
}

/* ── Example advertising packet from our exercises ──────────────── */
static void show_example_packet(void)
{
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  Example: Decoding the RAW advertising bytes from nRF Connect ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\nThis is what exercise2/3/4 broadcasts (approx):\n");

    /*
     * AD packet:
     *   02 01 06   — Flags: General Discoverable, BLE only
     *   0F 09 4C 61 70 74 6F 70 47 41 54 54 5F 45 78 33
     *           → Length=15, Type=0x09 (Full Name), "LaptopGATT_Ex3"
     *   01 0A 00   — TX Power = 0 dBm
     */
    static const uint8_t example_payload[] = {
        0x02, 0x01, 0x06,                            /* Flags                 */
        0x0F, 0x09,                                  /* Local Name, len=15    */
          'L','a','p','t','o','p','G','A','T','T',
          '_','E','x','3',                           /* "LaptopGATT_Ex3"      */
        0x02, 0x0A, 0x00,                            /* TX Power = 0 dBm      */
    };

    decode_ad_packet(example_payload, (int)sizeof(example_payload));
}

/* ── Full checklist ──────────────────────────────────────────────── */
static void print_checklist(void)
{
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  nRF Connect Observation Checklist                           ║\n");
    printf("║  (Run exercise2/3/4 first, then check each item below)       ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    printf("─── SCAN SCREEN ──────────────────────────────────────────────\n");
    printf("  [ ] Laptop name visible: LaptopGATT_Ex3 / Ex4\n");
    printf("  [ ] RSSI shown (e.g. -55 dBm)\n");
    printf("        Move laptop closer  → RSSI closer to 0 (less negative)\n");
    printf("        Move laptop farther → RSSI more negative\n");
    printf("  [ ] Tap device row → 'RAW' tab\n");
    printf("        [ ] Find AD Type 0x01 (Flags)\n");
    printf("        [ ] Find AD Type 0x09 (Complete Local Name)\n");
    printf("        [ ] Find AD Type 0x07 (128-bit Service UUID)\n");
    printf("              Note: UUID bytes are LITTLE-ENDIAN in the packet!\n");
    printf("              Our UUID: 12345678-1234-5678-1234-56789abcdef0\n");
    printf("              In packet: F0 DE BC 9A 78 56 34 12 34 12 78 56 34 12 78 12\n\n");

    printf("─── CONNECT ──────────────────────────────────────────────────\n");
    printf("  [ ] Tap CONNECT button → status shows 'Connected'\n");
    printf("  [ ] Three services visible:\n");
    printf("        0x1800 = Generic Access (GAP)     — added by BlueZ auto\n");
    printf("        0x1801 = Generic Attribute (GATT) — added by BlueZ auto\n");
    printf("        12345678-... = Our custom service\n");
    printf("  [ ] Expand 'Unknown Service' → UUID matches SVC_UUID\n\n");

    printf("─── READ (Exercise 3) ────────────────────────────────────────\n");
    printf("  [ ] Find characteristic ending in ...abcdef1\n");
    printf("  [ ] Properties shown: READ, WRITE\n");
    printf("  [ ] Tap ↓ READ button\n");
    printf("  [ ] Hex bytes: 48 65 6C 6C 6F 20 6E 52 46 21\n");
    printf("  [ ] Switch display to 'Text (UTF-8)' → 'Hello nRF!'\n\n");

    printf("─── WRITE (Exercise 3) ───────────────────────────────────────\n");
    printf("  [ ] Tap ↑ WRITE button\n");
    printf("  [ ] Select format: Text (UTF-8)\n");
    printf("  [ ] Type: Test  → press SEND\n");
    printf("  [ ] Terminal shows: [WRITE] <- Received from nRF Connect: 'Test'\n");
    printf("  [ ] Try writing hex bytes: 0xDE 0xAD 0xBE 0xEF\n");
    printf("  [ ] Terminal shows them as hex: DE AD BE EF\n\n");

    printf("─── NOTIFY (Exercise 4) ──────────────────────────────────────\n");
    printf("  [ ] Find characteristic ending in ...abcdef2\n");
    printf("  [ ] Properties shown: READ, NOTIFY\n");
    printf("  [ ] Tap 🔔 BELL icon → subscribe\n");
    printf("  [ ] Value updates every ~1 second\n");
    printf("  [ ] Decode the 2 bytes:\n");
    printf("        counter=1:  bytes [01 00] — little-endian uint16\n");
    printf("        counter=256: bytes [00 01] — 0x0100 = 256\n");
    printf("        counter=1000: bytes [E8 03] — 0x03E8 = 1000\n");
    printf("  [ ] Tap 🔔 again → unsubscribe → updates stop\n\n");

    printf("─── ATT PROTOCOL LOG ─────────────────────────────────────────\n");
    printf("  [ ] In nRF Connect: tap ≡ menu → 'Log'\n");
    printf("  [ ] Connect again — watch raw ATT packets:\n");
    printf("        ATT_MTU exchange\n");
    printf("        Service Discovery (GATT Read By Group)\n");
    printf("        Characteristic Discovery\n");
    printf("        Descriptor Discovery (CCCD for notify)\n");
    printf("        Write to CCCD 0x0001 = enable notifications\n\n");

    printf("─── BONUS CHALLENGES ─────────────────────────────────────────\n");
    printf("  [ ] Change notify speed: edit g_timeout_add(1000,...)\n");
    printf("        → 100  = 10 updates/sec  (fast)\n");
    printf("        → 5000 = 1 update/5 sec (slow)\n");
    printf("  [ ] Add Battery Level: UUID 0x2A19, value = uint8 0-100\n");
    printf("  [ ] Add a WRITE_WITHOUT_RESPONSE characteristic\n");
    printf("  [ ] Connect from two phones simultaneously\n\n");
}

/* ── C BLE concepts quick reference ─────────────────────────────── */
static void print_concepts(void)
{
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  BLE Concepts Quick Reference                                ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║                                                              ║\n");
    printf("║  GAP (Generic Access Profile)                                ║\n");
    printf("║    Controls advertising, device name, discovery mode         ║\n");
    printf("║    Peripheral = advertises and accepts connections            ║\n");
    printf("║    Central    = scans and initiates connections (nRF app)     ║\n");
    printf("║                                                              ║\n");
    printf("║  GATT (Generic ATTribute Profile)                            ║\n");
    printf("║    Organises data as: Server → Services → Characteristics    ║\n");
    printf("║    Attribute = addressable unit with UUID + value + perms    ║\n");
    printf("║                                                              ║\n");
    printf("║  Characteristic Flags                                        ║\n");
    printf("║    read                = Central can read current value       ║\n");
    printf("║    write               = Central can write (with response)    ║\n");
    printf("║    write-without-response = write, no ACK (lower latency)    ║\n");
    printf("║    notify              = Peripheral sends unsolicited updates ║\n");
    printf("║    indicate            = like notify but with ACK             ║\n");
    printf("║                                                              ║\n");
    printf("║  CCCD (Client Characteristic Configuration Descriptor)       ║\n");
    printf("║    UUID 0x2902 — Central writes 0x0001 to enable notify       ║\n");
    printf("║    BlueZ handles this automatically                          ║\n");
    printf("║                                                              ║\n");
    printf("║  D-Bus Interfaces (Linux BlueZ)                              ║\n");
    printf("║    LEAdvertisement1    = your adv data object                 ║\n");
    printf("║    LEAdvertisingManager1 = adapter's adv manager             ║\n");
    printf("║    GattManager1        = adapter's GATT manager              ║\n");
    printf("║    GattService1        = a service in your GATT tree         ║\n");
    printf("║    GattCharacteristic1 = a characteristic                    ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

int main(void)
{
    printf("\n=== Exercise 5: nRF Connect Observation Guide ===\n\n");
    show_example_packet();
    print_checklist();
    print_concepts();
    return 0;
}
