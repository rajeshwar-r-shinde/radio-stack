/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  STEP 02 — BLE Passive Scan                                 ║
 * ║                                                              ║
 * ║  What you learn:                                             ║
 * ║   • How to configure HCI LE scan parameters                  ║
 * ║   • How to enable/disable LE scanning                        ║
 * ║   • How to receive raw advertising HCI events                ║
 * ║   • How to parse LE Meta Event → Advertising Report          ║
 * ║   • Difference: passive scan (no scan request sent)          ║
 * ║                                                              ║
 * ║  Build:  gcc ble_scan.c -o ble_scan -lbluetooth              ║
 * ║  Run:    sudo ./ble_scan                                      ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  BLE Scan Flow (HCI commands):
 *
 *  Host                           Controller (chip)
 *  ─────                          ─────────────────
 *  LE_Set_Scan_Parameters ───────→ configures scan window/interval
 *  LE_Set_Scan_Enable     ───────→ starts scanning ch37/38/39
 *                         ←─────── LE_Meta_Event (ADV_IND)
 *                         ←─────── LE_Meta_Event (ADV_IND)
 *                         ←─────── LE_Meta_Event (SCAN_RSP)
 *  LE_Set_Scan_Enable(0)  ───────→ stops scanning
 *
 *  HCI Event Packet format:
 *  ┌──────┬──────────┬──────┬──────────────────────────────────┐
 *  │ 0x04 │ evt_code │ len  │ parameters                        │
 *  │ (1B) │  (1B)    │ (1B) │ (variable)                        │
 *  └──────┴──────────┴──────┴──────────────────────────────────┘
 *
 *  LE Meta Event (evt_code=0x3E) parameters:
 *  ┌────────────┬────────────┬──── Advertising Report ──────────┐
 *  │ subevent   │ num_reports│ event_type│addr_type│addr│len│data│
 *  │ (1B=0x02)  │ (1B)       │ (1B)      │ (1B)    │(6B)│(1B)│   │
 *  └────────────┴────────────┴──────────────────────────────────┘
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "../ble_compat.h"

/* ── Config ─────────────────────────────────────────────────────── */
#define SCAN_DURATION_SEC   15       /* How long to scan             */
#define SCAN_INTERVAL       0x0010   /* 10ms — how often to scan     */
#define SCAN_WINDOW         0x0010   /* 10ms — how long per interval */

/* ── Globals ─────────────────────────────────────────────────────── */
static volatile int g_running = 1;
static int          g_hci_fd  = -1;

static void sig_handler(int s) { (void)s; g_running = 0; }

/* ── AD Type names (partial list) ────────────────────────────────── */
static const char *ad_type_name(uint8_t type)
{
    switch (type) {
    case 0x01: return "Flags";
    case 0x02: return "16-bit UUIDs (incomplete)";
    case 0x03: return "16-bit UUIDs (complete)";
    case 0x04: return "32-bit UUIDs (incomplete)";
    case 0x05: return "32-bit UUIDs (complete)";
    case 0x06: return "128-bit UUIDs (incomplete)";
    case 0x07: return "128-bit UUIDs (complete)";
    case 0x08: return "Short Local Name";
    case 0x09: return "Complete Local Name";
    case 0x0A: return "TX Power Level";
    case 0x0D: return "Class of Device";
    case 0x16: return "Service Data (16-bit UUID)";
    case 0x20: return "Service Data (32-bit UUID)";
    case 0x21: return "Service Data (128-bit UUID)";
    case 0xFF: return "Manufacturer Specific";
    default:   return "Unknown";
    }
}

/* ── Parse and print AD structures from advertising payload ──────── */
static void parse_ad_data(const uint8_t *data, int len)
{
    int i = 0;
    while (i < len) {
        uint8_t ad_len  = data[i];      /* length of (type + data) */
        if (ad_len == 0 || i + ad_len > len) break;

        uint8_t        ad_type = data[i + 1];
        const uint8_t *ad_data = &data[i + 2];
        int            ad_dlen = ad_len - 1;

        printf("      [0x%02X] %-32s ", ad_type, ad_type_name(ad_type));

        switch (ad_type) {
        case 0x01: /* Flags */
            printf("0x%02X (", ad_data[0]);
            if (ad_data[0] & 0x02) printf("LE-General-Disc ");
            if (ad_data[0] & 0x04) printf("LE-Limited-Disc ");
            if (ad_data[0] & 0x08) printf("No-BR/EDR ");
            printf(")");
            break;

        case 0x08: /* Short name */
        case 0x09: /* Complete name */
            printf("\"");
            for (int j = 0; j < ad_dlen; j++)
                putchar(ad_data[j] >= 0x20 ? ad_data[j] : '?');
            printf("\"");
            break;

        case 0x0A: /* TX Power */
            printf("%d dBm", (int8_t)ad_data[0]);
            break;

        case 0x02: /* 16-bit UUIDs */
        case 0x03:
            for (int j = 0; j + 1 < ad_dlen; j += 2)
                printf("0x%04X ", (ad_data[j+1] << 8) | ad_data[j]);
            break;

        case 0xFF: /* Manufacturer specific */
            if (ad_dlen >= 2) {
                uint16_t company = (ad_data[1] << 8) | ad_data[0];
                printf("Company=0x%04X data=[", company);
                for (int j = 2; j < ad_dlen && j < 10; j++)
                    printf("%02X ", ad_data[j]);
                if (ad_dlen > 10) printf("...");
                printf("]");
            }
            break;

        default:
            /* Print raw hex for unknown types */
            printf("[");
            for (int j = 0; j < ad_dlen && j < 8; j++)
                printf("%02X ", ad_data[j]);
            if (ad_dlen > 8) printf("...");
            printf("]");
            break;
        }
        printf("\n");
        i += ad_len + 1;
    }
}

/* ── Parse one LE Advertising Report event ──────────────────────── */
static void parse_le_adv_report(const uint8_t *data, int len)
{
    if (len < 9) return;  /* minimum: type+addr_type+addr+data_len */

    /*
     * LE Advertising Report structure (after subevent + num_reports):
     *  event_type (1) | addr_type (1) | address (6) | data_len (1) | data (N) | RSSI (1)
     */
    uint8_t  evt_type  = data[0];
    uint8_t  addr_type = data[1];
    bdaddr_t addr;
    memcpy(&addr, &data[2], 6);

    char addr_str[18];
    ba2str(&addr, addr_str);

    uint8_t data_len = data[8];
    int8_t  rssi     = (data_len + 9 < len) ? (int8_t)data[9 + data_len] : 0;

    /* Event type meanings */
    const char *evt_names[] = {
        "ADV_IND",       /* 0x00 - connectable undirected   */
        "ADV_DIRECT_IND",/* 0x01 - connectable directed     */
        "ADV_SCAN_IND",  /* 0x02 - scannable undirected     */
        "ADV_NONCONN_IND",/* 0x03 - non-connectable          */
        "SCAN_RSP",      /* 0x04 - scan response            */
    };

    printf("\n  ┌─ Device Found ─────────────────────────────────\n");
    printf("  │  Address  : %s (%s)\n", addr_str,
           addr_type == 0 ? "Public" : "Random");
    printf("  │  Adv Type : %s (0x%02X)\n",
           evt_type < 5 ? evt_names[evt_type] : "Unknown", evt_type);
    printf("  │  RSSI     : %d dBm\n", rssi);
    printf("  │  AD Length: %d bytes\n", data_len);
    if (data_len > 0) {
        printf("  │  AD Data  :\n");
        parse_ad_data(&data[9], data_len);
    }
    printf("  └────────────────────────────────────────────────\n");
}

/* ── Enable/Disable LE scan via HCI ioctls ──────────────────────── */
static int le_scan_enable(int fd, int enable)
{
    struct hci_request       rq;
    le_set_scan_enable_cp    scan_cp;
    uint8_t                  status;

    memset(&scan_cp, 0, sizeof(scan_cp));
    scan_cp.enable     = enable ? LE_SCAN_ENABLE : LE_SCAN_DISABLE;
    scan_cp.filter_dup = 0;   /* 0 = show duplicates, 1 = filter them */

    memset(&rq, 0, sizeof(rq));
    rq.ogf    = OGF_LE_CTL;               /* Opcode Group Field: LE Controller */
    rq.ocf    = OCF_LE_SET_SCAN_ENABLE;   /* Opcode Command Field              */
    rq.cparam = &scan_cp;
    rq.clen   = LE_SET_SCAN_ENABLE_CP_SIZE;
    rq.rparam = &status;
    rq.rlen   = 1;

    if (hci_send_req(fd, &rq, 1000) < 0) {
        perror("[!] hci_send_req(LE_SET_SCAN_ENABLE)");
        return -1;
    }
    if (status) {
        fprintf(stderr, "[!] LE_SET_SCAN_ENABLE failed, status=0x%02X\n", status);
        return -1;
    }
    return 0;
}

static int le_scan_set_params(int fd)
{
    struct hci_request      rq;
    le_set_scan_parameters_cp params_cp;
    uint8_t                 status;

    memset(&params_cp, 0, sizeof(params_cp));
    params_cp.type          = LE_SCAN_PASSIVE;   /* 0x00=passive, 0x01=active */
    params_cp.interval      = htobs(SCAN_INTERVAL);
    params_cp.window        = htobs(SCAN_WINDOW);
    params_cp.own_bdaddr_type = LE_PUBLIC_ADDRESS; /* use public adapter addr */
    params_cp.filter        = 0;                 /* 0=accept all            */

    memset(&rq, 0, sizeof(rq));
    rq.ogf    = OGF_LE_CTL;
    rq.ocf    = OCF_LE_SET_SCAN_PARAMETERS;
    rq.cparam = &params_cp;
    rq.clen   = LE_SET_SCAN_PARAMETERS_CP_SIZE;
    rq.rparam = &status;
    rq.rlen   = 1;

    if (hci_send_req(fd, &rq, 1000) < 0) {
        perror("[!] hci_send_req(LE_SET_SCAN_PARAMETERS)");
        return -1;
    }
    return 0;
}

/* ── Main ────────────────────────────────────────────────────────── */
int main(void)
{
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  BLE Step 02 — Passive Scan              ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* 1. Open HCI device */
    int dev_id = hci_get_route(NULL);
    if (dev_id < 0) { fprintf(stderr,"[!] No BLE adapter found\n"); return 1; }

    g_hci_fd = hci_open_dev(dev_id);
    if (g_hci_fd < 0) {
        perror("[!] hci_open_dev — try: sudo ./ble_scan");
        return 1;
    }
    printf("[✓] Opened hci%d (fd=%d)\n", dev_id, g_hci_fd);

    /* 2. Set HCI filter — tell kernel to pass LE Meta events to us */
    struct hci_filter flt;
    hci_filter_clear(&flt);
    hci_filter_set_ptype(HCI_EVENT_PKT, &flt);        /* only events     */
    hci_filter_set_event(EVT_LE_META_EVENT, &flt);    /* only LE meta    */
    if (setsockopt(g_hci_fd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0) {
        perror("[!] setsockopt HCI_FILTER"); close(g_hci_fd); return 1;
    }

    /* 3. Configure scan parameters */
    if (le_scan_set_params(g_hci_fd) < 0) { close(g_hci_fd); return 1; }
    printf("[*] Scan parameters set (interval=10ms, window=10ms, passive)\n");

    /* 4. Enable scanning */
    if (le_scan_enable(g_hci_fd, 1) < 0) { close(g_hci_fd); return 1; }
    printf("[✓] LE scan ENABLED — listening for %d seconds...\n\n",
           SCAN_DURATION_SEC);

    /* 5. Read HCI events in a loop */
    time_t start = time(NULL);
    int    count = 0;

    while (g_running && (time(NULL) - start) < SCAN_DURATION_SEC) {

        /*
         * Raw HCI event packet layout:
         *  [0]      = packet type  (0x04 = HCI event)
         *  [1]      = event code   (0x3E = LE Meta Event)
         *  [2]      = param length
         *  [3]      = LE subevent  (0x02 = LE Advertising Report)
         *  [4]      = num_reports  (usually 1)
         *  [5...]   = advertising report(s)
         */
        uint8_t buf[HCI_MAX_EVENT_SIZE];
        int     r = read(g_hci_fd, buf, sizeof(buf));
        if (r < 0) {
            if (errno == EINTR) break;
            perror("[!] read HCI event"); break;
        }

        /* buf[1] = event code, buf[3] = LE subevent */
        if (r < 5) continue;
        if (buf[1] != EVT_LE_META_EVENT) continue;
        if (buf[3] != EVT_LE_ADVERTISING_REPORT) continue;

        uint8_t num_reports = buf[4];
        uint8_t *report     = &buf[5];
        
	int      remaining   = r - 5;

        for (int i = 0; i < num_reports; i++) {
            //parse_le_adv_report(report, r - 5);
            //count++;
	    uint8_t rpt_data_len = report[8];
    	    int     rpt_total    = 9 + rpt_data_len + 1;  /* +1 = RSSI byte */
    	    parse_le_adv_report(report, rpt_total);
    	    report    += rpt_total;   /* ← move to next report */
            remaining -= rpt_total;
        }
    }

    /* 6. Disable scan */
    le_scan_enable(g_hci_fd, 0);
    printf("\n[✓] Scan complete. Found %d advertising events.\n", count);
    printf("[*] Next step: Step 03 — Inquiry + Name Resolution\n");

    close(g_hci_fd);
    return 0;
}
