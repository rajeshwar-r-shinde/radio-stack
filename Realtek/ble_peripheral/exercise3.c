/*
 * exercise3.c — Full GATT Server: READ + WRITE Characteristic
 *
 * Implements:
 *   - LEAdvertisement1  (advertising)
 *   - GattManager1      (GATT application)
 *   - GattService1      (custom service, UUID = SVC_UUID)
 *   - GattCharacteristic1 (UUID = CHAR_RW_UUID, flags: read + write)
 *
 * In nRF Connect:
 *   1. SCAN → Connect to "LaptopGATT_Ex3"
 *   2. Expand "Unknown Service"
 *   3. Tap ↓ (READ)  → see "Hello nRF!" returned
 *   4. Tap ↑ (WRITE) → type bytes/text → printed in terminal
 *
 * Build:  make exercise3
 * Run:    sudo ./exercise3
 */

#include "ble_common.h"

GMainLoop *g_main_loop_handle = NULL;

/* ── Characteristic value buffer ─────────────────────────────────── */
static uint8_t  char_value[]  = "Hello nRF!";
static int      char_value_len = 10;

/* ── Forward declarations ────────────────────────────────────────── */
static DBusHandlerResult adv_handler (DBusConnection*, DBusMessage*, void*);
static DBusHandlerResult app_handler (DBusConnection*, DBusMessage*, void*);
static DBusHandlerResult svc_handler (DBusConnection*, DBusMessage*, void*);
static DBusHandlerResult chr_handler (DBusConnection*, DBusMessage*, void*);

static const DBusObjectPathVTable adv_vtable = { .message_function = adv_handler };
static const DBusObjectPathVTable app_vtable = { .message_function = app_handler };
static const DBusObjectPathVTable svc_vtable = { .message_function = svc_handler };
static const DBusObjectPathVTable chr_vtable = { .message_function = chr_handler };

/* ════════════════════════════════════════════════════════════════════
 * Advertisement handler — responds to GetAll with adv properties
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

        /* Type */
        const char *kt = "Type", *vt = "peripheral";
        dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &kt);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &vt);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&props, &entry);

        /* LocalName */
        const char *kn = "LocalName", *vn = "LaptopGATT_Ex3";
        dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &kn);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &vn);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&props, &entry);

        /* ServiceUUIDs */
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
 * Application handler — GetManagedObjects returns all GATT objects
 * ════════════════════════════════════════════════════════════════════ */
static void append_service_entry(DBusMessageIter *objects)
{
    /* Entry: { "/org/bluez/svc0": { "GattService1": { "UUID":"...", "Primary":true } } } */
    DBusMessageIter top_entry, iface_dict, iface_entry, props, entry, var;
    const char *path = SVC_PATH;
    const char *k_uuid = "UUID", *v_uuid = SVC_UUID;
    const char *k_prim = "Primary";
    dbus_bool_t v_prim = TRUE;

    dbus_message_iter_open_container(objects, DBUS_TYPE_DICT_ENTRY, NULL, &top_entry);
    dbus_message_iter_append_basic(&top_entry, DBUS_TYPE_OBJECT_PATH, &path);
    dbus_message_iter_open_container(&top_entry, DBUS_TYPE_ARRAY, "{sa{sv}}", &iface_dict);

    dbus_message_iter_open_container(&iface_dict, DBUS_TYPE_DICT_ENTRY, NULL, &iface_entry);
    const char *iface = BLUEZ_GATT_SVC_IFACE;
    dbus_message_iter_append_basic(&iface_entry, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_open_container(&iface_entry, DBUS_TYPE_ARRAY, "{sv}", &props);

    /* UUID */
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k_uuid);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &v_uuid);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&props, &entry);

    /* Primary */
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k_prim);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &v_prim);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&props, &entry);

    dbus_message_iter_close_container(&iface_entry, &props);
    dbus_message_iter_close_container(&iface_dict, &iface_entry);
    dbus_message_iter_close_container(&top_entry, &iface_dict);
    dbus_message_iter_close_container(objects, &top_entry);
}

static void append_char_entry(DBusMessageIter *objects)
{
    DBusMessageIter top_entry, iface_dict, iface_entry, props, entry, var, arr;
    const char *path    = CHAR_RW_PATH;
    const char *svcpath = SVC_PATH;
    const char *k_uuid  = "UUID",    *v_uuid = CHAR_RW_UUID;
    const char *k_svc   = "Service";
    const char *k_flags = "Flags";
    const char *flag_r  = "read", *flag_w = "write";

    dbus_message_iter_open_container(objects, DBUS_TYPE_DICT_ENTRY, NULL, &top_entry);
    dbus_message_iter_append_basic(&top_entry, DBUS_TYPE_OBJECT_PATH, &path);
    dbus_message_iter_open_container(&top_entry, DBUS_TYPE_ARRAY, "{sa{sv}}", &iface_dict);

    dbus_message_iter_open_container(&iface_dict, DBUS_TYPE_DICT_ENTRY, NULL, &iface_entry);
    const char *iface = BLUEZ_GATT_CHR_IFACE;
    dbus_message_iter_append_basic(&iface_entry, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_open_container(&iface_entry, DBUS_TYPE_ARRAY, "{sv}", &props);

    /* UUID */
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k_uuid);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &v_uuid);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&props, &entry);

    /* Service */
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k_svc);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "o", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_OBJECT_PATH, &svcpath);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&props, &entry);

    /* Flags: ["read", "write"] */
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k_flags);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &arr);
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &flag_r);
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &flag_w);
    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&props, &entry);

    dbus_message_iter_close_container(&iface_entry, &props);
    dbus_message_iter_close_container(&iface_dict, &iface_entry);
    dbus_message_iter_close_container(&top_entry, &iface_dict);
    dbus_message_iter_close_container(objects, &top_entry);
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
        append_service_entry(&objects);
        append_char_entry(&objects);
        dbus_message_iter_close_container(&it, &objects);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ════════════════════════════════════════════════════════════════════
 * Service handler (unused — BlueZ reads via GetManagedObjects)
 * ════════════════════════════════════════════════════════════════════ */
static DBusHandlerResult svc_handler(DBusConnection *conn, DBusMessage *msg, void *data)
{
    (void)conn; (void)msg; (void)data;
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ════════════════════════════════════════════════════════════════════
 * Characteristic handler — ReadValue + WriteValue
 * ════════════════════════════════════════════════════════════════════ */
static DBusHandlerResult chr_handler(DBusConnection *conn, DBusMessage *msg, void *data)
{
    (void)data;
    if (strcmp(dbus_message_get_path(msg), CHAR_RW_PATH) != 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    const char *member = dbus_message_get_member(msg);
    const char *iface  = dbus_message_get_interface(msg);

    if (!iface || strcmp(iface, BLUEZ_GATT_CHR_IFACE) != 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    /* ── ReadValue ────────────────────────────────────────────────── */
    if (member && strcmp(member, "ReadValue") == 0) {
        printf("  [READ]  → Sending: %.*s\n", char_value_len, char_value);

        DBusMessage    *reply = dbus_message_new_method_return(msg);
        DBusMessageIter it, arr;
        dbus_message_iter_init_append(reply, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "y", &arr);
        for (int i = 0; i < char_value_len; i++)
            dbus_message_iter_append_basic(&arr, DBUS_TYPE_BYTE, &char_value[i]);
        dbus_message_iter_close_container(&it, &arr);

        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* ── WriteValue ───────────────────────────────────────────────── */
    if (member && strcmp(member, "WriteValue") == 0) {
        DBusMessageIter iter, arr_iter;
        dbus_message_iter_init(msg, &iter);
        dbus_message_iter_recurse(&iter, &arr_iter);

        char   buf[256] = {0};
        int    len      = 0;
        while (dbus_message_iter_get_arg_type(&arr_iter) == DBUS_TYPE_BYTE && len < 255) {
            uint8_t b;
            dbus_message_iter_get_basic(&arr_iter, &b);
            char_value[len] = b;
            buf[len]        = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
            len++;
            dbus_message_iter_next(&arr_iter);
        }
        char_value_len = len;
        buf[len] = '\0';

        printf("  [WRITE] ← Received from nRF Connect: '%s'  (%d bytes)\n", buf, len);
        printf("            Hex:");
        for (int i = 0; i < len; i++) printf(" %02X", char_value[i]);
        printf("\n");

        /* Send empty success reply */
        DBusMessage *reply = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ── Registration callbacks ──────────────────────────────────────── */
static void on_gatt_registered(DBusPendingCall *call, void *data)
{
    (void)data;
    DBusMessage *r = dbus_pending_call_steal_reply(call);
    if (dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR) {
        const char *e = NULL;
        dbus_message_get_args(r, NULL, DBUS_TYPE_STRING, &e, DBUS_TYPE_INVALID);
        ERR("RegisterApplication: %s", e ? e : "?");
        g_main_loop_quit(g_main_loop_handle);
    } else {
        OK("GATT application registered");
    }
    dbus_message_unref(r);
}

static void on_adv_registered(DBusPendingCall *call, void *data)
{
    (void)data;
    DBusMessage *r = dbus_pending_call_steal_reply(call);
    if (dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR) {
        const char *e = NULL;
        dbus_message_get_args(r, NULL, DBUS_TYPE_STRING, &e, DBUS_TYPE_INVALID);
        ERR("RegisterAdvertisement: %s", e ? e : "?");
        g_main_loop_quit(g_main_loop_handle);
    } else {
        OK("Advertising as 'LaptopGATT_Ex3'");
        printf("\n  In nRF Connect:\n");
        printf("    1. SCAN → Connect to 'LaptopGATT_Ex3'\n");
        printf("    2. Expand 'Unknown Service'\n");
        printf("    3. Tap ↓ READ  → returns 'Hello nRF!'\n");
        printf("    4. Tap ↑ WRITE → type text/hex → printed here\n");
        printf("\n  Press Ctrl+C to stop\n\n");
    }
    dbus_message_unref(r);
}

int main(void)
{
    if (geteuid() != 0) { ERR("Run as root: sudo ./exercise3"); return 1; }

    printf("\n=== Exercise 3: Full GATT Server (READ + WRITE) ===\n\n");
    printf("  Service UUID : %s\n", SVC_UUID);
    printf("  Char UUID    : %s\n\n", CHAR_RW_UUID);
    setup_signals();

    DBusError err;
    dbus_error_init(&err);
    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err)) { ERR("%s", err.message); return 1; }

    char *adapter = find_adapter(conn);
    if (!adapter) return 1;
    LOG("Adapter: %s", adapter);

    adapter_set_property(conn, adapter, "Alias", "LaptopGATT_Ex3");

    /* Register D-Bus object paths */
    dbus_connection_register_object_path(conn, ADV_PATH,      &adv_vtable, NULL);
    dbus_connection_register_object_path(conn, APP_PATH,      &app_vtable, NULL);
    dbus_connection_register_object_path(conn, SVC_PATH,      &svc_vtable, NULL);
    dbus_connection_register_object_path(conn, CHAR_RW_PATH,  &chr_vtable, NULL);

    /* RegisterApplication */
    DBusMessage    *msg;
    DBusPendingCall *call;
    DBusMessageIter it, opts;
    const char     *app_path = APP_PATH;

    msg = dbus_message_new_method_call(BLUEZ_SERVICE, adapter, BLUEZ_GATT_MGR_IFACE, "RegisterApplication");
    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &app_path);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &opts);
    dbus_message_iter_close_container(&it, &opts);
    dbus_connection_send_with_reply(conn, msg, &call, 5000);
    dbus_message_unref(msg);
    dbus_pending_call_set_notify(call, on_gatt_registered, NULL, NULL);

    /* RegisterAdvertisement */
    const char *adv_path = ADV_PATH;
    msg = dbus_message_new_method_call(BLUEZ_SERVICE, adapter, BLUEZ_LE_ADV_MGR_IFACE, "RegisterAdvertisement");
    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &adv_path);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &opts);
    dbus_message_iter_close_container(&it, &opts);
    dbus_connection_send_with_reply(conn, msg, &call, 5000);
    dbus_message_unref(msg);
    dbus_pending_call_set_notify(call, on_adv_registered, NULL, NULL);

    GMainContext *ctx = g_main_context_default();
    g_main_loop_handle = g_main_loop_new(ctx, FALSE);
    dbus_connection_setup_with_g_main(conn, ctx);
    g_main_loop_run(g_main_loop_handle);

    /* Cleanup */
    DBusMessage *unreg;
    unreg = dbus_message_new_method_call(BLUEZ_SERVICE, adapter, BLUEZ_LE_ADV_MGR_IFACE, "UnregisterAdvertisement");
    dbus_message_iter_init_append(unreg, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &adv_path);
    DBusMessage *r = dbus_connection_send_with_reply_and_block(conn, unreg, 2000, NULL);
    dbus_message_unref(unreg);
    if (r) dbus_message_unref(r);

    unreg = dbus_message_new_method_call(BLUEZ_SERVICE, adapter, BLUEZ_GATT_MGR_IFACE, "UnregisterApplication");
    dbus_message_iter_init_append(unreg, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &app_path);
    r = dbus_connection_send_with_reply_and_block(conn, unreg, 2000, NULL);
    dbus_message_unref(unreg);
    if (r) dbus_message_unref(r);

    g_main_loop_unref(g_main_loop_handle);
    dbus_connection_unref(conn);
    OK("Done.");
    return 0;
}
