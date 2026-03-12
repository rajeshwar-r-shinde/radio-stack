/*
 * exercise2.c — BLE GAP Advertisement via BlueZ D-Bus
 *
 * Registers a LEAdvertisement1 D-Bus object and calls
 * LEAdvertisingManager1.RegisterAdvertisement on the adapter.
 * No GATT service — just broadcasts name + UUID in adv payload.
 *
 * Build:  make exercise2
 * Run:    sudo ./exercise2
 *
 * In nRF Connect:
 *   SCAN → see "LaptopGATT_Ex2"
 *   Tap device → RAW tab → see Service UUID 0x180D in payload
 *   Try to CONNECT → will fail (no GATT server) — that's expected!
 */

#include "ble_common.h"

GMainLoop *g_main_loop_handle = NULL;

/* ── D-Bus message filter: handle method calls on our adv object ─── */
static DBusHandlerResult adv_message_handler(DBusConnection *conn,
                                              DBusMessage    *msg,
                                              void           *data)
{
    (void)data;
    const char *iface  = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);
    const char *path   = dbus_message_get_path(msg);

    /* Only handle messages for our advertisement object */
    if (!path || strcmp(path, ADV_PATH) != 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    /* ── org.freedesktop.DBus.Properties.GetAll ─────────────────── */
    if (iface && strcmp(iface, DBUS_PROP_IFACE) == 0
     && member && strcmp(member, "GetAll") == 0)
    {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        DBusMessageIter iter, props, entry, variant, arr;

        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &props);

        /* Type = "peripheral" */
        const char *key_type  = "Type";
        const char *val_type  = "peripheral";
        dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key_type);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val_type);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&props, &entry);

        /* LocalName = "LaptopGATT_Ex2" */
        const char *key_name = "LocalName";
        const char *val_name = "LaptopGATT_Ex2";
        dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key_name);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val_name);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&props, &entry);

        /* ServiceUUIDs = ["0000180d-..."] (Heart Rate) */
        const char *key_uuids = "ServiceUUIDs";
        const char *uuid_val  = HEART_RATE_SVC_UUID;
        dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key_uuids);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &variant);
        dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &arr);
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &uuid_val);
        dbus_message_iter_close_container(&variant, &arr);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&props, &entry);

        /* IncludeTxPower = true */
        const char *key_tx  = "IncludeTxPower";
        dbus_bool_t val_tx  = TRUE;
        dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key_tx);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &val_tx);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&props, &entry);

        dbus_message_iter_close_container(&iter, &props);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* ── Release() — called by BlueZ when advertisement is removed ─ */
    if (iface && strcmp(iface, BLUEZ_LE_ADV_IFACE) == 0
     && member && strcmp(member, "Release") == 0)
    {
        LOG("Advertisement released by BlueZ");
        DBusMessage *reply = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable adv_vtable = {
    .message_function = adv_message_handler,
};

/* ── RegisterAdvertisement reply callback ────────────────────────── */
static void on_adv_registered(DBusPendingCall *call, void *data)
{
    (void)data;
    DBusMessage *reply = dbus_pending_call_steal_reply(call);

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        const char *err_msg = NULL;
        dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &err_msg, DBUS_TYPE_INVALID);
        ERR("RegisterAdvertisement failed: %s", err_msg ? err_msg : "(unknown)");
        ERR("Make sure bluetoothd is running: sudo systemctl start bluetooth");
        g_main_loop_quit(g_main_loop_handle);
    } else {
        OK("Advertisement registered!");
        OK("Advertising as 'LaptopGATT_Ex2'");
        printf("\n  Open nRF Connect → SCAN\n");
        printf("  Look for 'LaptopGATT_Ex2'\n");
        printf("  Press Ctrl+C to stop\n\n");
    }
    dbus_message_unref(reply);
}

int main(void)
{
    DBusConnection *conn;
    DBusError       err;
    char           *adapter_path;

    if (geteuid() != 0) { ERR("Run as root: sudo ./exercise2"); return 1; }

    printf("\n=== Exercise 2: GAP Advertisement via D-Bus ===\n\n");
    setup_signals();

    dbus_error_init(&err);
    conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err)) { ERR("%s", err.message); return 1; }

    adapter_path = find_adapter(conn);
    if (!adapter_path) return 1;
    LOG("Using adapter: %s", adapter_path);

    /* Set device name */
    adapter_set_property(conn, adapter_path, "Alias", "LaptopGATT_Ex2");

    /* Register our advertisement object on D-Bus */
    if (!dbus_connection_register_object_path(conn, ADV_PATH, &adv_vtable, NULL)) {
        ERR("Failed to register advertisement object path");
        return 1;
    }

    /* Call LEAdvertisingManager1.RegisterAdvertisement */
    DBusMessage    *msg;
    DBusPendingCall *call;
    DBusMessageIter iter, options;
    const char     *adv_path = ADV_PATH;

    msg = dbus_message_new_method_call(
            BLUEZ_SERVICE, adapter_path,
            BLUEZ_LE_ADV_MGR_IFACE, "RegisterAdvertisement");

    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &adv_path);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &options);
    dbus_message_iter_close_container(&iter, &options);   /* empty options dict */

    dbus_connection_send_with_reply(conn, msg, &call, 5000);
    dbus_message_unref(msg);
    dbus_pending_call_set_notify(call, on_adv_registered, NULL, NULL);

    /* Connect D-Bus to GLib main loop */
    GMainContext *ctx = g_main_context_default();
    g_main_loop_handle = g_main_loop_new(ctx, FALSE);

    /* Integrate dbus into glib loop */
    dbus_connection_setup_with_g_main(conn, ctx);

    g_main_loop_run(g_main_loop_handle);

    /* Cleanup: unregister advertisement */
    LOG("Unregistering advertisement...");
    DBusMessage *unreg = dbus_message_new_method_call(
            BLUEZ_SERVICE, adapter_path,
            BLUEZ_LE_ADV_MGR_IFACE, "UnregisterAdvertisement");
    DBusMessageIter ui;
    dbus_message_iter_init_append(unreg, &ui);
    dbus_message_iter_append_basic(&ui, DBUS_TYPE_OBJECT_PATH, &adv_path);
    DBusMessage *r = dbus_connection_send_with_reply_and_block(conn, unreg, 2000, NULL);
    dbus_message_unref(unreg);
    if (r) dbus_message_unref(r);

    g_main_loop_unref(g_main_loop_handle);
    dbus_connection_unref(conn);
    OK("Done.");
    return 0;
}
