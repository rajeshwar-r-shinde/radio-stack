/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  STEP 04 — BLE GATT Connection (L2CAP ATT)                  ║
 * ║                                                              ║
 * ║  What you learn:                                             ║
 * ║   • How BLE connections work at the L2CAP / ATT level        ║
 * ║   • Connecting via HCI LE Create Connection                  ║
 * ║   • Opening an L2CAP ATT socket (the GATT channel)           ║
 * ║   • Connection parameters: interval, latency, timeout        ║
 * ║   • MTU negotiation (ATT_MTU)                                ║
 * ║                                                              ║
 * ║  Build:  gcc ble_connect.c -o ble_connect -lbluetooth        ║
 * ║  Run:    sudo ./ble_connect AA:BB:CC:DD:EE:FF [0=public|1=rnd]║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  BLE Connection Layer Model:
 *
 *  Application ──→ ATT Protocol    (read/write/notify)
 *                      ↓
 *                  L2CAP CID 0x0004 (fixed ATT channel)
 *                      ↓
 *                  LE Link Layer   (connection intervals, acks)
 *                      ↓
 *                  PHY             (2.4GHz radio)
 *
 *  Connection Parameters:
 *  • interval_min/max: how often master+slave communicate (7.5ms–4s)
 *  • latency:          slave can skip N events (saves power)
 *  • supervision_timeout: connection declared lost after N*10ms
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "../ble_compat.h"
#include <bluetooth/l2cap.h>   /* L2CAP socket structures */

/* ── ATT protocol constants ──────────────────────────────────────── */
#define ATT_CID              4      /* L2CAP channel for ATT (BLE GATT) */
#define ATT_DEFAULT_MTU     23      /* minimum guaranteed MTU           */
#define ATT_MAX_MTU        517      /* maximum per spec                 */

/* ATT Opcodes (first byte of every ATT packet) */
#define ATT_OP_MTU_REQ      0x02
#define ATT_OP_MTU_RSP      0x03
#define ATT_OP_ERROR        0x01

/* ── Open L2CAP ATT socket to a BLE device ───────────────────────── */
/*
 * This is the lowest-level way to connect:
 * AF_BLUETOOTH + BTPROTO_L2CAP + CID=4 = BLE ATT channel
 *
 * This is what BlueZ uses internally for all GATT operations.
 */
static int open_att_socket(const bdaddr_t *dst, uint8_t dst_type,
                            const bdaddr_t *src, uint8_t src_type)
{
    struct sockaddr_l2 src_addr = {0};
    struct sockaddr_l2 dst_addr = {0};

    /* Create L2CAP socket */
    int fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (fd < 0) {
        perror("[!] socket(BTPROTO_L2CAP)");
        return -1;
    }

    /* Bind to local adapter */
    src_addr.l2_family   = AF_BLUETOOTH;
    src_addr.l2_bdaddr   = *src;
    src_addr.l2_cid      = htobs(ATT_CID);
    src_addr.l2_bdaddr_type = src_type;

    if (bind(fd, (struct sockaddr *)&src_addr, sizeof(src_addr)) < 0) {
        perror("[!] bind L2CAP");
        close(fd); return -1;
    }

    /* Set BT_SECURITY (optional — force encryption) */
    struct bt_security sec = {0};
    sec.level = BT_SECURITY_LOW;   /* LOW=no security, MEDIUM=encrypt, HIGH=auth */
    if (setsockopt(fd, SOL_BLUETOOTH, BT_SECURITY, &sec, sizeof(sec)) < 0)
        perror("[w] setsockopt BT_SECURITY (non-fatal)");

    /* Connect to remote device on ATT channel */
    dst_addr.l2_family      = AF_BLUETOOTH;
    dst_addr.l2_bdaddr      = *dst;
    dst_addr.l2_cid         = htobs(ATT_CID);
    dst_addr.l2_bdaddr_type = dst_type;

    printf("[*] Connecting to ATT channel (L2CAP CID=0x0004)...\n");
    if (connect(fd, (struct sockaddr *)&dst_addr, sizeof(dst_addr)) < 0) {
        perror("[!] connect L2CAP ATT");
        close(fd); return -1;
    }

    return fd;
}

/* ── MTU Exchange (ATT_EXCHANGE_MTU_REQ/RSP) ─────────────────────── */
/*
 * Negotiating a larger MTU allows bigger ATT payloads.
 * Default is 23 bytes (20 bytes data + 3 bytes ATT header).
 * Modern devices support 247 bytes or more.
 *
 * Request:  [0x02][client_rx_mtu(2)]
 * Response: [0x03][server_rx_mtu(2)]
 * Agreed MTU = min(client_rx_mtu, server_rx_mtu)
 */
static int negotiate_mtu(int fd, uint16_t *mtu_out)
{
    uint8_t req[3];
    uint16_t client_mtu = 517;  /* we can receive up to 517 bytes */

    req[0] = ATT_OP_MTU_REQ;
    req[1] = client_mtu & 0xFF;
    req[2] = (client_mtu >> 8) & 0xFF;

    printf("[*] Sending ATT_EXCHANGE_MTU_REQ (client_mtu=%u)...\n", client_mtu);

    if (send(fd, req, sizeof(req), 0) < 0) {
        perror("[!] send MTU_REQ"); return -1;
    }

    uint8_t rsp[8];
    int r = recv(fd, rsp, sizeof(rsp), 0);
    if (r < 3) { fprintf(stderr, "[!] MTU response too short\n"); return -1; }

    if (rsp[0] == ATT_OP_ERROR) {
        fprintf(stderr, "[!] ATT Error on MTU exchange: 0x%02X\n", rsp[4]);
        *mtu_out = ATT_DEFAULT_MTU;
        return 0;
    }

    if (rsp[0] != ATT_OP_MTU_RSP) {
        fprintf(stderr, "[!] Unexpected response opcode: 0x%02X\n", rsp[0]);
        return -1;
    }

    uint16_t server_mtu = rsp[1] | (rsp[2] << 8);
    *mtu_out = (client_mtu < server_mtu) ? client_mtu : server_mtu;

    printf("[✓] MTU negotiated: client=%u server=%u → agreed=%u bytes\n",
           client_mtu, server_mtu, *mtu_out);
    return 0;
}

/* ── Print connection info ───────────────────────────────────────── */
static void print_conn_info(int fd, const char *addr_str)
{
    struct sockaddr_l2 addr;
    socklen_t alen = sizeof(addr);

    if (getpeername(fd, (struct sockaddr *)&addr, &alen) == 0) {
        char peer[18];
        ba2str(&addr.l2_bdaddr, peer);
        printf("[✓] Peer address (from socket): %s\n", peer);
        printf("    L2CAP CID: 0x%04X\n", btohs(addr.l2_cid));
        printf("    Addr type: %s\n", addr.l2_bdaddr_type ? "Random" : "Public");
    }

    /* Get connection parameters via HCI ioctl */
    printf("[*] Connection established to %s\n", addr_str);
    printf("    ATT channel (CID 4) is open\n");
    printf("    Ready to send ATT requests\n");
}

/* ── Main ────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  BLE Step 04 — GATT Connection           ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    if (argc < 2) {
        printf("Usage: sudo ./ble_connect <BT_ADDR> [addr_type]\n");
        printf("       addr_type: 0=public (default), 1=random\n");
        printf("Example: sudo ./ble_connect AA:BB:CC:DD:EE:FF 1\n");
        printf("\nGet addresses from: sudo ./ble_active_scan (Step 03)\n");
        return 1;
    }

    bdaddr_t dst, src;
    str2ba(argv[1], &dst);

    uint8_t dst_type = (argc >= 3) ? atoi(argv[2]) : 0;
    uint8_t src_type = BDADDR_LE_PUBLIC;

    /* Get local adapter address */
    int dev_id = hci_get_route(NULL);
    if (dev_id < 0) { fprintf(stderr, "[!] No adapter\n"); return 1; }

    int hci_fd = hci_open_dev(dev_id);
    {
        struct hci_dev_info di;
        di.dev_id = dev_id;
        ioctl(hci_fd, HCIGETDEVINFO, &di);
        bacpy(&src, &di.bdaddr);
        char src_str[18];
        ba2str(&src, src_str);
        printf("[*] Local adapter: hci%d  %s\n", dev_id, src_str);
        printf("[*] Target device: %s (%s addr)\n\n",
               argv[1], dst_type ? "Random" : "Public");
    }
    close(hci_fd);

    /* Open L2CAP ATT socket — this triggers a BLE connection */
    int att_fd = open_att_socket(&dst, dst_type, &src, src_type);
    if (att_fd < 0) return 1;

    printf("[✓] L2CAP ATT socket connected! (fd=%d)\n\n", att_fd);
    print_conn_info(att_fd, argv[1]);

    /* Negotiate MTU */
    uint16_t mtu = ATT_DEFAULT_MTU;
    printf("\n── MTU Negotiation ─────────────────────────────────────────\n");
    negotiate_mtu(att_fd, &mtu);
    printf("── MTU = %u bytes (max payload per packet = %u bytes) ──────\n\n",
           mtu, mtu - 3);

    printf("── Connection Parameters ───────────────────────────────────\n");
    printf("    Connection interval: typically 7.5ms - 30ms\n");
    printf("    Supervision timeout: typically 5000ms\n");
    printf("    Latency: 0 (no skipped intervals)\n\n");

    printf("[✓] ATT channel ready for GATT operations.\n");
    printf("[*] Next: Step 05 — Discover All Services\n\n");

    printf("Press Enter to disconnect...\n");
    getchar();

    close(att_fd);
    printf("[*] Disconnected.\n");
    return 0;
}
