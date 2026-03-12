/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  STEP 06 — Read Characteristic Values                       ║
 * ║                                                              ║
 * ║  What you learn:                                             ║
 * ║   • ATT_READ_REQ / ATT_READ_RSP protocol                     ║
 * ║   • ATT_READ_BLOB_REQ for values longer than MTU             ║
 * ║   • Decoding standard GATT characteristic formats            ║
 * ║   • Battery Level, Device Name, Temperature, Heart Rate      ║
 * ║   • DataView-equivalent byte parsing in C                    ║
 * ║                                                              ║
 * ║  Build:  gcc ble_read.c -o ble_read -lbluetooth              ║
 * ║  Run:    sudo ./ble_read AA:BB:CC:DD:EE:FF [0|1]             ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  ATT Read Protocol:
 *
 *  Client (us)                   Server (peripheral)
 *  ──────────                    ───────────────────
 *  ATT_READ_REQ [0x0A][handle(2)] ──────────────────→
 *                                ←── ATT_READ_RSP [0x0B][value...]
 *
 *  ATT Read Blob (for long values > MTU-1 bytes):
 *  ATT_READ_BLOB_REQ [0x0C][handle(2)][offset(2)] ──→
 *                                ←── ATT_READ_BLOB_RSP [0x0D][value...]
 *  (repeat with increasing offset until short response)
 *
 *  ATT Error Response:
 *  ←── [0x01][req_opcode][handle(2)][error_code]
 *  Error codes:
 *    0x01 = Invalid Handle
 *    0x02 = Read Not Permitted
 *    0x05 = Insufficient Authentication
 *    0x08 = Insufficient Authorization
 *    0x0A = Attribute Not Found
 *    0x0F = Insufficient Encryption Key Size
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "../ble_compat.h"
#include <bluetooth/l2cap.h>

/* ── ATT opcodes ─────────────────────────────────────────────────── */
#define ATT_OP_ERROR            0x01
#define ATT_OP_MTU_REQ          0x02
#define ATT_OP_MTU_RSP          0x03
#define ATT_OP_READ_BY_TYPE_REQ 0x08
#define ATT_OP_READ_BY_TYPE_RSP 0x09
#define ATT_OP_READ_REQ         0x0A
#define ATT_OP_READ_RSP         0x0B
#define ATT_OP_READ_BLOB_REQ    0x0C
#define ATT_OP_READ_BLOB_RSP    0x0D
#define ATT_OP_READ_BY_GROUP_REQ 0x10
#define ATT_OP_READ_BY_GROUP_RSP 0x11

/* ── Standard GATT UUIDs ─────────────────────────────────────────── */
#define UUID_PRIMARY_SERVICE    0x2800
#define UUID_CHARACTERISTIC     0x2803
#define UUID_DEVICE_NAME        0x2A00
#define UUID_APPEARANCE         0x2A01
#define UUID_BATTERY_LEVEL      0x2A19
#define UUID_MANUFACTURER_NAME  0x2A29
#define UUID_MODEL_NUMBER       0x2A24
#define UUID_SERIAL_NUMBER      0x2A25
#define UUID_FW_REVISION        0x2A26
#define UUID_HW_REVISION        0x2A27
#define UUID_TEMPERATURE        0x2A6E
#define UUID_HUMIDITY           0x2A6F
#define UUID_HR_MEASUREMENT     0x2A37
#define UUID_BODY_LOCATION      0x2A38

/* ── Read result buffer ──────────────────────────────────────────── */
typedef struct {
    uint8_t  data[512];
    int      len;
    uint16_t handle;
    int      ok;
} att_read_result_t;

/* ── Low-level ATT_READ_REQ ──────────────────────────────────────── */
static int att_read(int fd, uint16_t handle, att_read_result_t *out)
{
    uint8_t req[3];
    req[0] = ATT_OP_READ_REQ;
    req[1] = handle & 0xFF;
    req[2] = (handle >> 8) & 0xFF;

    if (send(fd, req, 3, 0) < 0) { perror("send"); return -1; }

    uint8_t rsp[512];
    int r = recv(fd, rsp, sizeof(rsp), 0);
    if (r < 1) return -1;

    out->handle = handle;

    if (rsp[0] == ATT_OP_ERROR) {
        out->ok = 0;
        /* Decode error */
        const char *errs[] = { "",
            "Invalid Handle",        /* 0x01 */
            "Read Not Permitted",    /* 0x02 */
            "Write Not Permitted",   /* 0x03 */
            "Invalid PDU",           /* 0x04 */
            "Insufficient Auth",     /* 0x05 */
            "Request Not Supported", /* 0x06 */
            "Invalid Offset",        /* 0x07 */
            "Insufficient Auth2",    /* 0x08 */
            "Attribute Too Long",    /* 0x09 */
            "Attribute Not Found",   /* 0x0A */
        };
        uint8_t code = (r >= 5) ? rsp[4] : 0;
        printf("    [!] ATT Error 0x%02X: %s\n", code,
               code <= 0x0A ? errs[code] : "Other");
        return -1;
    }

    if (rsp[0] != ATT_OP_READ_RSP) return -1;

    out->len = r - 1;
    memcpy(out->data, &rsp[1], out->len);
    out->ok = 1;

    /* If full MTU returned, there may be more — use READ_BLOB */
    /* (simplified: assume MTU=23 for this demo) */
    return out->len;
}

/* ── Read Long Characteristic (using READ_BLOB for continuation) ─── */
static int att_read_long(int fd, uint16_t handle, uint8_t *buf, int bufsz)
{
    int total = 0;
    uint16_t offset = 0;

    /* First read */
    {
        att_read_result_t r;
        if (att_read(fd, handle, &r) < 0) return -1;
        int copy = r.len < bufsz ? r.len : bufsz;
        memcpy(buf, r.data, copy);
        total += copy;
        if (r.len < 22) return total;  /* less than MTU-1 = done */
        offset = copy;
    }

    /* Continue with READ_BLOB for remaining data */
    while (total < bufsz) {
        uint8_t req[5];
        req[0] = ATT_OP_READ_BLOB_REQ;
        req[1] = handle & 0xFF;
        req[2] = (handle >> 8) & 0xFF;
        req[3] = offset & 0xFF;
        req[4] = (offset >> 8) & 0xFF;
        send(fd, req, 5, 0);

        uint8_t rsp[256];
        int r = recv(fd, rsp, sizeof(rsp), 0);
        if (r < 2 || rsp[0] == ATT_OP_ERROR) break;
        if (rsp[0] != ATT_OP_READ_BLOB_RSP) break;

        int chunk = r - 1;
        int copy  = chunk < (bufsz - total) ? chunk : (bufsz - total);
        memcpy(buf + total, &rsp[1], copy);
        total  += copy;
        offset += copy;
        if (chunk < 22) break;  /* short response = done */
    }
    return total;
}

/* ── Find characteristic handle by UUID (using READ_BY_TYPE) ─────── */
static uint16_t find_char_handle(int fd, uint16_t svc_start,
                                  uint16_t svc_end, uint16_t char_uuid)
{
    uint8_t req[7];
    req[0] = ATT_OP_READ_BY_TYPE_REQ;
    req[1] = svc_start & 0xFF; req[2] = (svc_start >> 8) & 0xFF;
    req[3] = svc_end   & 0xFF; req[4] = (svc_end   >> 8) & 0xFF;
    req[5] = UUID_CHARACTERISTIC & 0xFF;
    req[6] = (UUID_CHARACTERISTIC >> 8) & 0xFF;
    send(fd, req, 7, 0);

    uint8_t rsp[256];
    int r = recv(fd, rsp, sizeof(rsp), 0);
    if (r < 4 || rsp[0] != ATT_OP_READ_BY_TYPE_RSP) return 0;

    uint8_t item_len = rsp[1];
    int pos = 2;
    while (pos + item_len <= r) {
        /* properties=rsp[pos+2], val_handle=rsp[pos+3..4], uuid=rsp[pos+5..] */
        uint16_t uuid = rsp[pos+5] | (rsp[pos+6] << 8);
        if (uuid == char_uuid) {
            return rsp[pos+3] | (rsp[pos+4] << 8);  /* value handle */
        }
        pos += item_len;
    }
    return 0;
}

/* ── Decode and print known characteristic values ─────────────────── */
static void decode_and_print(uint16_t uuid16, const uint8_t *data, int len)
{
    switch (uuid16) {

    case UUID_DEVICE_NAME:
    case UUID_MANUFACTURER_NAME:
    case UUID_MODEL_NUMBER:
    case UUID_SERIAL_NUMBER:
    case UUID_FW_REVISION:
    case UUID_HW_REVISION:
        /* UTF-8 string */
        printf("    String: \"");
        for (int i = 0; i < len; i++)
            putchar(data[i] >= 0x20 && data[i] < 0x7F ? data[i] : '?');
        printf("\"\n");
        break;

    case UUID_BATTERY_LEVEL:
        /* uint8: 0–100% */
        if (len >= 1)
            printf("    Battery: %u%%\n", data[0]);
        break;

    case UUID_APPEARANCE:
        /* uint16 little-endian: appearance category */
        if (len >= 2) {
            uint16_t v = data[0] | (data[1] << 8);
            printf("    Appearance: 0x%04X", v);
            /* Common values */
            switch (v) {
            case 0x0000: printf(" (Unknown)"); break;
            case 0x0040: printf(" (Phone)"); break;
            case 0x0080: printf(" (Computer)"); break;
            case 0x0180: printf(" (Heart Rate Sensor)"); break;
            case 0x0200: printf(" (Thermometer)"); break;
            case 0x0300: printf(" (Glucose Meter)"); break;
            case 0x03C0: printf(" (Cycling Sensor)"); break;
            }
            printf("\n");
        }
        break;

    case UUID_TEMPERATURE:
        /* int16 little-endian × 0.01 = degrees Celsius */
        if (len >= 2) {
            int16_t raw = (int16_t)(data[0] | (data[1] << 8));
            printf("    Temperature: %.2f °C\n", raw / 100.0);
        }
        break;

    case UUID_HUMIDITY:
        /* uint16 little-endian × 0.01 = %RH */
        if (len >= 2) {
            uint16_t raw = data[0] | (data[1] << 8);
            printf("    Humidity: %.2f %%RH\n", raw / 100.0);
        }
        break;

    case UUID_HR_MEASUREMENT:
        /*
         * Byte 0: flags
         *   bit 0: 0=uint8 HR, 1=uint16 HR
         *   bit 1-2: sensor contact
         *   bit 3: energy expended present
         *   bit 4: RR interval present
         * Byte 1 (or 1-2): heart rate value
         */
        if (len >= 2) {
            uint8_t flags = data[0];
            uint16_t bpm;
            int      pos = 1;
            if (flags & 0x01) {
                bpm = data[1] | (data[2] << 8); pos = 3;
            } else {
                bpm = data[1]; pos = 2;
            }
            printf("    Heart Rate: %u bpm\n", bpm);
            printf("    Sensor Contact: %s\n",
                   (flags >> 1) & 0x03 == 2 ? "detected" : "not detected");
        }
        break;

    default:
        /* Raw hex dump */
        printf("    Raw (%d bytes): ", len);
        for (int i = 0; i < len && i < 20; i++)
            printf("%02X ", data[i]);
        if (len > 20) printf("...");
        printf("\n");
        /* Also try as string if printable */
        int printable = 1;
        for (int i = 0; i < len; i++)
            if (data[i] < 0x20 || data[i] > 0x7E) { printable=0; break; }
        if (printable && len > 0) {
            printf("    As string: \"");
            for (int i = 0; i < len; i++) putchar(data[i]);
            printf("\"\n");
        }
        break;
    }
}

/* ── Known characteristics to read ──────────────────────────────── */
typedef struct {
    uint16_t uuid;
    const char *name;
} char_def_t;

static const char_def_t KNOWN_CHARS[] = {
    { UUID_DEVICE_NAME,       "Device Name" },
    { UUID_APPEARANCE,        "Appearance" },
    { UUID_BATTERY_LEVEL,     "Battery Level" },
    { UUID_MANUFACTURER_NAME, "Manufacturer Name" },
    { UUID_MODEL_NUMBER,      "Model Number" },
    { UUID_SERIAL_NUMBER,     "Serial Number" },
    { UUID_FW_REVISION,       "Firmware Revision" },
    { UUID_HW_REVISION,       "Hardware Revision" },
    { UUID_TEMPERATURE,       "Temperature" },
    { UUID_HUMIDITY,          "Humidity" },
    { 0, NULL }
};

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
    printf("║  BLE Step 06 — Read Characteristics      ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    if (argc < 2) {
        printf("Usage: sudo ./ble_read <BT_ADDR> [0=public|1=random]\n");
        return 1;
    }

    uint8_t addr_type = argc >= 3 ? atoi(argv[2]) : BDADDR_LE_PUBLIC;

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

    /* ── Read all known characteristics by scanning handle range ── */
    printf("── Reading Standard GATT Characteristics ────────────────────\n\n");

    /* Find handles by reading characteristic declarations 0x0001–0xFFFF */
    uint8_t req[7];
    uint16_t start = 0x0001;

    while (start <= 0xFFFF) {
        req[0] = ATT_OP_READ_BY_TYPE_REQ;
        req[1] = start & 0xFF; req[2] = (start >> 8) & 0xFF;
        req[3] = 0xFF;         req[4] = 0xFF;
        req[5] = UUID_CHARACTERISTIC & 0xFF;
        req[6] = (UUID_CHARACTERISTIC >> 8) & 0xFF;
        send(fd, req, 7, 0);

        uint8_t rsp[256];
        int r = recv(fd, rsp, sizeof(rsp), 0);
        if (r < 4 || rsp[0] == ATT_OP_ERROR) break;
        if (rsp[0] != ATT_OP_READ_BY_TYPE_RSP) break;

        uint8_t item_len = rsp[1];
        int pos = 2;
        int uuid_len = item_len - 5;

        while (pos + item_len <= r) {
            uint16_t decl_handle  = rsp[pos]   | (rsp[pos+1] << 8);
            uint8_t  props        = rsp[pos+2];
            uint16_t value_handle = rsp[pos+3] | (rsp[pos+4] << 8);

            /* Only read if has READ property */
            if (props & 0x02) {
                uint16_t uuid16 = 0;
                if (uuid_len == 2)
                    uuid16 = rsp[pos+5] | (rsp[pos+6] << 8);

                /* Find name */
                const char *name = "Unknown";
                for (int k = 0; KNOWN_CHARS[k].uuid; k++)
                    if (KNOWN_CHARS[k].uuid == uuid16)
                        { name = KNOWN_CHARS[k].name; break; }

                printf("  Char [%04X] UUID=0x%04X  %s\n",
                       value_handle, uuid16, name);

                att_read_result_t result;
                if (att_read(fd, value_handle, &result) >= 0) {
                    decode_and_print(uuid16, result.data, result.len);
                }
                printf("\n");
            }

            start = value_handle + 1;
            pos  += item_len;
        }
    }

    printf("[✓] Read complete.\n");
    printf("[*] Next: Step 07 — Write Characteristics\n");

    close(fd);
    return 0;
}
