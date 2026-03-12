/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  STEP 05 — GATT Primary Service Discovery                   ║
 * ║                                                              ║
 * ║  What you learn:                                             ║
 * ║   • ATT Read By Group Type Request (discovers services)      ║
 * ║   • ATT Read By Type Request (discovers characteristics)     ║
 * ║   • ATT Find Information Request (discovers descriptors)     ║
 * ║   • How handles, UUIDs, and attribute types relate           ║
 * ║   • Full GATT attribute tree: Service→Char→Descriptor        ║
 * ║                                                              ║
 * ║  Build:  gcc ble_discover.c -o ble_discover -lbluetooth      ║
 * ║  Run:    sudo ./ble_discover AA:BB:CC:DD:EE:FF [0|1]         ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  GATT Attribute Table (on the peripheral):
 *
 *  Handle  │ Type UUID │ Value
 *  ────────┼───────────┼─────────────────────────────────────
 *  0x0001  │ 0x2800    │ 0x1800  ← Primary Service: Generic Access
 *  0x0002  │ 0x2803    │ props+handle+0x2A00 ← Characteristic decl
 *  0x0003  │ 0x2A00    │ "MySensor" ← Device Name value
 *  0x0004  │ 0x2803    │ props+handle+0x2A01 ← Characteristic decl
 *  0x0005  │ 0x2A01    │ 0x0000 ← Appearance value
 *  0x0010  │ 0x2800    │ 0x180F  ← Primary Service: Battery
 *  0x0011  │ 0x2803    │ props+handle+0x2A19 ← Characteristic decl
 *  0x0012  │ 0x2A19    │ 85 ← Battery Level (85%)
 *  0x0013  │ 0x2902    │ 0x0000 ← CCCD descriptor
 *  ────────┼───────────┼─────────────────────────────────────
 *
 *  ATT_READ_BY_GROUP_TYPE:
 *  Request:  [0x10][start_handle(2)][end_handle(2)][group_type_uuid(2)]
 *  Response: [0x11][length(1)][{handle(2),end_handle(2),uuid(2|16)}...]
 *
 *  ATT_READ_BY_TYPE (for characteristics):
 *  Request:  [0x08][start(2)][end(2)][0x03,0x28] ← UUID 0x2803
 *  Response: [0x09][length(1)][{handle(2),value}...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "../ble_compat.h"
#include <bluetooth/l2cap.h>

/* ── ATT opcodes ─────────────────────────────────────────────────── */
#define ATT_OP_ERROR               0x01
#define ATT_OP_MTU_REQ             0x02
#define ATT_OP_MTU_RSP             0x03
#define ATT_OP_FIND_INFO_REQ       0x04
#define ATT_OP_FIND_INFO_RSP       0x05
#define ATT_OP_READ_BY_TYPE_REQ    0x08
#define ATT_OP_READ_BY_TYPE_RSP    0x09
#define ATT_OP_READ_REQ            0x0A
#define ATT_OP_READ_RSP            0x0B
#define ATT_OP_READ_BY_GROUP_REQ   0x10
#define ATT_OP_READ_BY_GROUP_RSP   0x11
#define ATT_OP_WRITE_REQ           0x12
#define ATT_OP_WRITE_RSP           0x13
#define ATT_OP_NOTIFY              0x1B
#define ATT_OP_INDICATE            0x1D
#define ATT_OP_CONFIRM             0x1E
#define ATT_OP_WRITE_CMD           0x52

/* ── UUID constants ──────────────────────────────────────────────── */
#define UUID_PRIMARY_SERVICE    0x2800
#define UUID_SECONDARY_SERVICE  0x2801
#define UUID_CHARACTERISTIC     0x2803
#define UUID_CCCD               0x2902
#define UUID_USER_DESC          0x2901

/* ── Characteristic property bits ───────────────────────────────── */
#define PROP_BROADCAST          0x01
#define PROP_READ               0x02
#define PROP_WRITE_NO_RSP       0x04
#define PROP_WRITE              0x08
#define PROP_NOTIFY             0x10
#define PROP_INDICATE           0x20
#define PROP_AUTH_WRITE         0x40
#define PROP_EXT_PROPS          0x80

/* ── Data structures ─────────────────────────────────────────────── */
typedef struct {
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t  uuid[16];
    int      uuid_len;         /* 2 or 16 */
} gatt_service_t;

typedef struct {
    uint16_t decl_handle;      /* handle of characteristic declaration */
    uint16_t value_handle;     /* handle of characteristic value       */
    uint8_t  properties;       /* bitmask of PROP_* flags              */
    uint8_t  uuid[16];
    int      uuid_len;
} gatt_char_t;

typedef struct {
    uint16_t handle;
    uint16_t uuid16;           /* descriptor UUID (always 16-bit)      */
} gatt_desc_t;

#define MAX_SERVICES   32
#define MAX_CHARS      64
#define MAX_DESCS      32

static gatt_service_t g_services[MAX_SERVICES];
static int            g_svc_count = 0;
static gatt_char_t    g_chars[MAX_CHARS];
static int            g_char_count = 0;

/* ── ATT send/recv helpers ───────────────────────────────────────── */
static int att_send(int fd, const uint8_t *buf, int len)
{
    int r = send(fd, buf, len, 0);
    if (r < 0) perror("[!] ATT send");
    return r;
}

static int att_recv(int fd, uint8_t *buf, int bufsz)
{
    int r = recv(fd, buf, bufsz, 0);
    if (r < 0) perror("[!] ATT recv");
    return r;
}

/* ── UUID printing ───────────────────────────────────────────────── */
static void print_uuid(const uint8_t *uuid, int len)
{
    if (len == 2) {
        uint16_t u = uuid[0] | (uuid[1] << 8);
        printf("0x%04X", u);
    } else {
        /* 128-bit UUID: print in 8-4-4-4-12 format */
        printf("%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
               uuid[15],uuid[14],uuid[13],uuid[12],
               uuid[11],uuid[10],uuid[9],uuid[8],
               uuid[7],uuid[6],uuid[5],uuid[4],
               uuid[3],uuid[2],uuid[1],uuid[0]);
    }
}

/* ── Service UUID to name ────────────────────────────────────────── */
static const char *service_name(uint16_t uuid)
{
    switch (uuid) {
    case 0x1800: return "Generic Access";
    case 0x1801: return "Generic Attribute";
    case 0x180A: return "Device Information";
    case 0x180D: return "Heart Rate";
    case 0x180F: return "Battery";
    case 0x1810: return "Blood Pressure";
    case 0x1812: return "HID";
    case 0x1816: return "Cycling Speed";
    case 0x181A: return "Environmental Sensing";
    case 0x181C: return "User Data";
    default:     return "Unknown Service";
    }
}

/* ── Characteristic UUID to name ────────────────────────────────── */
static const char *char_name(uint16_t uuid)
{
    switch (uuid) {
    case 0x2A00: return "Device Name";
    case 0x2A01: return "Appearance";
    case 0x2A05: return "Service Changed";
    case 0x2A19: return "Battery Level";
    case 0x2A24: return "Model Number";
    case 0x2A25: return "Serial Number";
    case 0x2A26: return "Firmware Revision";
    case 0x2A27: return "Hardware Revision";
    case 0x2A28: return "Software Revision";
    case 0x2A29: return "Manufacturer Name";
    case 0x2A37: return "Heart Rate Measurement";
    case 0x2A38: return "Body Sensor Location";
    case 0x2A39: return "Heart Rate Control Point";
    case 0x2A6E: return "Temperature";
    case 0x2A6F: return "Humidity";
    default:     return "Unknown";
    }
}

/* ── Properties string ───────────────────────────────────────────── */
static void print_properties(uint8_t props)
{
    if (props & PROP_READ)          printf("READ ");
    if (props & PROP_WRITE)         printf("WRITE ");
    if (props & PROP_WRITE_NO_RSP)  printf("WRITE_NO_RSP ");
    if (props & PROP_NOTIFY)        printf("NOTIFY ");
    if (props & PROP_INDICATE)      printf("INDICATE ");
    if (props & PROP_BROADCAST)     printf("BROADCAST ");
    if (props & PROP_AUTH_WRITE)    printf("AUTH_WRITE ");
}

/* ── Step 1: Discover Primary Services ───────────────────────────── */
/*
 * ATT_READ_BY_GROUP_TYPE iterates through the attribute table.
 * We repeat until we get ATT_ERROR (attribute not found = done).
 */
static int discover_primary_services(int fd)
{
    uint16_t start = 0x0001;
    g_svc_count = 0;

    printf("── Primary Service Discovery ────────────────────────────────\n");

    while (start <= 0xFFFF && g_svc_count < MAX_SERVICES) {
        /* Build ATT_READ_BY_GROUP_TYPE_REQ */
        uint8_t req[7];
        req[0] = ATT_OP_READ_BY_GROUP_REQ;
        req[1] = start & 0xFF;
        req[2] = (start >> 8) & 0xFF;
        req[3] = 0xFF;   /* end_handle = 0xFFFF */
        req[4] = 0xFF;
        req[5] = UUID_PRIMARY_SERVICE & 0xFF;   /* group type = 0x2800 */
        req[6] = (UUID_PRIMARY_SERVICE >> 8) & 0xFF;

        att_send(fd, req, sizeof(req));

        uint8_t rsp[256];
        int r = att_recv(fd, rsp, sizeof(rsp));
        if (r < 2) break;

        if (rsp[0] == ATT_OP_ERROR) {
            /* 0x0A = Attribute Not Found = no more services */
            break;
        }

        if (rsp[0] != ATT_OP_READ_BY_GROUP_RSP) break;

        uint8_t item_len = rsp[1];    /* 6 for 16-bit UUID, 20 for 128-bit */
        int     pos      = 2;
        int     uuid_len = item_len - 4;  /* item = start(2)+end(2)+uuid */

        while (pos + item_len <= r) {
            gatt_service_t *svc = &g_services[g_svc_count++];
            svc->start_handle = rsp[pos] | (rsp[pos+1] << 8);
            svc->end_handle   = rsp[pos+2] | (rsp[pos+3] << 8);
            svc->uuid_len     = uuid_len;
            memcpy(svc->uuid, &rsp[pos+4], uuid_len);

            printf("  [%04X–%04X] Service: ", svc->start_handle, svc->end_handle);
            print_uuid(svc->uuid, svc->uuid_len);
            if (uuid_len == 2) {
                uint16_t u = svc->uuid[0] | (svc->uuid[1] << 8);
                printf("  (%s)", service_name(u));
            }
            printf("\n");

            start = svc->end_handle + 1;
            pos += item_len;
        }
    }

    printf("  → Found %d services\n\n", g_svc_count);
    return g_svc_count;
}

/* ── Step 2: Discover Characteristics within a service ───────────── */
/*
 * ATT_READ_BY_TYPE_REQ with UUID=0x2803 finds all characteristic
 * declarations in a handle range. Each declaration contains:
 *   properties (1 byte) + value_handle (2 bytes) + char UUID (2 or 16 bytes)
 */
static int discover_characteristics(int fd, uint16_t svc_start,
                                     uint16_t svc_end, int svc_idx)
{
    uint16_t start = svc_start;
    int count = 0;

    while (start < svc_end && g_char_count < MAX_CHARS) {
        uint8_t req[7];
        req[0] = ATT_OP_READ_BY_TYPE_REQ;
        req[1] = start & 0xFF;
        req[2] = (start >> 8) & 0xFF;
        req[3] = svc_end & 0xFF;
        req[4] = (svc_end >> 8) & 0xFF;
        req[5] = UUID_CHARACTERISTIC & 0xFF;   /* 0x2803 */
        req[6] = (UUID_CHARACTERISTIC >> 8) & 0xFF;

        att_send(fd, req, sizeof(req));

        uint8_t rsp[256];
        int r = att_recv(fd, rsp, sizeof(rsp));
        if (r < 2 || rsp[0] == ATT_OP_ERROR) break;
        if (rsp[0] != ATT_OP_READ_BY_TYPE_RSP) break;

        uint8_t item_len = rsp[1];   /* 7 for 16-bit UUID char, 21 for 128-bit */
        int     pos      = 2;
        int     uuid_len = item_len - 5;  /* decl_handle(2)+props(1)+val_handle(2)+uuid */

        while (pos + item_len <= r) {
            gatt_char_t *ch = &g_chars[g_char_count++];
            ch->decl_handle  = rsp[pos]   | (rsp[pos+1] << 8);
            ch->properties   = rsp[pos+2];
            ch->value_handle = rsp[pos+3] | (rsp[pos+4] << 8);
            ch->uuid_len     = uuid_len;
            memcpy(ch->uuid, &rsp[pos+5], uuid_len);

            printf("    ├─ Char [decl=%04X value=%04X] props=[",
                   ch->decl_handle, ch->value_handle);
            print_properties(ch->properties);
            printf("]\n");
            printf("    │      UUID: ");
            print_uuid(ch->uuid, ch->uuid_len);
            if (uuid_len == 2) {
                uint16_t u = ch->uuid[0] | (ch->uuid[1] << 8);
                printf("  (%s)", char_name(u));
            }
            printf("\n");

            start = ch->value_handle + 1;
            pos  += item_len;
            count++;
        }
    }
    (void)svc_idx;
    return count;
}

/* ── Step 3: Discover Descriptors in a characteristic range ──────── */
/*
 * ATT_FIND_INFORMATION_REQ returns all attribute handle+UUID pairs.
 * We skip the value handle itself (it's the characteristic value).
 * Everything else in the range is a descriptor.
 */
static void discover_descriptors(int fd, uint16_t char_val_handle,
                                  uint16_t end_handle)
{
    if (char_val_handle + 1 > end_handle) return;

    uint8_t req[5];
    req[0] = ATT_OP_FIND_INFO_REQ;
    req[1] = (char_val_handle + 1) & 0xFF;
    req[2] = ((char_val_handle + 1) >> 8) & 0xFF;
    req[3] = end_handle & 0xFF;
    req[4] = (end_handle >> 8) & 0xFF;

    att_send(fd, req, sizeof(req));

    uint8_t rsp[256];
    int r = att_recv(fd, rsp, sizeof(rsp));
    if (r < 2 || rsp[0] == ATT_OP_ERROR) return;
    if (rsp[0] != ATT_OP_FIND_INFO_RSP) return;

    uint8_t fmt = rsp[1];   /* 1 = 16-bit UUIDs, 2 = 128-bit UUIDs */
    int item_len = (fmt == 1) ? 4 : 18;
    int pos = 2;

    while (pos + item_len <= r) {
        uint16_t dhandle = rsp[pos] | (rsp[pos+1] << 8);
        uint16_t duuid   = 0;

        if (fmt == 1) {
            duuid = rsp[pos+2] | (rsp[pos+3] << 8);
            printf("    │    └─ Descriptor [%04X] UUID: 0x%04X", dhandle, duuid);
            switch (duuid) {
            case 0x2902: printf("  (CCCD — enables notifications)"); break;
            case 0x2901: printf("  (User Description)"); break;
            case 0x2904: printf("  (Presentation Format)"); break;
            case 0x2900: printf("  (Extended Properties)"); break;
            }
            printf("\n");
        }
        pos += item_len;
    }
}

/* ── Open ATT socket (same as step04) ───────────────────────────── */
static int open_att_socket(const bdaddr_t *dst, uint8_t dst_type,
                            const bdaddr_t *src)
{
    struct sockaddr_l2 sa = {0};
    int fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (fd < 0) { perror("socket"); return -1; }

    sa.l2_family = AF_BLUETOOTH;
    sa.l2_bdaddr = *src;
    sa.l2_cid    = htobs(4);
    sa.l2_bdaddr_type = BDADDR_LE_PUBLIC;
    bind(fd, (struct sockaddr *)&sa, sizeof(sa));

    sa.l2_bdaddr      = *dst;
    sa.l2_bdaddr_type = dst_type;
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("[!] connect"); close(fd); return -1;
    }
    return fd;
}

/* ── Main ────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  BLE Step 05 — GATT Service Discovery    ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    if (argc < 2) {
        printf("Usage: sudo ./ble_discover <BT_ADDR> [0=public|1=random]\n");
        return 1;
    }

    bdaddr_t dst, src;
    str2ba(argv[1], &dst);
    uint8_t dst_type = argc >= 3 ? atoi(argv[2]) : BDADDR_LE_PUBLIC;

    /* Get local address */
    int dev_id = hci_get_route(NULL);
    int hci_fd = hci_open_dev(dev_id);
    { struct hci_dev_info di; di.dev_id=dev_id;
      ioctl(hci_fd,HCIGETDEVINFO,&di); bacpy(&src,&di.bdaddr); }
    close(hci_fd);

    printf("[*] Connecting to %s...\n", argv[1]);
    int fd = open_att_socket(&dst, dst_type, &src);
    if (fd < 0) return 1;
    printf("[✓] Connected. Starting GATT discovery...\n\n");

    /* ── Full Discovery ──────────────────────────────────────────── */
    discover_primary_services(fd);

    printf("── Characteristics & Descriptors ────────────────────────────\n");
    for (int i = 0; i < g_svc_count; i++) {
        gatt_service_t *svc = &g_services[i];
        printf("\nService [%04X–%04X] ", svc->start_handle, svc->end_handle);
        print_uuid(svc->uuid, svc->uuid_len);
        if (svc->uuid_len == 2) {
            uint16_t u = svc->uuid[0] | (svc->uuid[1] << 8);
            printf("  (%s)", service_name(u));
        }
        printf("\n");

        int char_start = g_char_count;
        discover_characteristics(fd, svc->start_handle,
                                  svc->end_handle, i);
        int char_end = g_char_count;

        /* Discover descriptors for each characteristic */
        for (int j = char_start; j < char_end; j++) {
            uint16_t next_end = (j + 1 < char_end)
                                ? g_chars[j+1].decl_handle - 1
                                : svc->end_handle;
            discover_descriptors(fd, g_chars[j].value_handle, next_end);
        }
    }

    printf("\n[✓] Discovery complete.\n");
    printf("    Services: %d  Characteristics: %d\n",
           g_svc_count, g_char_count);
    printf("[*] Next: Step 06 — Read Characteristic Values\n");

    close(fd);
    return 0;
}
