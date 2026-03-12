#ifndef BLE_COMMON_H
#define BLE_COMMON_H

/*
 * ble_common.h — Shared BlueZ D-Bus constants and helpers
 * Used by all 5 exercises.
 *
 * Requires: libdbus-1-dev, libglib2.0-dev
 * Build:    see Makefile
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>   /* dbus_connection_setup_with_g_main */
#include <glib.h>

/* ── BlueZ D-Bus names ───────────────────────────────────────────────── */
#define BLUEZ_SERVICE           "org.bluez"
#define BLUEZ_ADAPTER_IFACE     "org.bluez.Adapter1"
#define BLUEZ_LE_ADV_IFACE      "org.bluez.LEAdvertisement1"
#define BLUEZ_LE_ADV_MGR_IFACE  "org.bluez.LEAdvertisingManager1"
#define BLUEZ_GATT_MGR_IFACE    "org.bluez.GattManager1"
#define BLUEZ_GATT_SVC_IFACE    "org.bluez.GattService1"
#define BLUEZ_GATT_CHR_IFACE    "org.bluez.GattCharacteristic1"
#define DBUS_PROP_IFACE         "org.freedesktop.DBus.Properties"
#define DBUS_OM_IFACE           "org.freedesktop.DBus.ObjectManager"

/* ── Object paths ────────────────────────────────────────────────────── */
#define ADV_PATH                "/org/bluez/adv0"
#define APP_PATH                "/"
#define SVC_PATH                "/org/bluez/svc0"
#define CHAR_RW_PATH            "/org/bluez/svc0/char0"
#define CHAR_NTF_PATH           "/org/bluez/svc0/char1"

/* ── Custom 128-bit UUIDs ────────────────────────────────────────────── */
#define SVC_UUID                "12345678-1234-5678-1234-56789abcdef0"
#define CHAR_RW_UUID            "12345678-1234-5678-1234-56789abcdef1"
#define CHAR_NTF_UUID           "12345678-1234-5678-1234-56789abcdef2"

/* ── Standard BLE service UUIDs (for Exercise 1 reference) ──────────── */
#define HEART_RATE_SVC_UUID     "0000180d-0000-1000-8000-00805f9b34fb"
#define BATTERY_SVC_UUID        "0000180f-0000-1000-8000-00805f9b34fb"

/* ── Logging helpers ─────────────────────────────────────────────────── */
#define LOG(fmt, ...)   printf("[BLE] " fmt "\n", ##__VA_ARGS__)
#define ERR(fmt, ...)   fprintf(stderr, "[ERR] " fmt "\n", ##__VA_ARGS__)
#define OK(fmt, ...)    printf("[ OK] " fmt "\n", ##__VA_ARGS__)

/* ── Global GLib main loop (shared across exercises) ─────────────────── */
extern GMainLoop *g_main_loop_handle;

/* ── Helper: find adapter D-Bus path ─────────────────────────────────── */
static inline char *find_adapter(DBusConnection *conn)
{
    DBusMessage *msg, *reply;
    DBusError    err;
    static char  adapter_path[256] = {0};

    dbus_error_init(&err);

    msg = dbus_message_new_method_call(
            BLUEZ_SERVICE, "/",
            DBUS_OM_IFACE, "GetManagedObjects");
    if (!msg) { ERR("OOM"); return NULL; }

    reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, &err);
    dbus_message_unref(msg);

    if (dbus_error_is_set(&err)) {
        ERR("GetManagedObjects: %s", err.message);
        dbus_error_free(&err);
        return NULL;
    }

    /* Walk the object tree looking for Adapter1 */
    DBusMessageIter iter, objects, obj, ifaces, iface_entry;
    dbus_message_iter_init(reply, &iter);
    dbus_message_iter_recurse(&iter, &objects);          /* a{oa{sa{sv}}} */

    while (dbus_message_iter_get_arg_type(&objects) == DBUS_TYPE_DICT_ENTRY) {
        dbus_message_iter_recurse(&objects, &obj);

        const char *path = NULL;
        dbus_message_iter_get_basic(&obj, &path);
        dbus_message_iter_next(&obj);

        dbus_message_iter_recurse(&obj, &ifaces);        /* a{sa{sv}} */
        while (dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_DICT_ENTRY) {
            dbus_message_iter_recurse(&ifaces, &iface_entry);
            const char *iface_name = NULL;
            dbus_message_iter_get_basic(&iface_entry, &iface_name);
            if (strcmp(iface_name, BLUEZ_ADAPTER_IFACE) == 0) {
                strncpy(adapter_path, path, sizeof(adapter_path) - 1);
                dbus_message_unref(reply);
                return adapter_path;
            }
            dbus_message_iter_next(&ifaces);
        }
        dbus_message_iter_next(&objects);
    }

    dbus_message_unref(reply);
    ERR("No Bluetooth adapter found. Try: sudo hciconfig hci0 up");
    return NULL;
}

/* ── Helper: set adapter string property (Alias, etc.) ──────────────── */
static inline void adapter_set_property(DBusConnection *conn,
                                        const char *adapter_path,
                                        const char *prop,
                                        const char *value)
{
    DBusMessage *msg;
    DBusMessageIter iter, variant;
    DBusError err;
    dbus_error_init(&err);

    msg = dbus_message_new_method_call(
            BLUEZ_SERVICE, adapter_path,
            DBUS_PROP_IFACE, "Set");

    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &(const char *){BLUEZ_ADAPTER_IFACE});
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&iter, &variant);

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, &err);
    dbus_message_unref(msg);
    if (reply) dbus_message_unref(reply);
    if (dbus_error_is_set(&err)) dbus_error_free(&err);
}

/* ── Signal handler: Ctrl+C stops main loop ──────────────────────────── */
static inline void sig_handler(int sig)
{
    (void)sig;
    printf("\n[BLE] Stopping...\n");
    if (g_main_loop_handle)
        g_main_loop_quit(g_main_loop_handle);
}

static inline void setup_signals(void)
{
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
}

#endif /* BLE_COMMON_H */
