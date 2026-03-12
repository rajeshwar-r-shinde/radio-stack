# BLE Learning Series — C Code (BlueZ / Linux)

Complete step-by-step C code for BLE on Linux using BlueZ.
Each file builds on the previous and is heavily commented.

---

## Prerequisites

```bash
sudo apt install libbluetooth-dev build-essential

# Verify your BLE adapter is visible
hciconfig -a

# Bring adapter up if needed
sudo hciconfig hci0 up
```

---

## Build Everything

```bash
make install-deps   # install libbluetooth-dev
make all            # builds all steps into ./bin/
```

---

## Steps Overview

```
Step 01 — Hardware Check        Check adapter, list HCI devices, read features
Step 02 — Passive Scan          HCI LE scan, receive raw ADV_IND events
Step 03 — Active Scan + Table   SCAN_REQ/RSP, deduplication, live device table
Step 04 — GATT Connect          L2CAP ATT socket, MTU negotiation
Step 05 — Service Discovery     ATT_READ_BY_GROUP_TYPE, char + descriptor discovery
Step 06 — Read Characteristics  ATT_READ_REQ, decode battery/name/temp/HR
Step 07 — Write Characteristics ATT_WRITE_REQ, ATT_WRITE_CMD, write CCCD
Step 08 — Notifications         Enable NOTIFY/INDICATE, receive loop, auto-decode
```

---

## Running Each Step

### Step 01 — Check Hardware
```bash
sudo ./bin/ble_check
```
Output: adapter name, address, LE support flag, packet stats.

---

### Step 02 — Passive Scan (15 seconds)
```bash
sudo ./bin/ble_scan
```
Output: raw advertising events with AD structure parsing.
```
┌─ Device Found ──────────────────────────────────
│  Address  : AA:BB:CC:DD:EE:FF (Random)
│  Adv Type : ADV_IND (0x00)
│  RSSI     : -67 dBm
│  AD Data  :
│    [0x01] Flags                          0x06 (LE-General-Disc No-BR/EDR )
│    [0x09] Complete Local Name            "MySensor"
│    [0x03] 16-bit UUIDs (complete)        0x180F
└────────────────────────────────────────────────
```

---

### Step 03 — Active Scan (live table, Ctrl+C to stop)
```bash
sudo ./bin/ble_active_scan
```
Output: live updating table with RSSI bar graphs and device deduplication.

---

### Step 04 — Connect to a Device
```bash
# Get address from Step 02/03 output
sudo ./bin/ble_connect AA:BB:CC:DD:EE:FF 1
#                      ^address          ^1=random addr type
```
Output: ATT socket connected, MTU negotiated.

---

### Step 05 — Discover GATT Services
```bash
sudo ./bin/ble_discover AA:BB:CC:DD:EE:FF 1
```
Output: full GATT tree with services, characteristics, and descriptors.
```
Service [0001–0007] 0x1800  (Generic Access)
  ├─ Char [decl=0002 value=0003] props=[READ ]
  │      UUID: 0x2A00  (Device Name)
  ├─ Char [decl=0004 value=0005] props=[READ ]
  │      UUID: 0x2A01  (Appearance)

Service [0010–0016] 0x180F  (Battery)
  ├─ Char [decl=0011 value=0012] props=[READ NOTIFY ]
  │      UUID: 0x2A19  (Battery Level)
  │    └─ Descriptor [0013] UUID: 0x2902  (CCCD — enables notifications)
```

---

### Step 06 — Read All Characteristics
```bash
sudo ./bin/ble_read AA:BB:CC:DD:EE:FF 1
```
Output: reads every readable characteristic and decodes it.
```
Char [0003] UUID=0x2A00  Device Name
    String: "MySensor"

Char [0012] UUID=0x2A19  Battery Level
    Battery: 87%

Char [0020] UUID=0x2A6E  Temperature
    Temperature: 23.45 °C
```

---

### Step 07 — Write to a Characteristic
```bash
# Write raw bytes to a handle
sudo ./bin/ble_write AA:BB:CC:DD:EE:FF 1 0x0012 01

# Enable notifications by writing CCCD (automatically starts listen loop)
sudo ./bin/ble_write AA:BB:CC:DD:EE:FF 1 0x0013 01 00

# Disable notifications
sudo ./bin/ble_write AA:BB:CC:DD:EE:FF 1 0x0013 00 00
```

---

### Step 08 — Subscribe to Notifications (auto-discovers CCCDs)
```bash
sudo ./bin/ble_notify AA:BB:CC:DD:EE:FF 1
```
Output: real-time notification stream.
```
── Receiving Notifications (Ctrl+C to stop) ─────────────────

  ━━ Notification [handle=0x0012 uuid=0x2A19] (1 bytes)
     Hex : 57
     Value: Battery 87%

  ━━ Notification [handle=0x0020 uuid=0x2A6E] (2 bytes)
     Hex : 11 09
     Value: Temperature 23.37 °C

  [→ CONFIRM sent]   ← for indications
```

---

## Key Concepts Covered

### BLE Stack Layers
```
App Code (your C)
    ↓ socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP)
ATT Protocol (CID=4)       ← ATT_READ_REQ, ATT_WRITE_REQ, etc.
L2CAP
Link Layer                 ← Managed by BlueZ kernel driver
PHY (2.4 GHz radio)        ← Bluetooth chip hardware
```

### ATT Packet Format
```
All ATT packets: [opcode(1)] [parameters...]

ATT_READ_REQ:    [0x0A][handle_lo][handle_hi]
ATT_READ_RSP:    [0x0B][value...]
ATT_WRITE_REQ:   [0x12][handle_lo][handle_hi][value...]
ATT_WRITE_RSP:   [0x13]
ATT_NOTIFY:      [0x1B][handle_lo][handle_hi][value...]
ATT_INDICATE:    [0x1D][handle_lo][handle_hi][value...]
ATT_CONFIRM:     [0x1E]
ATT_ERROR:       [0x01][req_opcode][handle(2)][error_code]
```

### CCCD Values
```
0x0000 = Notifications disabled
0x0001 = Notifications enabled
0x0002 = Indications enabled
0x0003 = Both enabled
```

---

## Troubleshooting

| Error | Fix |
|-------|-----|
| `Permission denied` | Run with `sudo` |
| `No adapter found` | `sudo hciconfig hci0 up` |
| `Connection refused` | Device may not be advertising; check range |
| `Insufficient authentication` | Device requires pairing: `bluetoothctl pair <addr>` |
| `ATT Error 0x02 (Read Not Permitted)` | Characteristic doesn't support read |
| `hci_send_req failed` | Another process (bluetoothd) may own the socket. Stop it: `sudo systemctl stop bluetooth` |

---

## Useful BlueZ Commands (Companion Tools)

```bash
# Interactive BLE shell
bluetoothctl

# Inside bluetoothctl:
scan on                          # start scanning
devices                          # list found devices
connect AA:BB:CC:DD:EE:FF        # connect
gatt.discover-attributes         # discover services
gatt.read-attribute 0x0012       # read by handle

# Low-level HCI dump (see raw packets)
sudo hcidump -X

# gatttool (older but useful)
sudo gatttool -b AA:BB:CC:DD:EE:FF -I
# > connect
# > primary              (list services)
# > characteristics      (list chars)
# > char-read-hnd 0x0012 (read by handle)
# > char-write-req 0x0013 0100 (write CCCD)
```
