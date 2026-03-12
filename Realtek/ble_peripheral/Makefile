# Makefile — BLE Peripheral Exercises (Linux BlueZ D-Bus)
#
# Dependencies:
#   sudo apt install libdbus-1-dev libglib2.0-dev
#
# Build all:   make
# Build one:   make exercise3
# Clean:       make clean

CC      := gcc
CFLAGS  := -Wall -Wextra -g \
           $(shell pkg-config --cflags dbus-1 glib-2.0) \
           -I.
LDFLAGS := $(shell pkg-config --libs dbus-1 glib-2.0) \
           -ldbus-glib-1

TARGETS := exercise1 exercise2 exercise3 exercise4 exercise5

.PHONY: all clean check-deps

all: check-deps $(TARGETS)

check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists dbus-1     || (echo "ERROR: Install libdbus-1-dev:  sudo apt install libdbus-1-dev" && exit 1)
	@pkg-config --exists glib-2.0   || (echo "ERROR: Install libglib2.0-dev: sudo apt install libglib2.0-dev" && exit 1)
	@pkg-config --exists dbus-glib-1 2>/dev/null || \
		echo "WARN: dbus-glib-1 missing — install: sudo apt install libdbus-glib-1-dev"
	@echo "OK: All dependencies found"

exercise1: exercise1.c ble_common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "Built: $@  →  sudo ./$@"

exercise2: exercise2.c ble_common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "Built: $@  →  sudo ./$@"

exercise3: exercise3.c ble_common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "Built: $@  →  sudo ./$@"

exercise4: exercise4.c ble_common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "Built: $@  →  sudo ./$@"

exercise5: exercise5.c
	$(CC) $(CFLAGS) -o $@ $< 
	@echo "Built: $@  →  ./$@ (no sudo needed)"

clean:
	rm -f $(TARGETS)
	@echo "Cleaned."

help:
	@echo ""
	@echo "  make              — build all exercises"
	@echo "  make exercise3    — build only exercise 3"
	@echo "  sudo ./exercise1  — HCI advertising (hcitool)"
	@echo "  sudo ./exercise2  — GAP advertisement via D-Bus"
	@echo "  sudo ./exercise3  — GATT server: READ + WRITE"
	@echo "  sudo ./exercise4  — GATT notify: live counter"
	@echo "  ./exercise5       — nRF Connect observation guide"
	@echo ""
