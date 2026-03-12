/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  STEP 07 — Write Characteristics                            ║
 * ║                                                              ║
 * ║  What you learn:                                             ║
 * ║   • ATT_WRITE_REQ / ATT_WRITE_RSP (with acknowledgment)     ║
 * ║   • ATT_WRITE_CMD (no response, fire-and-forget)            ║
 * ║   • ATT_PREPARE_WRITE / ATT_EXECUTE_WRITE (long writes)      ║
 * ║   • Writing CCCD to enable notifications (0x0001)            ║
 * ║   • Writing CCCD to enable indications (0x0002)              ║
 * ║                                                              ║
 * ║  Build:  gcc ble_write.c -o ble_write -lbluetooth            ║
 * ║  Run:    sudo ./ble_write <ADDR> [0|1] <handle> <hex_bytes>  ║
 * ║  Example: sudo ./ble_write AA:BB:CC:DD:EE:FF 1 0x0013 01 00  ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  ATT Write Protocol:
 *
 *  ATT_WRITE_REQ (expects acknowledgment):
 *  Client ──[ 0x12 | handle(2) | value(...) ]──────────────────→
 *         ←─[ 0x13 ] (ATT_WRITE_RSP, success)────────────────
 *    OR   ←─[ 0x01 | 0x12 | handle(2) | error_code ]─────────
 *
 *  ATT_WRITE_CMD (no response, used for high-rate data):
 *  Client ──[ 0x52 | handle(2) | value(...) ]──────────────────→
 *  (no response expected)
 *
 *  Long Write (> MTU-3 bytes):
 *  Client ──[ 0x16 | handle(2) | offset(2) | chunk ]──────────→  PREPARE
 *         ←─[ 0x17 | handle(2) | offset(2) | chunk ]──────────   PREPARE RSP
 *  Client ──[ 0x16 | handle(2) | offset(2) | chunk2 ]─────────→  PREPARE (next)
 *         ←─[ 0x17 ... ]──────────────────────────────────────
 *  Client ──[ 0x18 | 0x01 ]────────────────────────────────────→  EXECUTE
 *         ←─[ 0x19 ]──────────────────────────────────────────   EXECUTE RSP
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "../ble_compat.h"
#include <bluetooth/l2cap.h>

/* ── ATT Opcodes ─────────────────────────────────────────────────── */
#define ATT_OP_ERROR            0x01
#define ATT_OP_MTU_REQ          0x02
#define ATT_OP_MTU_RSP          0x03
#define ATT_OP_READ_BY_TYPE_REQ 0x08
#define ATT_OP_READ_BY_TYPE_RSP 0x09
#define ATT_OP_READ_REQ         0x0A
#define ATT_OP_READ_RSP         0x0B
#define ATT_OP_READ_BY_GROUP_REQ 0x10
#define ATT_OP_READ_BY_GROUP_RSP 0x11
#define ATT_OP_WRITE_REQ        0x12
#define ATT_OP_WRITE_RSP        0x13
#define ATT_OP_PREPARE_WRITE_REQ 0x16
#define ATT_OP_PREPARE_WRITE_RSP 0x17
#define ATT_OP_EXECUTE_WRITE_REQ 0x18
#define ATT_OP_EXECUTE_WRITE_RSP 0x19
#define ATT_OP_NOTIFY           0x1B
#define ATT_OP_INDICATE         0x1D
#define ATT_OP_CONFIRM          0x1E
#define ATT_OP_WRITE_CMD        0x52

/* ── CCCD UUID & values ──────────────────────────────────────────── */
#define UUID_CCCD               0x2902
#define CCCD_NOTIFY             0x0001   /* enable notifications */
#define CCCD_INDICATE           0x0002   /* enable indications   */
#define CCCD_DISABLE            0x0000   /* disable both         */

static volatile int g_running = 1;
static void sig_handler(int s) { (void)s; g_running = 0; }

/* ── ATT Write With Response ─────────────────────────────────────── */
static int att_write_req(int fd, uint16_t handle,
                          const uint8_t *data, int len)
{
    /* Packet: [0x12][handle_lo][handle_hi][value...] */
    int pkt_len = 3 + len;
    uint8_t *pkt = malloc(pkt_len);
    pkt[0] = ATT_OP_WRITE_REQ;
    pkt[1] = handle & 0xFF;
    pkt[2] = (handle >> 8) & 0xFF;
    memcpy(&pkt[3], data, len);

    printf("  [→] ATT_WRITE_REQ handle=0x%04X data=[", handle);
    for (int i = 0; i < len && i < 8; i++) printf("%02X ", data[i]);
    if (len > 8) printf("...");
    printf("]\n");

    send(fd, pkt, pkt_len, 0);
    free(pkt);

    /* Wait for ATT_WRITE_RSP or ATT_ERROR */
    uint8_t rsp[16];
    int r = recv(fd, rsp, sizeof(rsp), 0);
    if (r < 1) { perror("recv"); return -1; }

    if (rsp[0] == ATT_OP_WRITE_RSP) {
        printf("  [✓] ATT_WRITE_RSP received (success)\n");
        return 0;
    }

    if (rsp[0] == ATT_OP_ERROR) {
        uint8_t code = r >= 5 ? rsp[4] : 0;
        fprintf(stderr, "  [!] ATT Error 0x%02X on WRITE_REQ\n", code);
        return -1;
    }

    fprintf(stderr, "  [!] Unexpected response: 0x%02X\n", rsp[0]);
    return -1;
}

/* ── ATT Write Without Response (fire and forget) ─────────────────── */
static int att_write_cmd(int fd, uint16_t handle,
                          const uint8_t *data, int len)
{
    int pkt_len = 3 + len;
    uint8_t *pkt = malloc(pkt_len);
    pkt[0] = ATT_OP_WRITE_CMD;   /* 0x52 — no response expected */
    pkt[1] = handle & 0xFF;
    pkt[2] = (handle >> 8) & 0xFF;
    memcpy(&pkt[3], data, len);

    printf("  [→] ATT_WRITE_CMD handle=0x%04X (no ack) data=[", handle);
    for (int i = 0; i < len && i < 8; i++) printf("%02X ", data[i]);
    printf("]\n");

    int r = send(fd, pkt, pkt_len, 0);
    free(pkt);
    return r < 0 ? -1 : 0;
}

/* ── Enable/Disable Notifications via CCCD ───────────────────────── */
/*
 * Every notifiable characteristic has a CCCD descriptor at handle+1 or +2.
 * Writing 0x0001 to it enables notifications.
 * The CCCD handle must be discovered (it follows the char value handle).
 *
 * CCCD value format: uint16 little-endian
 *   0x0000 = disabled
 *   0x0001 = notifications enabled
 *   0x0002 = indications enabled
 *   0x0003 = both enabled
 */
static int write_cccd(int fd, uint16_t cccd_handle, uint16_t value)
{
    uint8_t data[2];
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;

    printf("  [*] Writing CCCD [handle=0x%04X] = 0x%04X (%s)\n",
           cccd_handle, value,
           value == CCCD_NOTIFY   ? "NOTIFY enabled" :
           value == CCCD_INDICATE ? "INDICATE enabled" :
           value == CCCD_DISABLE  ? "disabled" : "both");

    return att_write_req(fd, cccd_handle, data, 2);
}

/* ── Find CCCD handle for a characteristic ───────────────────────── */
/*
 * The CCCD (UUID=0x2902) is a descriptor immediately after
 * the characteristic value handle. We use ATT_READ_BY_TYPE to find it.
 */
static uint16_t find_cccd(int fd, uint16_t char_value_handle,
                            uint16_t svc_end)
{
    /* Search in range [char_value_handle+1 .. svc_end] */
    uint16_t start = char_value_handle + 1;
    if (start > svc_end) return 0;

    uint8_t req[7];
    req[0] = 0x08;  /* ATT_READ_BY_TYPE_REQ */
    req[1] = start & 0xFF; req[2] = (start >> 8) & 0xFF;
    req[3] = svc_end & 0xFF; req[4] = (svc_end >> 8) & 0xFF;
    req[5] = UUID_CCCD & 0xFF;
    req[6] = (UUID_CCCD >> 8) & 0xFF;

    send(fd, req, 7, 0);

    uint8_t rsp[64];
    int r = recv(fd, rsp, sizeof(rsp), 0);
    if (r < 4 || rsp[0] != 0x09) return 0;  /* no CCCD found */

    /* First item: handle(2) + value */
    uint16_t cccd_handle = rsp[2] | (rsp[3] << 8);
    return cccd_handle;
}

/* ── Receive one ATT notification ─────────────────────────────────── */
/*
 * Notification packet format:
 * [ 0x1B | handle_lo | handle_hi | value... ]
 *
 * Indication format:
 * [ 0x1D | handle_lo | handle_hi | value... ]
 * (must reply with ATT_HANDLE_VALUE_CONFIRM 0x1E)
 */
static int recv_notification(int fd, uint16_t *handle_out,
                              uint8_t *data, int bufsz)
{
    uint8_t buf[256];
    int r = recv(fd, buf, sizeof(buf), 0);
    if (r < 3) return -1;

    if (buf[0] == ATT_OP_NOTIFY || buf[0] == ATT_OP_INDICATE) {
        *handle_out = buf[1] | (buf[2] << 8);
        int dlen = r - 3;
        int copy = dlen < bufsz ? dlen : bufsz;
        memcpy(data, &buf[3], copy);

        /* Must confirm indications */
        if (buf[0] == ATT_OP_INDICATE) {
            uint8_t confirm = ATT_OP_CONFIRM;
            send(fd, &confirm, 1, 0);
        }
        return copy;
    }
    return 0;   /* not a notification */
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
    printf("║  BLE Step 07 — Write + Notifications     ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    if (argc < 5) {
        printf("Usage: sudo ./ble_write <ADDR> <type> <handle> <hex_bytes...>\n");
        printf("  type:   0=public 1=random\n");
        printf("  handle: e.g. 0x0012 or 18\n\n");
        printf("Examples:\n");
        printf("  Write 0x01 to handle 0x0012:\n");
        printf("    sudo ./ble_write AA:BB:CC:DD:EE:FF 1 0x0012 01\n\n");
        printf("  Enable notifications on CCCD at 0x0013:\n");
        printf("    sudo ./ble_write AA:BB:CC:DD:EE:FF 1 0x0013 01 00\n\n");
        printf("  Disable notifications:\n");
        printf("    sudo ./ble_write AA:BB:CC:DD:EE:FF 1 0x0013 00 00\n\n");
        printf("Then use Step 08 (ble_notify) to receive notifications.\n");
        return 1;
    }

    uint8_t addr_type = atoi(argv[2]);
    uint16_t handle   = (uint16_t)strtol(argv[3], NULL, 0);

    /* Parse hex bytes from args */
    uint8_t data[128];
    int data_len = 0;
    for (int i = 4; i < argc && data_len < 128; i++)
        data[data_len++] = (uint8_t)strtol(argv[i], NULL, 16);

    /* Get local address */
    int dev_id = hci_get_route(NULL);
    int hci_fd = hci_open_dev(dev_id);
    bdaddr_t src;
    { struct hci_dev_info di; di.dev_id=dev_id;
      ioctl(hci_fd,HCIGETDEVINFO,&di); bacpy(&src,&di.bdaddr); }
    close(hci_fd);

    printf("[*] Connecting to %s...\n", argv[1]);
    int fd = open_att(argv[1], addr_type, &src);
    if (fd < 0) return 1;
    printf("[✓] Connected.\n\n");

    printf("── Write Operation ──────────────────────────────────────────\n");
    printf("  Handle : 0x%04X\n", handle);
    printf("  Data   : ");
    for (int i = 0; i < data_len; i++) printf("%02X ", data[i]);
    printf("(%d bytes)\n\n", data_len);

    /* Perform write with response */
    int r = att_write_req(fd, handle, data, data_len);
    if (r == 0) {
        printf("\n[✓] Write successful!\n");

        /* If writing 01 00 to CCCD, listen for notifications */
        if (data_len == 2 && data[0] == 0x01 && data[1] == 0x00) {
            printf("\n── Listening for Notifications (Ctrl+C to stop) ────────────\n");
            signal(SIGINT, sig_handler);
            int count = 0;
            while (g_running) {
                uint16_t notif_handle;
                uint8_t  notif_data[256];
                int      notif_len;

                notif_len = recv_notification(fd, &notif_handle,
                                               notif_data, sizeof(notif_data));
                if (notif_len > 0) {
                    count++;
                    printf("  [#%d] Notification handle=0x%04X data=[", count, notif_handle);
                    for (int i = 0; i < notif_len && i < 16; i++)
                        printf("%02X ", notif_data[i]);
                    if (notif_len > 16) printf("...");
                    printf("] (%d bytes)\n", notif_len);
                }
            }
            printf("\n[✓] Received %d notifications.\n", count);
        }
    } else {
        printf("\n[!] Write failed.\n");
    }

    printf("[*] Next: Step 08 — Subscribe to Notifications\n");
    close(fd);
    return 0;
}
