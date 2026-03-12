/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  STEP 08 — Subscribe to Notifications                       ║
 * ║                                                              ║
 * ║  What you learn:                                             ║
 * ║   • Full notification subscription flow                      ║
 * ║   • Auto-discovering CCCD descriptors                        ║
 * ║   • Receiving ATT_HANDLE_VALUE_NOTIFICATION                  ║
 * ║   • Handling ATT_HANDLE_VALUE_INDICATION + sending CONFIRM   ║
 * ║   • Decoding real-time sensor data from notifications         ║
 * ║   • Using select() for non-blocking ATT receive              ║
 * ║                                                              ║
 * ║  Build:  gcc ble_notify.c -o ble_notify -lbluetooth          ║
 * ║  Run:    sudo ./ble_notify <ADDR> [0|1]                      ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  Full Notification Flow:
 *
 *  1. Connect (L2CAP ATT socket)
 *  2. Discover services + characteristics
 *  3. For each NOTIFY/INDICATE capable characteristic:
 *     a. Find its CCCD descriptor handle
 *     b. Write 0x0001 (or 0x0002) to CCCD → enables push
 *  4. Enter receive loop:
 *     - ATT_HANDLE_VALUE_NOTIFICATION (0x1B) → decode + print
 *     - ATT_HANDLE_VALUE_INDICATION   (0x1D) → decode + send CONFIRM (0x1E)
 *  5. On exit: write 0x0000 to all CCCDs → disable notifications
 *  6. Disconnect
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/select.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "../ble_compat.h"
#include <bluetooth/l2cap.h>

#define ATT_OP_ERROR             0x01
#define ATT_OP_MTU_REQ           0x02
#define ATT_OP_MTU_RSP           0x03
#define ATT_OP_FIND_INFO_REQ     0x04
#define ATT_OP_FIND_INFO_RSP     0x05
#define ATT_OP_READ_BY_TYPE_REQ  0x08
#define ATT_OP_READ_BY_TYPE_RSP  0x09
#define ATT_OP_READ_REQ          0x0A
#define ATT_OP_READ_RSP          0x0B
#define ATT_OP_READ_BY_GROUP_REQ 0x10
#define ATT_OP_READ_BY_GROUP_RSP 0x11
#define ATT_OP_WRITE_REQ         0x12
#define ATT_OP_WRITE_RSP         0x13
#define ATT_OP_NOTIFY            0x1B
#define ATT_OP_INDICATE          0x1D
#define ATT_OP_CONFIRM           0x1E

#define UUID_CHARACTERISTIC      0x2803
#define UUID_CCCD                0x2902
#define PROP_NOTIFY              0x10
#define PROP_INDICATE            0x20
#define MAX_SUBSCRIPTIONS        16

/* ── Subscription tracking ───────────────────────────────────────── */
typedef struct {
    uint16_t value_handle;
    uint16_t cccd_handle;
    uint16_t char_uuid16;
    int      is_indication;
} subscription_t;

static subscription_t g_subs[MAX_SUBSCRIPTIONS];
static int             g_sub_count = 0;
static volatile int    g_running   = 1;
static int             g_att_fd    = -1;

static void sig_handler(int s)
{
    (void)s;
    g_running = 0;
}

/* ── ATT helpers ─────────────────────────────────────────────────── */
static int att_send(int fd, uint8_t *buf, int len)
{
    return send(fd, buf, len, 0);
}

static int att_recv_timeout(int fd, uint8_t *buf, int bufsz, int ms)
{
    fd_set rfds;
    struct timeval tv = { .tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000 };
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    int r = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (r <= 0) return r;  /* 0=timeout, -1=error */
    return recv(fd, buf, bufsz, 0);
}

/* ── Write to a handle (with response) ──────────────────────────── */
static int att_write(int fd, uint16_t handle, const uint8_t *data, int len)
{
    uint8_t pkt[256];
    pkt[0] = ATT_OP_WRITE_REQ;
    pkt[1] = handle & 0xFF;
    pkt[2] = (handle >> 8) & 0xFF;
    memcpy(&pkt[3], data, len);
    att_send(fd, pkt, 3 + len);

    uint8_t rsp[16];
    int r = att_recv_timeout(fd, rsp, sizeof(rsp), 2000);
    if (r < 1) return -1;
    return (rsp[0] == ATT_OP_WRITE_RSP) ? 0 : -1;
}

/* ── MTU exchange ────────────────────────────────────────────────── */
static uint16_t exchange_mtu(int fd)
{
    uint8_t req[3] = { ATT_OP_MTU_REQ, 0x00, 0x02 }; /* client MTU = 512 */
    req[1] = 0x00; req[2] = 0x02;  /* 512 little-endian */
    att_send(fd, req, 3);
    uint8_t rsp[8];
    int r = att_recv_timeout(fd, rsp, sizeof(rsp), 1000);
    if (r >= 3 && rsp[0] == ATT_OP_MTU_RSP)
        return rsp[1] | (rsp[2] << 8);
    return 23;
}

/* ── Find CCCD handle by scanning attribute range ─────────────────── */
/*
 * After a characteristic value handle, the CCCD (UUID=0x2902) descriptor
 * appears at value_handle+1 or value_handle+2.
 * We use ATT_FIND_INFORMATION_REQ to list all handle-UUID pairs in range.
 */
static uint16_t find_cccd_handle(int fd, uint16_t value_handle,
                                  uint16_t range_end)
{
    if (value_handle + 1 > range_end) return 0;

    uint8_t req[5];
    req[0] = ATT_OP_FIND_INFO_REQ;
    req[1] = (value_handle + 1) & 0xFF;
    req[2] = ((value_handle + 1) >> 8) & 0xFF;
    req[3] = range_end & 0xFF;
    req[4] = (range_end >> 8) & 0xFF;
    att_send(fd, req, 5);

    uint8_t rsp[256];
    int r = att_recv_timeout(fd, rsp, sizeof(rsp), 1000);
    if (r < 4 || rsp[0] != ATT_OP_FIND_INFO_RSP) return 0;

    uint8_t fmt = rsp[1];
    if (fmt != 1) return 0;  /* only handle 16-bit UUIDs */

    int pos = 2;
    while (pos + 4 <= r) {
        uint16_t h = rsp[pos] | (rsp[pos+1] << 8);
        uint16_t u = rsp[pos+2] | (rsp[pos+3] << 8);
        if (u == UUID_CCCD) return h;
        pos += 4;
    }
    return 0;
}

/* ── Discover all characteristics and subscribe to notifiable ones ── */
static void discover_and_subscribe(int fd)
{
    uint16_t start = 0x0001;
    uint16_t prev_val_handle = 0;
    uint8_t  prev_props = 0;
    uint16_t prev_uuid16 = 0;

    while (start <= 0xFFFF && g_sub_count < MAX_SUBSCRIPTIONS) {
        uint8_t req[7];
        req[0] = ATT_OP_READ_BY_TYPE_REQ;
        req[1] = start & 0xFF; req[2] = (start >> 8) & 0xFF;
        req[3] = 0xFF;         req[4] = 0xFF;
        req[5] = UUID_CHARACTERISTIC & 0xFF;
        req[6] = (UUID_CHARACTERISTIC >> 8) & 0xFF;
        att_send(fd, req, 7);

        uint8_t rsp[256];
        int r = att_recv_timeout(fd, rsp, sizeof(rsp), 1000);
        if (r < 4 || rsp[0] == ATT_OP_ERROR) break;
        if (rsp[0] != ATT_OP_READ_BY_TYPE_RSP) break;

        uint8_t item_len = rsp[1];
        int pos = 2, uuid_len = item_len - 5;

        while (pos + item_len <= r) {
            uint16_t decl_handle  = rsp[pos]   | (rsp[pos+1] << 8);
            uint8_t  props        = rsp[pos+2];
            uint16_t value_handle = rsp[pos+3] | (rsp[pos+4] << 8);
            uint16_t uuid16 = 0;
            if (uuid_len == 2)
                uuid16 = rsp[pos+5] | (rsp[pos+6] << 8);

            /* Handle previous characteristic's CCCD range */
            if (prev_val_handle && (prev_props & (PROP_NOTIFY | PROP_INDICATE))) {
                uint16_t range_end = decl_handle - 1;
                uint16_t cccd = find_cccd_handle(fd, prev_val_handle, range_end);
                if (cccd) {
                    printf("  [+] Found notifiable char 0x%04X  CCCD=0x%04X\n",
                           prev_val_handle, cccd);
                    g_subs[g_sub_count].value_handle  = prev_val_handle;
                    g_subs[g_sub_count].cccd_handle   = cccd;
                    g_subs[g_sub_count].char_uuid16   = prev_uuid16;
                    g_subs[g_sub_count].is_indication = !(prev_props & PROP_NOTIFY);
                    g_sub_count++;
                }
            }

            prev_val_handle = value_handle;
            prev_props      = props;
            prev_uuid16     = uuid16;
            start           = value_handle + 1;
            pos            += item_len;
            (void)decl_handle;
        }
    }

    /* Handle last characteristic */
    if (prev_val_handle && (prev_props & (PROP_NOTIFY | PROP_INDICATE))) {
        uint16_t cccd = find_cccd_handle(fd, prev_val_handle, 0xFFFF);
        if (cccd) {
            printf("  [+] Found notifiable char 0x%04X  CCCD=0x%04X\n",
                   prev_val_handle, cccd);
            g_subs[g_sub_count].value_handle  = prev_val_handle;
            g_subs[g_sub_count].cccd_handle   = cccd;
            g_subs[g_sub_count].char_uuid16   = prev_uuid16;
            g_subs[g_sub_count].is_indication = !(prev_props & PROP_NOTIFY);
            g_sub_count++;
        }
    }
}

/* ── Enable / Disable all subscriptions ──────────────────────────── */
static void set_all_notifications(int fd, int enable)
{
    for (int i = 0; i < g_sub_count; i++) {
        uint8_t cccd[2];
        if (enable) {
            cccd[0] = g_subs[i].is_indication ? 0x02 : 0x01;
            cccd[1] = 0x00;
        } else {
            cccd[0] = 0x00; cccd[1] = 0x00;
        }

        int r = att_write(fd, g_subs[i].cccd_handle, cccd, 2);
        printf("  [%s] CCCD 0x%04X → %s %s\n",
               r == 0 ? "✓" : "!",
               g_subs[i].cccd_handle,
               enable ? "ENABLED" : "DISABLED",
               r == 0 ? "" : "(failed)");
    }
}

/* ── Decode notification value ───────────────────────────────────── */
static void decode_notification(uint16_t handle, uint16_t uuid16,
                                  const uint8_t *data, int len)
{
    printf("  ━━ Notification [handle=0x%04X uuid=0x%04X] (%d bytes)\n",
           handle, uuid16, len);
    printf("     Hex : ");
    for (int i = 0; i < len && i < 20; i++) printf("%02X ", data[i]);
    if (len > 20) printf("...");
    printf("\n");

    /* Decode based on UUID */
    switch (uuid16) {
    case 0x2A19: /* Battery Level */
        if (len >= 1)
            printf("     Value: Battery %u%%\n", data[0]);
        break;

    case 0x2A37: /* Heart Rate Measurement */
        if (len >= 2) {
            uint16_t bpm = (data[0] & 1) ? (data[1]|(data[2]<<8)) : data[1];
            printf("     Value: Heart Rate %u bpm\n", bpm);
        }
        break;

    case 0x2A6E: /* Temperature (int16 × 0.01 °C) */
        if (len >= 2) {
            int16_t t = (int16_t)(data[0] | (data[1] << 8));
            printf("     Value: Temperature %.2f °C\n", t / 100.0);
        }
        break;

    case 0x2A6F: /* Humidity (uint16 × 0.01 %RH) */
        if (len >= 2) {
            uint16_t h = data[0] | (data[1] << 8);
            printf("     Value: Humidity %.2f %%RH\n", h / 100.0);
        }
        break;

    default:
        /* Try as string */
        {
            int printable = 1;
            for (int i = 0; i < len; i++)
                if (data[i] < 0x20 || data[i] >= 0x7F) { printable=0; break; }
            if (printable && len > 0) {
                printf("     String: \"");
                for (int i = 0; i < len; i++) putchar(data[i]);
                printf("\"\n");
            }
        }
        break;
    }
}

/* ── Notification receive loop ───────────────────────────────────── */
static void notification_loop(int fd)
{
    int count = 0;
    printf("\n── Receiving Notifications (Ctrl+C to stop) ─────────────────\n\n");

    while (g_running) {
        uint8_t buf[512];
        int r = att_recv_timeout(fd, buf, sizeof(buf), 100);

        if (r == 0) continue;  /* timeout — check g_running */
        if (r < 0) {
            if (errno == EINTR) break;
            break;
        }
        if (r < 3) continue;

        if (buf[0] == ATT_OP_NOTIFY || buf[0] == ATT_OP_INDICATE) {
            uint16_t notif_handle = buf[1] | (buf[2] << 8);
            uint8_t *notif_data   = &buf[3];
            int      notif_len    = r - 3;
            count++;

            /* Find matching UUID */
            uint16_t uuid16 = 0;
            for (int i = 0; i < g_sub_count; i++)
                if (g_subs[i].value_handle == notif_handle)
                    { uuid16 = g_subs[i].char_uuid16; break; }

            decode_notification(notif_handle, uuid16, notif_data, notif_len);

            /* Send confirm for indications */
            if (buf[0] == ATT_OP_INDICATE) {
                uint8_t confirm = ATT_OP_CONFIRM;
                att_send(fd, &confirm, 1);
                printf("     [→ CONFIRM sent]\n");
            }
            printf("\n");
        }
    }

    printf("[✓] Notification loop ended. Received %d notifications.\n\n",
           count);
}

/* ── Open ATT socket ─────────────────────────────────────────────── */
static int open_att(const char *addr_str, uint8_t addr_type,
                    const bdaddr_t *src)
{
    bdaddr_t dst;
    str2ba(addr_str, &dst);
    struct sockaddr_l2 sa = {0};
    int fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (fd < 0) { perror("socket"); return -1; }
    sa.l2_family=AF_BLUETOOTH; sa.l2_bdaddr=*src;
    sa.l2_cid=htobs(4); sa.l2_bdaddr_type=BDADDR_LE_PUBLIC;
    bind(fd, (struct sockaddr *)&sa, sizeof(sa));
    sa.l2_bdaddr=dst; sa.l2_bdaddr_type=addr_type;
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("[!] connect"); close(fd); return -1;
    }
    return fd;
}

/* ── Main ────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  BLE Step 08 — Notifications             ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    if (argc < 2) {
        printf("Usage: sudo ./ble_notify <ADDR> [0=public|1=random]\n");
        return 1;
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    uint8_t addr_type = argc >= 3 ? atoi(argv[2]) : BDADDR_LE_PUBLIC;

    int dev_id = hci_get_route(NULL);
    int hci_fd = hci_open_dev(dev_id);
    bdaddr_t src;
    { struct hci_dev_info di; di.dev_id=dev_id;
      ioctl(hci_fd,HCIGETDEVINFO,&di); bacpy(&src,&di.bdaddr); }
    close(hci_fd);

    printf("[*] Connecting to %s...\n", argv[1]);
    g_att_fd = open_att(argv[1], addr_type, &src);
    if (g_att_fd < 0) return 1;
    printf("[✓] Connected (fd=%d)\n\n", g_att_fd);

    /* MTU */
    uint16_t mtu = exchange_mtu(g_att_fd);
    printf("[*] MTU = %u bytes\n\n", mtu);

    /* Discover all notifiable characteristics */
    printf("── Discovering Notifiable Characteristics ───────────────────\n");
    discover_and_subscribe(g_att_fd);
    printf("  → Found %d notifiable characteristic(s)\n\n", g_sub_count);

    if (g_sub_count == 0) {
        printf("[!] No notifiable characteristics found on this device.\n");
        close(g_att_fd);
        return 0;
    }

    /* Enable all notifications */
    printf("── Enabling Notifications (writing CCCD) ────────────────────\n");
    set_all_notifications(g_att_fd, 1);

    /* Receive notifications */
    notification_loop(g_att_fd);

    /* Disable all notifications before disconnecting */
    printf("── Disabling Notifications ──────────────────────────────────\n");
    set_all_notifications(g_att_fd, 0);

    printf("[*] Next: Step 09 — Custom BLE Peripheral (Advertiser)\n");
    printf("[*] Or use bluetoothctl / gatttool to explore manually\n");

    close(g_att_fd);
    return 0;
}
