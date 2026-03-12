# ═══════════════════════════════════════════════════════════════════
#  BLE Learning Series — Master Makefile
#  
#  Prerequisites:
#    sudo apt install libbluetooth-dev build-essential
#
#  Build all steps:
#    make all
#
#  Build individual step:
#    make step01   make step02   make step03   make step04
#    make step05   make step06   make step07   make step08
#
#  Clean:
#    make clean
# ═══════════════════════════════════════════════════════════════════

CC      = gcc
CFLAGS  = -Wall -Wextra -g -I/usr/include
LIBS    = -lbluetooth -lpthread
OUTDIR  = ./bin

all: dirs step01 step02 step03 step04 step05 step06 step07 step08
	@echo ""
	@echo "════════════════════════════════════════════════"
	@echo "  All BLE steps built in ./bin/"
	@echo "  Run each with: sudo ./bin/ble_XXX"
	@echo "════════════════════════════════════════════════"

dirs:
	mkdir -p $(OUTDIR)

step01:
	$(CC) $(CFLAGS) step01_check/ble_check.c -o $(OUTDIR)/ble_check $(LIBS)
	@echo "[✓] step01: ble_check"

step02:
	$(CC) $(CFLAGS) step02_scan/ble_scan.c -o $(OUTDIR)/ble_scan $(LIBS)
	@echo "[✓] step02: ble_scan"

step03:
	$(CC) $(CFLAGS) step03_inquiry/ble_active_scan.c -o $(OUTDIR)/ble_active_scan $(LIBS)
	@echo "[✓] step03: ble_active_scan"

step04:
	$(CC) $(CFLAGS) step04_connect/ble_connect.c -o $(OUTDIR)/ble_connect $(LIBS)
	@echo "[✓] step04: ble_connect"

step05:
	$(CC) $(CFLAGS) step05_discover/ble_discover.c -o $(OUTDIR)/ble_discover $(LIBS)
	@echo "[✓] step05: ble_discover"

step06:
	$(CC) $(CFLAGS) step06_read/ble_read.c -o $(OUTDIR)/ble_read $(LIBS)
	@echo "[✓] step06: ble_read"

step07:
	$(CC) $(CFLAGS) step07_write/ble_write.c -o $(OUTDIR)/ble_write $(LIBS)
	@echo "[✓] step07: ble_write"

step08:
	$(CC) $(CFLAGS) step08_notify/ble_notify.c -o $(OUTDIR)/ble_notify $(LIBS)
	@echo "[✓] step08: ble_notify"

clean:
	rm -rf $(OUTDIR)
	@echo "[✓] Cleaned"

install-deps:
	sudo apt install -y libbluetooth-dev build-essential

.PHONY: all dirs clean install-deps \
        step01 step02 step03 step04 step05 step06 step07 step08
