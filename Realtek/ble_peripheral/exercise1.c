/*
 * exercise1.c — BLE Advertising using hciconfig / bluetoothctl
 *
 * This exercise shows TWO ways to advertise without a GATT stack:
 *   Method A: Print the bluetoothctl commands to run manually
 *   Method B: Use hcitool / hciconfig directly via system() calls
 *
 * Build:  make exercise1
 * Run:    sudo ./exercise1
 *
 * What to observe in nRF Connect:
 *   - Open SCAN tab
 *   - Look for "LaptopBLE_Ex1"
 *   - Tap the device row → view RAW advertising bytes
 *   - Identify AD Type 0x09 (Complete Local Name)
 */

#include "ble_common.h"

GMainLoop *g_main_loop_handle = NULL;   /* not used in ex1, satisfies linker */

/* ── Print the manual bluetoothctl steps ─────────────────────────── */
static void print_bluetoothctl_guide(void)
{
    printf("\n");
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║  Exercise 1A — Manual bluetoothctl Steps             ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  Open a NEW terminal and type:                       ║\n");
    printf("║                                                      ║\n");
    printf("║  $ bluetoothctl                                      ║\n");
    printf("║  [bluetooth]# power on                               ║\n");
    printf("║  [bluetooth]# menu advertise                         ║\n");
    printf("║  [advertise]# uuids 180D                             ║\n");
    printf("║  [advertise]# name \"LaptopBLE_Ex1\"                   ║\n");
    printf("║  [advertise]# back                                   ║\n");
    printf("║  [bluetooth]# advertise on                           ║\n");
    printf("║                                                      ║\n");
    printf("║  Then open nRF Connect → SCAN                        ║\n");
    printf("║  You will see \"LaptopBLE_Ex1\" in the list.           ║\n");
    printf("║                                                      ║\n");
    printf("║  To stop:                                            ║\n");
    printf("║  [bluetooth]# advertise off                          ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n\n");
}

/* ── Method B: start advertising via hciconfig + hcitool ────────── */
static void start_hci_advertising(void)
{
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║  Exercise 1B — Raw HCI Advertising via hcitool       ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n\n");

    printf("[1] Bringing up hci0...\n");
    system("sudo hciconfig hci0 up 2>/dev/null || hciconfig hci0 up");

    printf("[2] Resetting advertising...\n");
    /* Disable advertising first (opcode 0x2008 = LE Set Advertise Enable) */
    system("sudo hcitool -i hci0 cmd 0x08 0x000a 00 2>/dev/null");

    /*
     * [3] Set advertising data manually (HCI command 0x2008 opcode 0x0008)
     *
     * Advertising payload breakdown (31 bytes max):
     *   02 01 06          → Flags: LE General Discoverable, BR/EDR not supported
     *   0E 09 4C61707...  → AD Type 0x09 = Complete Local Name = "LaptopBLE_Ex1"
     *                        0x4C=L 0x61=a 0x70=p 0x74=t 0x6F=o 0x70=p
     *                        0x42=B 0x4C=L 0x45=E 0x5F=_ 0x45=E 0x78=x 0x31=1
     *
     * Byte 0 of hcitool cmd payload = total length of AD structures
     */
    printf("[3] Setting advertising data (name=LaptopBLE_Ex1)...\n");
    system("sudo hcitool -i hci0 cmd 0x08 0x0008 "
           "1a "                   /* total AD length = 26 bytes           */
           "02 01 06 "             /* Flags                                */
           "0e 09 "                /* Length=14, Type=0x09 (Complete Name) */
           "4c 61 70 74 6f 70 "   /* L  a  p  t  o  p                     */
           "42 4c 45 5f "         /* B  L  E  _                            */
           "45 78 31 "            /* E  x  1                               */
           "00 00 00 00 00 "      /* padding to 31 bytes                   */
           "2>/dev/null");

    /*
     * [4] Set advertising parameters:
     *   min_interval = 0x00A0 (100ms), max_interval = 0x00A0
     *   type = 0x00 (ADV_IND = connectable undirected)
     *   own_addr_type = 0x00 (public)
     *   peer_addr_type = 0x00, peer_addr = 000000000000
     *   channel_map = 0x07 (all 3 channels)
     *   filter_policy = 0x00
     */
    printf("[4] Setting advertising parameters (100ms interval)...\n");
    system("sudo hcitool -i hci0 cmd 0x08 0x0006 "
           "A0 00 "               /* min interval 0x00A0 = 160 * 0.625ms = 100ms */
           "A0 00 "               /* max interval                                 */
           "00 "                  /* ADV_IND (connectable undirected)             */
           "00 "                  /* own address type: public                     */
           "00 "                  /* peer address type                            */
           "00 00 00 00 00 00 "  /* peer address (unused for ADV_IND)            */
           "07 "                  /* channel map: 37, 38, 39                      */
           "00 "                  /* filter policy: none                          */
           "2>/dev/null");

    /* [5] Enable advertising */
    printf("[5] Enabling advertising...\n");
    system("sudo hcitool -i hci0 cmd 0x08 0x000a 01 2>/dev/null");

    printf("\n[ OK] Advertising started as 'LaptopBLE_Ex1'\n");
    printf("      Open nRF Connect → SCAN → look for LaptopBLE_Ex1\n");
    printf("      Press Enter to stop...\n\n");

    getchar();

    /* Disable advertising */
    system("sudo hcitool -i hci0 cmd 0x08 0x000a 00 2>/dev/null");
    printf("[OK] Advertising stopped.\n\n");
}

/* ── Observation guide ───────────────────────────────────────────── */
static void print_observation_guide(void)
{
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  What to observe in nRF Connect                              ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  1. Open SCAN tab                                            ║\n");
    printf("║  2. Find 'LaptopBLE_Ex1'                                     ║\n");
    printf("║  3. Tap device row → expand 'RAW' advertising data           ║\n");
    printf("║                                                              ║\n");
    printf("║  Decode the RAW bytes:                                       ║\n");
    printf("║    02 01 06      → Flags (LE Discoverable, no BR/EDR)        ║\n");
    printf("║    0E 09 4C...   → AD Type 0x09 = Complete Local Name        ║\n");
    printf("║                    Hex to ASCII: 4C=L 61=a 70=p 74=t...      ║\n");
    printf("║                                                              ║\n");
    printf("║  RSSI shown as negative dBm (e.g. -55 dBm)                  ║\n");
    printf("║  Move laptop closer → RSSI gets closer to 0                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

int main(void)
{
    if (geteuid() != 0) {
        ERR("Run as root:  sudo ./exercise1");
        return 1;
    }

    printf("\n=== Exercise 1: BLE Advertising (HCI Level) ===\n\n");

    print_bluetoothctl_guide();
    print_observation_guide();

    printf("Run Method B now? (raw HCI advertising) [y/N]: ");
    fflush(stdout);

    char ch = getchar();
    if (ch == 'y' || ch == 'Y') {
        start_hci_advertising();
    } else {
        printf("\nSkipped Method B. Use the bluetoothctl steps above.\n\n");
    }

    return 0;
}
