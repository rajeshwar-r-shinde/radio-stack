#ifndef BLE_COMPAT_H
#define BLE_COMPAT_H

#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>

/* LE Scan constants removed from BlueZ 5.x public headers */
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
#ifndef LE_RANDOM_ADDRESS
#define LE_RANDOM_ADDRESS       0x01
#endif
#ifndef BDADDR_LE_PUBLIC
#define BDADDR_LE_PUBLIC        0x01
#endif
#ifndef BDADDR_LE_RANDOM
#define BDADDR_LE_RANDOM        0x02
#endif

/* LE Scan structs removed from BlueZ 5.x */
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

/* OGF/OCF opcodes */
#ifndef OGF_LE_CTL
#define OGF_LE_CTL                        0x08
#endif
#ifndef OCF_LE_SET_SCAN_PARAMETERS
#define OCF_LE_SET_SCAN_PARAMETERS        0x000B
#endif
#ifndef OCF_LE_SET_SCAN_ENABLE
#define OCF_LE_SET_SCAN_ENABLE            0x000C
#endif

/* LE Meta subevent codes */
#ifndef EVT_LE_ADVERTISING_REPORT
#define EVT_LE_ADVERTISING_REPORT         0x02
#endif

/* HCI packet types */
#ifndef HCI_EVENT_PKT
#define HCI_EVENT_PKT   0x04
#endif
#ifndef HCI_MAX_EVENT_SIZE
#define HCI_MAX_EVENT_SIZE 260
#endif
#ifndef SOL_HCI
#define SOL_HCI  0
#endif
#ifndef HCI_FILTER
#define HCI_FILTER 2
#endif

/* BT_SECURITY socket option */
#ifndef BT_SECURITY
#define BT_SECURITY 4
struct bt_security { uint8_t level; uint8_t key_size; };
#define BT_SECURITY_LOW    1
#define BT_SECURITY_MEDIUM 2
#define BT_SECURITY_HIGH   3
#endif

#ifndef SOL_BLUETOOTH
#define SOL_BLUETOOTH 274
#endif

#endif /* BLE_COMPAT_H */
