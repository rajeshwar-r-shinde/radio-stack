/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  STEP 03 — Active Scan + Device Table                        ║
 * ║                                                              ║
 * ║  What you learn:                                             ║
 * ║   • Active scanning (sends SCAN_REQ, receives SCAN_RSP)      ║
 * ║   • Building a device table (deduplication by address)       ║
 * ║   • RSSI averaging for signal strength                       ║
 * ║   • Printing a live device discovery table                   ║
 * ║   • Using hci_le_set_scan_parameters() high-level API        ║
 * ║                                                              ║
 * ║  Build:  gcc ble_active_scan.c -o ble_active_scan -lbluetooth║
 * ║  Run:    sudo ./ble_active_scan                              ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  Active vs Passive scanning:
 *
 *  Passive:  Scanner ─────────────────────────────> (listens only)
 *            Device  <─── ADV_IND ────────────────
 *
 *  Active:   Scanner ─────────────────────────────> (listens)
 *            Device  <─── ADV_IND ────────────────
 *            Scanner ─────────────── SCAN_REQ ───>
 *            Device  <─── SCAN_RSP ───────────────  (extra data!)
 *
 *  SCAN_RSP contains additional AD data that doesn't fit in ADV_IND
 *  (ADV_IND payload is limited to 31 bytes)
 *
 *  Fixes applied vs original:
 *    1. print_rssi_bar: replaced Unicode block chars with ASCII # and -
 *       (Unicode caused garbled output on non-UTF8 terminals)
 *    2. parse_ad_into_device: boundary check >= changed to >
 *       (off-by-one was dropping last AD structure in payload)
 *    3. receive loop: pointer p now correctly advances per report
 *       (was always parsing first report for all reports in packet)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "../ble_compat.h"   /* BlueZ 5.x compat — defines removed constants */

/*
 * BlueZ 5.x removed these from public headers. Define manually.
 * Values are from Bluetooth Core Spec Vol 4 Part E.
 */
#ifndef LE_SCAN_PASSIVE
#define LE_SCAN_PASSIVE         0x00
#endif
#ifndef LE_SCAN_ACTIVE
#define LE_SCAN_ACTIVE          0x01
#endif
#ifndef LE_SCAN_ENABLE
#define LE_SCAN_ENABLE          0x01
#endif
#ifndef LE_SCAN_DISABLE
#define LE_SCAN_DISABLE         0x00
#endif
#ifndef LE_PUBLIC_ADDRESS
#define LE_PUBLIC_ADDRESS       0x00
#endif

#ifndef LE_SET_SCAN_PARAMETERS_CP_SIZE
typedef struct {
    uint8_t  type;
    uint16_t interval;
    uint16_t window;
    uint8_t  own_bdaddr_type;
    uint8_t  filter;
} __attribute__((packed)) le_set_scan_parameters_cp;
#define LE_SET_SCAN_PARAMETERS_CP_SIZE 7
#endif

#ifndef LE_SET_SCAN_ENABLE_CP_SIZE
typedef struct {
    uint8_t enable;
    uint8_t filter_dup;
} __attribute__((packed)) le_set_scan_enable_cp;
#define LE_SET_SCAN_ENABLE_CP_SIZE 2
#endif

/* ══════════════════════════════════════════════════════════════════
 * Device Table
 * ══════════════════════════════════════════════════════════════════ */
#define MAX_DEVICES  64

typedef struct {
    bdaddr_t addr;
    uint8_t  addr_type;       /* 0=public  1=random                  */
    char     name[64];        /* from AD type 0x08/0x09 or SCAN_RSP  */
    int8_t   rssi;            /* most recent RSSI reading             */
    int8_t   rssi_avg;        /* exponential moving average           */
    int      rssi_count;      /* number of samples taken              */
    uint8_t  adv_type;        /* ADV_IND=0 DIRECT=1 SCAN=2 NONCONN=3 */
    uint8_t  flags;           /* from AD type 0x01                   */
    uint16_t uuid16[8];       /* 16-bit service UUIDs advertised      */
    int      uuid16_count;
    int8_t   tx_power;        /* from AD type 0x0A  (127=unknown)     */
    int      seen_count;      /* total advertising events from device */
    time_t   last_seen;       /* timestamp of last advertisement      */
} ble_device_t;

static ble_device_t g_devices[MAX_DEVICES];
static int          g_dev_count = 0;
static volatile int g_running   = 1;

static void sig_handler(int s) { (void)s; g_running = 0; }

/* ══════════════════════════════════════════════════════════════════
 * find_or_add()
 *
 * Looks up device by BD address in the table.
 * If not found, allocates a new entry.
 * Returns pointer to device entry, or NULL if table is full.
 * ══════════════════════════════════════════════════════════════════ */
static ble_device_t *find_or_add(const bdaddr_t *addr, uint8_t addr_type)
{
    /* Linear search — fine for up to 64 devices */
    for (int i = 0; i < g_dev_count; i++) {
        if (bacmp(&g_devices[i].addr, addr) == 0)
            return &g_devices[i];
    }

    if (g_dev_count >= MAX_DEVICES) return NULL;  /* table full */

    ble_device_t *d = &g_devices[g_dev_count++];
    memset(d, 0, sizeof(*d));
    bacpy(&d->addr, addr);
    d->addr_type = addr_type;
    d->tx_power  = 127;        /* 127 = "not present" per BT spec */
    return d;
}

/* ══════════════════════════════════════════════════════════════════
 * parse_ad_into_device()
 *
 * Parses the AD (Advertising Data) payload and fills in the
 * device struct fields. AD payload is TLV format:
 *
 *   [length(1)] [type(1)] [data(length-1)] [length] [type] [data] ...
 *
 * BUG FIX: boundary check uses > not >= to avoid off-by-one
 *          that dropped the last AD structure in the payload.
 * ══════════════════════════════════════════════════════════════════ */
static void parse_ad_into_device(ble_device_t *dev,
                                  const uint8_t *data, int len)
{
    int i = 0;
    while (i + 1 < len) {
        uint8_t ad_len = data[i];

        /* FIX: was >= which skipped last AD struct when it ended exactly at len */
        if (ad_len == 0 || i + ad_len > len) break;

        uint8_t        ad_type = data[i + 1];
        const uint8_t *ad_data = &data[i + 2];
        int            ad_dlen = ad_len - 1;   /* data bytes = len - type byte */

        switch (ad_type) {

        case 0x01: /* Flags — LE discoverability + BR/EDR support */
            dev->flags = ad_data[0];
            break;

        case 0x08: /* Short Local Name */
        case 0x09: /* Complete Local Name */
            /* Only store name once — complete name (0x09) preferred */
            if (ad_dlen > 0 && (dev->name[0] == '\0' || ad_type == 0x09)) {
                int copy = ad_dlen < 63 ? ad_dlen : 63;
                memcpy(dev->name, ad_data, copy);
                dev->name[copy] = '\0';
            }
            break;

        case 0x0A: /* TX Power Level — signed byte in dBm */
            dev->tx_power = (int8_t)ad_data[0];
            break;

        case 0x02: /* 16-bit UUIDs (incomplete list) */
        case 0x03: /* 16-bit UUIDs (complete list)   */
            for (int j = 0; j + 1 < ad_dlen && dev->uuid16_count < 8; j += 2) {
                uint16_t uuid = (uint16_t)(ad_data[j] | (ad_data[j+1] << 8));
                /* deduplicate */
                int dup = 0;
                for (int k = 0; k < dev->uuid16_count; k++)
                    if (dev->uuid16[k] == uuid) { dup = 1; break; }
                if (!dup)
                    dev->uuid16[dev->uuid16_count++] = uuid;
            }
            break;

        /* 128-bit UUIDs, Service Data, Manufacturer Data etc. ignored for now */
        }

        i += ad_len + 1;   /* advance past this AD structure */
    }
}

/* ══════════════════════════════════════════════════════════════════
 * update_rssi()
 *
 * Tracks RSSI with an exponential moving average:
 *   avg = 0.8 * old_avg + 0.2 * new_sample
 *
 * This smooths out packet-to-packet variation while still
 * responding to gradual signal strength changes.
 * ══════════════════════════════════════════════════════════════════ */
static void update_rssi(ble_device_t *dev, int8_t rssi)
{
    dev->rssi = rssi;
    dev->rssi_count++;
    if (dev->rssi_count == 1)
        dev->rssi_avg = rssi;
    else
        dev->rssi_avg = (int8_t)(dev->rssi_avg * 0.8f + rssi * 0.2f);
}

/* ══════════════════════════════════════════════════════════════════
 * print_rssi_bar()
 *
 * Renders a 17-character ASCII signal strength bar.
 *
 * FIX: replaced Unicode block chars (█ ░) with ASCII # and -
 *      Unicode caused garbled ? output on terminals without UTF-8.
 *
 * Mapping:
 *   RSSI -100 dBm (weakest)  →  0 bars   [----------------]
 *   RSSI  -65 dBm (medium)   →  8 bars   [########--------]
 *   RSSI  -30 dBm (strongest)→ 17 bars   [#################]
 *
 * Color via ANSI escape codes:
 *   0-5  bars  → red    (weak signal)
 *   6-11 bars  → yellow (medium signal)
 *   12+  bars  → green  (strong signal)
 * ══════════════════════════════════════════════════════════════════ */
static void print_rssi_bar(int8_t rssi)
{
    int strength = (rssi + 100) * 17 / 70;
    if (strength < 0)  strength = 0;
    if (strength > 17) strength = 17;

    const char *color =
        strength <= 5  ? "\033[31m" :    /* red    — weak   */
        strength <= 11 ? "\033[33m" :    /* yellow — medium */
                         "\033[32m";     /* green  — strong */

    printf("%s[", color);
    for (int i = 0; i < 17; i++)
        putchar(i < strength ? '#' : '-');
    printf("]\033[0m");   /* reset color after bar */
}

/* ══════════════════════════════════════════════════════════════════
 * print_table()
 *
 * Redraws the full device table to the terminal.
 * Called every second from the main loop.
 * Uses ANSI escape \033[2J\033[H to clear screen and home cursor
 * so the table refreshes in place (no scrolling).
 * ══════════════════════════════════════════════════════════════════ */
static void print_table(void)
{
    printf("\033[2J\033[H");   /* clear screen, cursor to top-left */

    printf("+------+--------------------+----------+-------------------+--------------------+\n");
    printf("|  BLE Step 03 - Active Scan   [%2d devices]   Ctrl+C to stop                   |\n",
           g_dev_count);
    printf("+------+--------------------+----------+-------------------+--------------------+\n");
    printf("|  #   | Address            | RSSI avg | Signal            | Name / Services    |\n");
    printf("+------+--------------------+----------+-------------------+--------------------+\n");

    for (int i = 0; i < g_dev_count; i++) {
        ble_device_t *d = &g_devices[i];
        char addr_str[18];
        ba2str(&d->addr, addr_str);

        /* Row 1: index, address, RSSI, signal bar */
        printf("| %3d  | %-18s | %4d dBm | ", i+1, addr_str, d->rssi_avg);
        print_rssi_bar(d->rssi_avg);
        printf(" |\n");

        /* Row 2: addr type, seen count, device name */
        printf("|      | %-6s             | seen:%-4d|                   | %-18.18s |\n",
               d->addr_type ? "Random" : "Public",
               d->seen_count,
               d->name[0] ? d->name : "(unnamed)");

        /* Row 3: service UUIDs (only if present) */
        if (d->uuid16_count > 0) {
            printf("|      |                    |          |                   | SVCs: ");
            for (int j = 0; j < d->uuid16_count && j < 3; j++)
                printf("0x%04X ", d->uuid16[j]);
            if (d->uuid16_count > 3) printf("...");
            printf("\n");
        }

        /* TX power if known */
        if (d->tx_power != 127) {
            int path_loss = d->tx_power - d->rssi_avg;
            printf("|      |                    | TX:%3ddBm|                   | PathLoss: %3d dB   |\n",
                   d->tx_power, path_loss);
        }

        printf("+------+--------------------+----------+-------------------+--------------------+\n");
    }

    if (g_dev_count == 0)
        printf("|      Scanning... no BLE devices found yet                                    |\n");

    printf("\n  Signal: \033[31m[weak]\033[0m  \033[33m[medium]\033[0m  \033[32m[strong]\033[0m"
           "   RSSI range: -30 dBm (best) to -100 dBm (worst)\n");
    fflush(stdout);
}

/* ══════════════════════════════════════════════════════════════════
 * main()
 * ══════════════════════════════════════════════════════════════════ */
int main(void)
{
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* ── 1. Open HCI adapter ─────────────────────────────────────── */
    int dev_id = hci_get_route(NULL);
    if (dev_id < 0) {
        fprintf(stderr, "[!] No Bluetooth adapter found\n");
        fprintf(stderr, "    Try: sudo hciconfig hci0 up\n");
        return 1;
    }

    int hci_fd = hci_open_dev(dev_id);
    if (hci_fd < 0) {
        perror("[!] hci_open_dev — try: sudo ./ble_active_scan");
        return 1;
    }
    printf("[*] Opened hci%d\n", dev_id);

    /* ── 2. Set HCI filter — only want LE Meta events ────────────── */
    /*
     * The HCI socket receives ALL events from the chip by default.
     * We set a filter so the kernel only queues LE Meta Events
     * into our socket's receive buffer — ignores everything else.
     */
    struct hci_filter flt;
    hci_filter_clear(&flt);
    hci_filter_set_ptype(HCI_EVENT_PKT,    &flt);  /* HCI events only    */
    hci_filter_set_event(EVT_LE_META_EVENT, &flt);  /* LE meta only       */
    if (setsockopt(hci_fd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0) {
        perror("[!] setsockopt HCI_FILTER");
        close(hci_fd); return 1;
    }

    /* ── 3. Set scan parameters ──────────────────────────────────── */
    /*
     * interval = 0x0050 = 50ms  how often to scan
     * window   = 0x0030 = 30ms  how long each scan lasts
     * duty cycle = window/interval = 30/50 = 60%
     *
     * ACTIVE scan: after receiving ADV_IND, we send SCAN_REQ
     * The device responds with SCAN_RSP containing extra AD data
     * (device name is often only in SCAN_RSP, not ADV_IND)
     */
    {
        struct hci_request        rq;
        le_set_scan_parameters_cp p;
        uint8_t                   status;

        memset(&p, 0, sizeof(p));
        p.type            = LE_SCAN_ACTIVE;     /* send SCAN_REQ     */
        p.interval        = htobs(0x0050);      /* 50ms              */
        p.window          = htobs(0x0030);      /* 30ms              */
        p.own_bdaddr_type = LE_PUBLIC_ADDRESS;  /* use our public MAC */
        p.filter          = 0;                  /* accept all devices */

        memset(&rq, 0, sizeof(rq));
        rq.ogf    = OGF_LE_CTL;
        rq.ocf    = OCF_LE_SET_SCAN_PARAMETERS;
        rq.cparam = &p;
        rq.clen   = LE_SET_SCAN_PARAMETERS_CP_SIZE;
        rq.rparam = &status;
        rq.rlen   = 1;

        if (hci_send_req(hci_fd, &rq, 1000) < 0) {
            perror("[!] LE_SET_SCAN_PARAMETERS"); close(hci_fd); return 1;
        }
        printf("[*] Scan parameters set (active, 50ms interval, 30ms window)\n");
    }

    /* ── 4. Enable scanning ──────────────────────────────────────── */
    /*
     * filter_dup = 0 means show EVERY advertisement including duplicates.
     * This lets us track seen_count and update RSSI average continuously.
     * Set to 1 to see each device only once (chip-level deduplication).
     */
    {
        struct hci_request    rq;
        le_set_scan_enable_cp e;
        uint8_t               status;

        memset(&e, 0, sizeof(e));
        e.enable     = LE_SCAN_ENABLE;
        e.filter_dup = 0;   /* 0=show all  1=chip deduplicates */

        memset(&rq, 0, sizeof(rq));
        rq.ogf    = OGF_LE_CTL;
        rq.ocf    = OCF_LE_SET_SCAN_ENABLE;
        rq.cparam = &e;
        rq.clen   = LE_SET_SCAN_ENABLE_CP_SIZE;
        rq.rparam = &status;
        rq.rlen   = 1;

        if (hci_send_req(hci_fd, &rq, 1000) < 0) {
            perror("[!] LE_SET_SCAN_ENABLE"); close(hci_fd); return 1;
        }
        printf("[*] Scanning started. Press Ctrl+C to stop.\n\n");
    }

    /* ── 5. Main receive loop ─────────────────────────────────────── */
    time_t last_print = 0;

    while (g_running) {

        /*
         * read() blocks until an HCI event arrives.
         * Because we set the HCI filter above, only
         * LE_META_EVENT packets reach us here.
         *
         * Raw packet layout:
         *  buf[0] = 0x04           HCI event packet type
         *  buf[1] = 0x3E           EVT_LE_META_EVENT
         *  buf[2] = param_len      total parameter length
         *  buf[3] = 0x02           LE subevent: ADV_REPORT
         *  buf[4] = num_reports    number of reports in this packet
         *  buf[5+]= report data    one or more advertising reports
         */
        uint8_t buf[HCI_MAX_EVENT_SIZE];
        int r = read(hci_fd, buf, sizeof(buf));

        if (r < 0) {
            if (errno == EINTR) break;   /* Ctrl+C */
            perror("[!] read"); break;
        }
        if (r < 6)                                    continue;
        if (buf[1] != EVT_LE_META_EVENT)              continue;
        if (buf[3] != EVT_LE_ADVERTISING_REPORT)      continue;

        uint8_t  num_reports = buf[4];
        uint8_t *p           = &buf[5];    /* pointer into packet */
        int      remaining   = r - 5;      /* bytes left to parse */

        for (int n = 0; n < num_reports; n++) {

            /*
             * Each advertising report:
             *  p[0]    = event_type  (ADV_IND=0, DIRECT=1, SCAN=2, NONCONN=3, RSP=4)
             *  p[1]    = addr_type   (0=public, 1=random)
             *  p[2..7] = BD address  (6 bytes, little-endian)
             *  p[8]    = data_len    (0-31 bytes of AD payload)
             *  p[9..9+data_len-1] = AD structures
             *  p[9+data_len] = RSSI  (int8, dBm)
             */
            if (remaining < 10) break;   /* need at least 10 bytes */

            uint8_t  evt_type  = p[0];
            uint8_t  addr_type = p[1];
            bdaddr_t addr;
            memcpy(&addr, &p[2], 6);

            uint8_t  data_len = p[8];
            uint8_t *data     = &p[9];

            /* bounds check — data_len must fit in remaining buffer */
            int report_size = 9 + data_len + 1;   /* +1 for RSSI byte */
            if (remaining < report_size) break;

            int8_t rssi = (int8_t)data[data_len];  /* RSSI after AD data */

            /* look up or create device entry */
            ble_device_t *dev = find_or_add(&addr, addr_type);
            if (dev) {
                dev->adv_type = evt_type;
                dev->seen_count++;
                dev->last_seen = time(NULL);
                update_rssi(dev, rssi);
                parse_ad_into_device(dev, data, data_len);
            }

            /* FIX: advance pointer to next report in same HCI packet */
            p         += report_size;
            remaining -= report_size;
        }

        /* Redraw table once per second */
        time_t now = time(NULL);
        if (now != last_print) {
            print_table();
            last_print = now;
        }
    }

    /* ── 6. Disable scanning before exit ─────────────────────────── */
    {
        struct hci_request    rq;
        le_set_scan_enable_cp e;
        uint8_t               status;

        memset(&e, 0, sizeof(e));
        e.enable     = LE_SCAN_DISABLE;
        e.filter_dup = 0;

        memset(&rq, 0, sizeof(rq));
        rq.ogf    = OGF_LE_CTL;
        rq.ocf    = OCF_LE_SET_SCAN_ENABLE;
        rq.cparam = &e;
        rq.clen   = LE_SET_SCAN_ENABLE_CP_SIZE;
        rq.rparam = &status;
        rq.rlen   = 1;
        hci_send_req(hci_fd, &rq, 1000);
    }

    printf("\n[OK] Scan stopped. %d unique devices found.\n", g_dev_count);
    printf("[*]  Next step: Step 04 — Connect to a device\n");
    printf("     Usage: sudo ./bin/ble_connect <ADDRESS> <0=public|1=random>\n");
    printf("     Example (Nokia): sudo ./bin/ble_connect A4:FC:A1:99:3B:1C 0\n");

    close(hci_fd);
    return 0;
}
