/*
 * exercise4.c — GATT Notify Characteristic (live counter)
 *
 * Adds a second characteristic with NOTIFY flag.
 * A GLib timer sends an incrementing uint16 counter every second.
 * When nRF Connect subscribes (taps 🔔), it receives live updates.
 *
 * In nRF Connect:
 *   1. SCAN → Connect to "LaptopGATT_Ex4"
 *   2. Expand service → find char with NOTIFY flag
 *   3. Tap 🔔 BELL to subscribe
 *   4. Watch counter update every second: 0001, 0002, 0003...
 *   5. Note: bytes are little-endian uint16
 *      e.g. counter=5 → bytes [05 00]
 *
 * Build:  make exercise4
 * Run:    sudo ./exercise4
 */

#include "ble_common.h"

GMainLoop *g_main_loop_handle = NULL;

/* ── Shared state ────────────────────────────────────────────────── */
static DBusConnection *g_conn       = NULL;
static uint16_t        g_counter    = 0;
static int             g_notifying  = 0;   /* 1 = client subscribed */

/* ── Forward declarations ────────────────────────────────────────── */
static DBusHandlerResult adv_handler (DBusConnection*, DBusMessage*, void*);
static DBusHandlerResult app_handler (DBusConnection*, DBusMessage*, void*);
static DBusHandlerResult chr_handler (DBusConnection*, DBusMessage*, void*);

static const DBusObjectPathVTable adv_vtable = { .message_function = adv_handler };
static const DBusObjectPathVTable app_vtable = { .message_function = app_handler };
static const DBusObjectPathVTable chr_vtable = { .message_function = chr_handler };

/* ════════════════════════════════════════════════════════════════════
 * Advertisement (same pattern as ex3, different name)
 * ════════════════════════════════════════════════════════════════════ */
static DBusHandlerResult adv_handler(DBusConnection *conn, DBusMessage *msg, void *data)
{
    (void)data;
    if (strcmp(dbus_message_get_path(msg), ADV_PATH) != 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    const char *member = dbus_message_get_member(msg);
    const char *iface  = dbus_message_get_interface(msg);

    if (iface && strcmp(iface, DBUS_PROP_IFACE) == 0
     && member && strcmp(member, "GetAll") == 0)
    {
        DBusMessage    *reply = dbus_message_new_method_return(msg);
        DBusMessageIter it, props, entry, var, arr;
        dbus_message_iter_init_append(reply, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &props);

        const char *kt = "Type", *vt = "peripheral";
        dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &kt);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &vt);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&props, &entry);

        const char *kn = "LocalName", *vn = "LaptopGATT_Ex4";
        dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &kn);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &vn);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&props, &entry);

        const char *ku = "ServiceUUIDs", *vu = SVC_UUID;
        dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &ku);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &var);
        dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &arr);
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &vu);
        dbus_message_iter_close_container(&var, &arr);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&props, &entry);

        dbus_message_iter_close_container(&it, &props);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (member && strcmp(member, "Release") == 0) {
        DBusMessage *r = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, r, NULL);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ════════════════════════════════════════════════════════════════════
 * GetManagedObjects — exposes service + notify characteristic
 * ════════════════════════════════════════════════════════════════════ */
static void append_svc(DBusMessageIter *objects)
{
    DBusMessageIter top, ifaces, ie, props, e, v;
    const char *path = SVC_PATH;
    const char *k1 = "UUID",    *v1 = SVC_UUID;
    const char *k2 = "Primary";
    dbus_bool_t vb = TRUE;
    const char *svc_iface = BLUEZ_GATT_SVC_IFACE;

    dbus_message_iter_open_container(objects, DBUS_TYPE_DICT_ENTRY, NULL, &top);
    dbus_message_iter_append_basic(&top, DBUS_TYPE_OBJECT_PATH, &path);
    dbus_message_iter_open_container(&top, DBUS_TYPE_ARRAY, "{sa{sv}}", &ifaces);
    dbus_message_iter_open_container(&ifaces, DBUS_TYPE_DICT_ENTRY, NULL, &ie);
    dbus_message_iter_append_basic(&ie, DBUS_TYPE_STRING, &svc_iface);
    dbus_message_iter_open_container(&ie, DBUS_TYPE_ARRAY, "{sv}", &props);

    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &k1);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "s", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &v1);
    dbus_message_iter_close_container(&e, &v);
    dbus_message_iter_close_container(&props, &e);

    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &k2);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "b", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &vb);
    dbus_message_iter_close_container(&e, &v);
    dbus_message_iter_close_container(&props, &e);

    dbus_message_iter_close_container(&ie, &props);
    dbus_message_iter_close_container(&ifaces, &ie);
    dbus_message_iter_close_container(&top, &ifaces);
    dbus_message_iter_close_container(objects, &top);
}

static void append_chr(DBusMessageIter *objects)
{
    DBusMessageIter top, ifaces, ie, props, e, v, arr;
    const char *path    = CHAR_NTF_PATH;
    const char *svcpath = SVC_PATH;
    const char *chr_iface = BLUEZ_GATT_CHR_IFACE;
    const char *ku = "UUID",    *vu = CHAR_NTF_UUID;
    const char *ks = "Service";
    const char *kf = "Flags";
    const char *fr = "read", *fn2 = "notify";

    /* Current value: 2-byte little-endian counter */
    uint8_t lo = g_counter & 0xFF, hi = (g_counter >> 8) & 0xFF;

    dbus_message_iter_open_container(objects, DBUS_TYPE_DICT_ENTRY, NULL, &top);
    dbus_message_iter_append_basic(&top, DBUS_TYPE_OBJECT_PATH, &path);
    dbus_message_iter_open_container(&top, DBUS_TYPE_ARRAY, "{sa{sv}}", &ifaces);
    dbus_message_iter_open_container(&ifaces, DBUS_TYPE_DICT_ENTRY, NULL, &ie);
    dbus_message_iter_append_basic(&ie, DBUS_TYPE_STRING, &chr_iface);
    dbus_message_iter_open_container(&ie, DBUS_TYPE_ARRAY, "{sv}", &props);

    /* UUID */
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &ku);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "s", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &vu);
    dbus_message_iter_close_container(&e, &v);
    dbus_message_iter_close_container(&props, &e);

    /* Service */
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &ks);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "o", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_OBJECT_PATH, &svcpath);
    dbus_message_iter_close_container(&e, &v);
    dbus_message_iter_close_container(&props, &e);

    /* Flags */
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &kf);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "as", &v);
    dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "s", &arr);
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &fr);
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &fn2);
    dbus_message_iter_close_container(&v, &arr);
    dbus_message_iter_close_container(&e, &v);
    dbus_message_iter_close_container(&props, &e);

    /* Value (current counter) */
    const char *kv = "Value";
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &kv);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "ay", &v);
    dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "y", &arr);
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_BYTE, &lo);
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_BYTE, &hi);
    dbus_message_iter_close_container(&v, &arr);
    dbus_message_iter_close_container(&e, &v);
    dbus_message_iter_close_container(&props, &e);

    dbus_message_iter_close_container(&ie, &props);
    dbus_message_iter_close_container(&ifaces, &ie);
    dbus_message_iter_close_container(&top, &ifaces);
    dbus_message_iter_close_container(objects, &top);
}

static DBusHandlerResult app_handler(DBusConnection *conn, DBusMessage *msg, void *data)
{
    (void)data;
    const char *member = dbus_message_get_member(msg);
    const char *iface  = dbus_message_get_interface(msg);

    if (iface && strcmp(iface, DBUS_OM_IFACE) == 0
     && member && strcmp(member, "GetManagedObjects") == 0)
    {
        DBusMessage    *reply = dbus_message_new_method_return(msg);
        DBusMessageIter it, objects;
        dbus_message_iter_init_append(reply, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{oa{sa{sv}}}", &objects);
        append_svc(&objects);
        append_chr(&objects);
        dbus_message_iter_close_container(&it, &objects);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ════════════════════════════════════════════════════════════════════
 * Characteristic handler — ReadValue, StartNotify, StopNotify
 * ════════════════════════════════════════════════════════════════════ */
static DBusHandlerResult chr_handler(DBusConnection *conn, DBusMessage *msg, void *data)
{
    (void)data;
    if (strcmp(dbus_message_get_path(msg), CHAR_NTF_PATH) != 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    const char *member = dbus_message_get_member(msg);
    const char *iface  = dbus_message_get_interface(msg);

    if (!iface || strcmp(iface, BLUEZ_GATT_CHR_IFACE) != 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    /* ReadValue */
    if (member && strcmp(member, "ReadValue") == 0) {
        uint8_t lo = g_counter & 0xFF, hi = (g_counter >> 8) & 0xFF;
        printf("  [READ]  counter = %u  (0x%04X)\n", g_counter, g_counter);

        DBusMessage    *reply = dbus_message_new_method_return(msg);
        DBusMessageIter it, arr;
        dbus_message_iter_init_append(reply, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "y", &arr);
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_BYTE, &lo);
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_BYTE, &hi);
        dbus_message_iter_close_container(&it, &arr);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* StartNotify */
    if (member && strcmp(member, "StartNotify") == 0) {
        printf("  [NOTIFY] Client subscribed ✓  (counter will stream every 1s)\n");
        g_notifying = 1;
        DBusMessage *reply = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* StopNotify */
    if (member && strcmp(member, "StopNotify") == 0) {
        printf("  [NOTIFY] Client unsubscribed\n");
        g_notifying = 0;
        DBusMessage *reply = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ════════════════════════════════════════════════════════════════════
 * GLib timer — fires every 1000 ms, sends PropertiesChanged signal
 * ════════════════════════════════════════════════════════════════════ */
static gboolean notify_tick(gpointer user_data)
{
    (void)user_data;

    g_counter = (g_counter + 1) % 65536;
    uint8_t lo = g_counter & 0xFF;
    uint8_t hi = (g_counter >> 8) & 0xFF;

    printf("  [NOTIFY] counter = %5u  (0x%04X)%s\n",
           g_counter, g_counter,
           g_notifying ? "" : "  [no subscriber]");

    if (!g_notifying || !g_conn) return G_SOURCE_CONTINUE;

    /*
     * Send: org.freedesktop.DBus.Properties.PropertiesChanged
     *   interface_name = "org.bluez.GattCharacteristic1"
     *   changed_props  = { "Value": ay [lo, hi] }
     *   invalidated    = []
     */
    DBusMessage    *sig = dbus_message_new_signal(
                            CHAR_NTF_PATH,
                            DBUS_PROP_IFACE,
                            "PropertiesChanged");
    DBusMessageIter it, dict, entry, variant, arr, inv;
    const char *chr_iface = BLUEZ_GATT_CHR_IFACE;
    const char *kv        = "Value";

    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &chr_iface);

    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &kv);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "ay", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "y", &arr);
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_BYTE, &lo);
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_BYTE, &hi);
    dbus_message_iter_close_container(&variant, &arr);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(&dict, &entry);
    dbus_message_iter_close_container(&it, &dict);

    /* Invalidated: empty array of strings */
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &inv);
    dbus_message_iter_close_container(&it, &inv);

    dbus_connection_send(g_conn, sig, NULL);
    dbus_connection_flush(g_conn);
    dbus_message_unref(sig);

    return G_SOURCE_CONTINUE;   /* keep repeating */
}

/* ── Registration callbacks ──────────────────────────────────────── */
static void on_gatt_ok(DBusPendingCall *call, void *d)
{
    (void)d;
    DBusMessage *r = dbus_pending_call_steal_reply(call);
    if (dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR) {
        const char *e = NULL;
        dbus_message_get_args(r, NULL, DBUS_TYPE_STRING, &e, DBUS_TYPE_INVALID);
        ERR("RegisterApplication: %s", e);
        g_main_loop_quit(g_main_loop_handle);
    } else {
        OK("GATT registered");
    }
    dbus_message_unref(r);
}

static void on_adv_ok(DBusPendingCall *call, void *d)
{
    (void)d;
    DBusMessage *r = dbus_pending_call_steal_reply(call);
    if (dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR) {
        const char *e = NULL;
        dbus_message_get_args(r, NULL, DBUS_TYPE_STRING, &e, DBUS_TYPE_INVALID);
        ERR("RegisterAdvertisement: %s", e);
        g_main_loop_quit(g_main_loop_handle);
    } else {
        OK("Advertising as 'LaptopGATT_Ex4'");
        printf("\n  In nRF Connect:\n");
        printf("    1. SCAN → Connect to 'LaptopGATT_Ex4'\n");
        printf("    2. Find the Notify characteristic (UUID ends in ...def2)\n");
        printf("    3. Tap 🔔 BELL to subscribe\n");
        printf("    4. Watch counter update every second\n");
        printf("    5. Bytes are little-endian uint16: [05 00] = 5\n");
        printf("\n  Press Ctrl+C to stop\n\n");

        /* Start 1-second notify timer */
        g_timeout_add(1000, notify_tick, NULL);
    }
    dbus_message_unref(r);
}

int main(void)
{
    if (geteuid() != 0) { ERR("Run as root: sudo ./exercise4"); return 1; }

    printf("\n=== Exercise 4: Notify Characteristic (live counter) ===\n\n");
    printf("  Notify Char UUID: %s\n\n", CHAR_NTF_UUID);
    setup_signals();

    DBusError err;
    dbus_error_init(&err);
    g_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err)) { ERR("%s", err.message); return 1; }

    char *adapter = find_adapter(g_conn);
    if (!adapter) return 1;

    adapter_set_property(g_conn, adapter, "Alias", "LaptopGATT_Ex4");

    dbus_connection_register_object_path(g_conn, ADV_PATH,      &adv_vtable, NULL);
    dbus_connection_register_object_path(g_conn, APP_PATH,      &app_vtable, NULL);
    dbus_connection_register_object_path(g_conn, CHAR_NTF_PATH, &chr_vtable, NULL);

    DBusMessage    *msg;
    DBusPendingCall *call;
    DBusMessageIter it, opts;
    const char *app_path = APP_PATH, *adv_path = ADV_PATH;

    msg = dbus_message_new_method_call(BLUEZ_SERVICE, adapter, BLUEZ_GATT_MGR_IFACE, "RegisterApplication");
    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &app_path);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &opts);
    dbus_message_iter_close_container(&it, &opts);
    dbus_connection_send_with_reply(g_conn, msg, &call, 5000);
    dbus_message_unref(msg);
    dbus_pending_call_set_notify(call, on_gatt_ok, NULL, NULL);

    msg = dbus_message_new_method_call(BLUEZ_SERVICE, adapter, BLUEZ_LE_ADV_MGR_IFACE, "RegisterAdvertisement");
    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &adv_path);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &opts);
    dbus_message_iter_close_container(&it, &opts);
    dbus_connection_send_with_reply(g_conn, msg, &call, 5000);
    dbus_message_unref(msg);
    dbus_pending_call_set_notify(call, on_adv_ok, NULL, NULL);

    GMainContext *ctx = g_main_context_default();
    g_main_loop_handle = g_main_loop_new(ctx, FALSE);
    dbus_connection_setup_with_g_main(g_conn, ctx);
    g_main_loop_run(g_main_loop_handle);

    g_main_loop_unref(g_main_loop_handle);
    dbus_connection_unref(g_conn);
    OK("Done.");
    return 0;
}
