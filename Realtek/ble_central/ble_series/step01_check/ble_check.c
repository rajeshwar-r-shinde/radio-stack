/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  STEP 01 — BLE Hardware Check                               ║
 * ║                                                              ║
 * ║  What you learn:                                             ║
 * ║   • How to open an HCI socket (raw Bluetooth socket)         ║
 * ║   • How to list all Bluetooth adapters (hci0, hci1...)       ║
 * ║   • How to read adapter name, address, and features          ║
 * ║   • The difference between HCI and GATT layer                ║
 * ║                                                              ║
 * ║  Build:  gcc ble_check.c -o ble_check -lbluetooth            ║
 * ║  Run:    sudo ./ble_check                                     ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  HCI (Host Controller Interface):
 *  ┌─────────────────────────────────────────────────────────┐
 *  │  Your C code (userspace)                                │
 *  │       ↓ HCI socket (AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)│
 *  │  BlueZ kernel driver                                    │
 *  │       ↓                                                 │
 *  │  Bluetooth chip (USB/UART/SDIO)                         │
 *  └─────────────────────────────────────────────────────────┘
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
/* BlueZ headers — installed via: sudo apt install libbluetooth-dev */
#include <bluetooth/bluetooth.h>   /* bdaddr_t, ba2str()            */
#include <bluetooth/hci.h>         /* hci_dev_info, HCI constants   */
#include <bluetooth/hci_lib.h>
#include "../ble_compat.h"     /* hci_open_dev(), hci_devinfo() */

/* ── Helper: decode BLE features bitmask ───────────────────────── */
static void print_le_features(uint8_t *features)
{
    /* LMP feature page 0, byte 4, bit 6 = LE Supported */
    if (features[4] & 0x40)
        printf("      ✓ LE (BLE) Supported\n");
    else
        printf("      ✗ LE (BLE) NOT Supported\n");

    if (features[4] & 0x80)
        printf("      ✓ Simultaneous LE + BR/EDR\n");
}

/* ── Helper: decode HCI device flags ───────────────────────────── */
static void print_flags(uint32_t flags)
{
    /* Flags come from hci_dev_info.flags — defined in hci.h */
    printf("      Flags: ");
    if (flags & (1 << HCI_UP))        printf("UP ");
    if (flags & (1 << HCI_INIT))      printf("INIT ");
    if (flags & (1 << HCI_RUNNING))   printf("RUNNING ");
    if (flags & (1 << HCI_PSCAN))     printf("PSCAN ");
    if (flags & (1 << HCI_ISCAN))     printf("ISCAN ");
    if (flags & (1 << HCI_RAW))       printf("RAW ");
    printf("\n");
}

int main(void)
{
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  BLE Step 01 — Hardware Check            ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    /* ── Step 1: Open HCI socket ──────────────────────────────── */
    /*
     * AF_BLUETOOTH = Bluetooth address family (like AF_INET for TCP/IP)
     * SOCK_RAW     = raw protocol access
     * BTPROTO_HCI  = HCI protocol (lowest BT layer accessible from userspace)
     */
    int ctl = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (ctl < 0) {
        perror("[!] socket(BTPROTO_HCI) failed — try: sudo ./ble_check");
        return 1;
    }
    printf("[✓] HCI socket opened (fd=%d)\n\n", ctl);

    /* ── Step 2: Query how many HCI devices exist ─────────────── */
    struct hci_dev_list_req *dl;
    struct hci_dev_req      *dr;

    /* Allocate: 1 header + up to 16 device entries */
    dl = calloc(1, HCI_MAX_DEV * sizeof(struct hci_dev_req)
                   + sizeof(struct hci_dev_list_req));
    if (!dl) { perror("calloc"); close(ctl); return 1; }

    dl->dev_num = HCI_MAX_DEV;

    /*
     * HCIGETDEVLIST ioctl: fills dl->dev_req[] with all HCI devices
     * Each entry has: dev_id (hci0=0, hci1=1...) and dev_opt (flags)
     */
    if (ioctl(ctl, HCIGETDEVLIST, dl) < 0) {
        perror("[!] HCIGETDEVLIST ioctl failed");
        free(dl); close(ctl); return 1;
    }

    printf("[*] Found %d Bluetooth adapter(s)\n\n", dl->dev_num);

    /* ── Step 3: Print info for each adapter ─────────────────── */
    dr = dl->dev_req;
    for (int i = 0; i < dl->dev_num; i++, dr++) {
        struct hci_dev_info di;
        memset(&di, 0, sizeof(di));
        di.dev_id = dr->dev_id;

        /* HCIGETDEVINFO: fills hci_dev_info for one device */
        if (ioctl(ctl, HCIGETDEVINFO, &di) < 0) {
            fprintf(stderr, "[!] HCIGETDEVINFO failed for hci%d\n", dr->dev_id);
            continue;
        }

        /* Convert bdaddr_t (6 bytes) to "XX:XX:XX:XX:XX:XX" string */
        char addr_str[18];
        ba2str(&di.bdaddr, addr_str);

        printf("  Adapter: hci%d\n", di.dev_id);
        printf("    Name   : %s\n",  di.name);
        printf("    Address: %s\n",  addr_str);
        printf("    Type   : %s\n",
               di.type == HCI_PRIMARY ? "Primary (Physical)" : "Virtual/AMP");
        print_flags(di.flags);
        print_le_features(di.features);

        /* ACL/SCO stats (packet counts) */
        printf("    ACL packets: tx=%u rx=%u\n",
               di.stat.acl_tx, di.stat.acl_rx);
        printf("\n");
    }

    /* ── Step 4: Get default adapter (hci0) device id ────────── */
    int dev_id = hci_get_route(NULL);   /* NULL = default adapter */
    if (dev_id < 0) {
        printf("[!] No Bluetooth adapter found or adapter is DOWN\n");
        printf("    Try: sudo hciconfig hci0 up\n");
    } else {
        printf("[✓] Default adapter: hci%d\n", dev_id);

        /* Open a device-specific HCI fd for later operations */
        int hci_fd = hci_open_dev(dev_id);
        if (hci_fd >= 0) {
            printf("[✓] HCI device fd opened (fd=%d)\n", hci_fd);
            close(hci_fd);
        }
    }

    printf("\n[*] Next step: Step 02 — BLE Passive Scan\n");

    free(dl);
    close(ctl);
    return 0;
}
