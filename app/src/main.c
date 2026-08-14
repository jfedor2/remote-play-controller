// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/usbd.h>

#include <chiaki/common.h>
#include <chiaki/controller.h>
#include <stdio.h>
#include <string.h>

#include "buttons.h"
#include "led_status.h"
#include "registration.h"
#include "session.h"
#include "settings.h"
#include "status.h"
#include "wifi_manager.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define CHK(X) ({ int err = X; if (err != 0) { LOG_ERR("%s returned %d (%s:%d)", #X, err, __FILE__, __LINE__); } err == 0; })

/* status_logf() already logs to the Zephyr log backend (LOG_INF) in
 * addition to pushing the line to the config website as HID Input Report 4,
 * so state-machine narration only needs to call it once. Also refreshes the
 * status LED's blink pattern, since every call site here is exactly a point
 * where Wi-Fi/session state may have just changed. */
#define STATUS_LOG(fmt, ...)             \
    do {                                 \
        status_logf(fmt, ##__VA_ARGS__); \
        led_status_refresh();            \
    } while (0)

/* ---------------------------------------------------------------------------
 * Explicit application state
 * -------------------------------------------------------------------------*/
typedef enum {
    APP_STATE_NO_WIFI,            /* no credentials; wait for WebHID config  */
    APP_STATE_WIFI_CONNECTING,    /* wifi_manager_init_and_connect() in flight*/
    APP_STATE_UNREGISTERED,       /* wifi OK; waiting for Link command        */
    APP_STATE_REGISTERING,        /* registration_run() in flight         */
    APP_STATE_SESSION_STARTING,   /* about to call session_init()         */
    APP_STATE_SESSION_CONNECTING, /* chiaki started; waiting for CONNECTED evt*/
    APP_STATE_SESSION_RUNNING,    /* connected; forwarding controller state   */
} AppState;

static const char* state_name(AppState s) {
    switch (s) {
        case APP_STATE_NO_WIFI:
            return "NO_WIFI";
        case APP_STATE_WIFI_CONNECTING:
            return "WIFI_CONNECTING";
        case APP_STATE_UNREGISTERED:
            return "UNREGISTERED";
        case APP_STATE_REGISTERING:
            return "REGISTERING";
        case APP_STATE_SESSION_STARTING:
            return "SESSION_STARTING";
        case APP_STATE_SESSION_CONNECTING:
            return "SESSION_CONNECTING";
        case APP_STATE_SESSION_RUNNING:
            return "SESSION_RUNNING";
    }
    return "?";
}

/* ---------------------------------------------------------------------------
 * Signals from USB callbacks to the main loop.
 *
 * All USB callbacks run in the USB stack context (limited stack, not allowed
 * to block).  They only set flags / give semaphores; the main loop does the
 * actual work.
 * -------------------------------------------------------------------------*/

/* Link button: stores the PIN and signals main to run registration. */
static K_SEM_DEFINE(s_link_sem, 0, 1);
static uint32_t s_pending_pin;

/* Config changes: volatile flags tell main *what* changed; the poll signal
 * wakes it up immediately so it doesn't have to wait for a timeout. */
static volatile bool s_wifi_changed = false;
static volatile bool s_ps5_cfg_changed = false;
static struct k_poll_signal s_config_signal = K_POLL_SIGNAL_INITIALIZER(s_config_signal);

/* set_report() runs on the "usbd" thread, which has a small stack and
 * shouldn't block on a flash write. It stages the merged config into
 * s_pending_cfg (under s_cfg_mutex) instead of calling
 * settings_save_config() directly; the main thread does the actual save.
 * s_pending_cfg is seeded once at boot and from then on only ever updated
 * by merges here -- set_report() never reads settings_get_config(), so the
 * main thread is free to save outside the lock without risking a
 * set_report() reseeding from a save that's still in flight. s_app_config
 * is only ever written by the main thread, so readers elsewhere need no
 * locking either. */
static K_MUTEX_DEFINE(s_cfg_mutex);
static AppConfig s_pending_cfg;
static bool s_cfg_save_pending = false;

/* ---------------------------------------------------------------------------
 * Controller state - written by buttons.c's polling thread (see
 * buttons_init() below), which:
 *   1. updates g_controller_state with the new button/axis values, then
 *   2. k_sem_give(&g_input_sem) to wake the main loop.
 *
 * The main loop sends it as soon as it's notified - buttons.c's polling
 * rate is the only thing that decides how often input is sent.
 * -------------------------------------------------------------------------*/

ChiakiControllerState g_controller_state; /* written by buttons.c  */
K_SEM_DEFINE(g_input_sem, 0, 1);          /* given by buttons.c    */

/* ---------------------------------------------------------------------------
 * HID descriptor and USB device setup
 * -------------------------------------------------------------------------*/

/* Report structs and IDs are defined in settings.h */

static uint8_t const report_descriptor[] = {
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x22,        // Usage (0x22)
    0xA1, 0x01,        // Collection (Application)
    // Report ID 1: Network Config
    0x85, 0x01,  //   Report ID (1)
    0x09, 0x01,  //   Usage (1)
    0x75, 0x08,  //   Report Size (8 bits)
    0x95, 0x3F,  //   Report Count (63 bytes)
    0xB1, 0x02,  //   Feature
    // Report ID 2: PS5 Config
    0x85, 0x02,  //   Report ID (2)
    0x09, 0x02,  //   Usage (2)
    0x75, 0x08,  //   Report Size (8 bits)
    0x95, 0x3F,  //   Report Count (63 bytes)
    0xB1, 0x02,  //   Feature
    // Report ID 3: Link / Register Command
    0x85, 0x03,  //   Report ID (3)
    0x09, 0x03,  //   Usage (3)
    0x75, 0x08,  //   Report Size (8 bits)
    0x95, 0x3F,  //   Report Count (63 bytes)
    0xB1, 0x02,  //   Feature
    // Report ID 4: Status. Pushed by the device as an Input report on every
    // status change.
    0x85, 0x04,  //   Report ID (4)
    0x09, 0x04,  //   Usage (4)
    0x75, 0x08,  //   Report Size (8 bits)
    0x95, 0x3F,  //   Report Count (63 bytes)
    0x81, 0x02,  //   Input (Data,Var,Abs)
    // Report ID 5: Status snapshot. Same content as Report ID 4, but as a
    // Feature report so a freshly opened config page can fetch the current
    // status immediately (a separate ID from the Input push above -- reusing
    // one ID for both types confused the HID class driver's report sizing).
    0x85, 0x05,  //   Report ID (5)
    0x09, 0x05,  //   Usage (5)
    0x75, 0x08,  //   Report Size (8 bits)
    0x95, 0x3F,  //   Report Count (63 bytes)
    0xB1, 0x02,  //   Feature
    0xC0,        // End Collection
};

const struct device* hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);

USBD_DEVICE_DEFINE(context, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0xCAFE, 0xBAFE);

USBD_DESC_LANG_DEFINE(desc_lang);
USBD_DESC_MANUFACTURER_DEFINE(desc_manufacturer, "Arasaka");
USBD_DESC_PRODUCT_DEFINE(desc_product, "Remote Play Controller");
USBD_DESC_SERIAL_NUMBER_DEFINE(desc_serial_number);

static const uint8_t attributes = 0;

USBD_CONFIGURATION_DEFINE(fs_config, attributes, 50, NULL);  // 50*2 mA = 100 mA
USBD_CONFIGURATION_DEFINE(hs_config, attributes, 50, NULL);  // 50*2 mA = 100 mA

/* ---------------------------------------------------------------------------
 * HID callbacks
 * -------------------------------------------------------------------------*/

static void iface_ready(const struct device* dev, const bool ready) {
    LOG_INF("USB HID Interface ready: %d", ready);
}

/* Computes and appends the trailing CRC-32, then prefixes the Report ID
 * byte GET_REPORT replies need (see the comment on get_report() below).
 * Returns the byte count get_report() should return: 1 (ID byte) + size. */
static int finish_get_report(uint8_t* buf, uint8_t id, void* rep, size_t size) {
    uint32_t calc_crc = crc32_ieee((const uint8_t*) rep, size - 4);
    sys_put_le32(calc_crc, (uint8_t*) rep + size - 4);

    buf[0] = id;
    memcpy(buf + 1, rep, size);
    return 1 + size;
}

/* Validates a SET_REPORT payload's length, trailing CRC-32, and version
 * byte (always the first byte of every report struct). Returns 0 if valid,
 * or the negative error code set_report() should return otherwise. */
static int verify_report_crc_and_version(const char* report_name, const uint8_t* payload, uint16_t len, size_t payload_size) {
    if (len < payload_size + 1) {
        LOG_ERR("SET_REPORT %s size mismatch: %u < %zu", report_name, len, payload_size + 1);
        return -EINVAL;
    }

    uint32_t calc_crc = crc32_ieee(payload, payload_size - 4);
    uint32_t recv_crc = sys_get_le32(payload + payload_size - 4);
    if (calc_crc != recv_crc) {
        LOG_WRN("SET_REPORT %s CRC-32 mismatch! Disregarding report. Calculated 0x%08x, Received 0x%08x",
            report_name, calc_crc, recv_crc);
        return -EBADMSG;
    }

    if (payload[0] != CONFIG_REPORT_VERSION) {
        LOG_WRN("Incompatible config version: %u", payload[0]);
        return -EINVAL;
    }

    return 0;
}

static int get_report(const struct device* dev,
    const uint8_t type,
    const uint8_t id,
    const uint16_t len,
    uint8_t* const buf) {
    LOG_INF("get_report: type=%u, id=%u, len=%u", type, id, len);

    const AppConfig* cfg = settings_get_config();

    if (id == REPORT_ID_NET) {
        if (len < 1 + sizeof(struct app_config_report_net)) {
            return -EINVAL;
        }
        struct app_config_report_net rep;
        memset(&rep, 0, sizeof(rep));
        rep.version = CONFIG_REPORT_VERSION;
        strncpy(rep.wifi_ssid, cfg->wifi_ssid, sizeof(rep.wifi_ssid));
        // Wi-Fi password is intentionally left zeroed out

        return finish_get_report(buf, REPORT_ID_NET, &rep, sizeof(rep));
    } else if (id == REPORT_ID_PS5) {
        if (len < 1 + sizeof(struct app_config_report_ps5)) {
            return -EINVAL;
        }
        struct app_config_report_ps5 rep;
        memset(&rep, 0, sizeof(rep));
        rep.version = CONFIG_REPORT_VERSION;
        memcpy(rep.account_id, cfg->account_id, sizeof(rep.account_id));

        int ip_parts[4] = { 0 };
        sscanf(cfg->ps5_ip_str, "%d.%d.%d.%d", &ip_parts[0], &ip_parts[1], &ip_parts[2], &ip_parts[3]);
        for (int i = 0; i < 4; i++) {
            rep.ps5_ip[i] = (uint8_t) ip_parts[i];
        }

        return finish_get_report(buf, REPORT_ID_PS5, &rep, sizeof(rep));
    } else if (id == REPORT_ID_STATUS_FEATURE) {
        if (len < 1 + sizeof(struct app_status_report)) {
            return -EINVAL;
        }
        struct app_status_report rep;
        status_get_report(&rep);
        buf[0] = REPORT_ID_STATUS_FEATURE;
        memcpy(buf + 1, &rep, sizeof(rep));
        return 1 + sizeof(rep);
    }

    return -ENOTSUP;
}

static int set_report(const struct device* dev, const uint8_t type, const uint8_t id, const uint16_t len, const uint8_t* const buf) {
    LOG_INF("set_report: type=%u, id=%u, len=%u", type, id, len);

    if (id == REPORT_ID_NET) {
        /* buf[0] is the report ID; skip it */
        const uint8_t* payload = buf + 1;
        const size_t payload_size = sizeof(struct app_config_report_net);

        int err = verify_report_crc_and_version("NET", payload, len, payload_size);
        if (err) {
            return err;
        }

        const struct app_config_report_net* rep = (const struct app_config_report_net*) payload;

        k_mutex_lock(&s_cfg_mutex, K_FOREVER);
        const AppConfig old_cfg = s_pending_cfg;

        memcpy(s_pending_cfg.wifi_ssid, rep->wifi_ssid, sizeof(rep->wifi_ssid));
        s_pending_cfg.wifi_ssid[sizeof(s_pending_cfg.wifi_ssid) - 1] = '\0';

        // Update password only if non-empty
        if (rep->wifi_password[0] != '\0') {
            memcpy(s_pending_cfg.wifi_password, rep->wifi_password, sizeof(rep->wifi_password));
            s_pending_cfg.wifi_password[sizeof(s_pending_cfg.wifi_password) - 1] = '\0';
        }

        bool wifi_changed = strcmp(s_pending_cfg.wifi_ssid, old_cfg.wifi_ssid) != 0 ||
                            strcmp(s_pending_cfg.wifi_password, old_cfg.wifi_password) != 0;
        s_cfg_save_pending = true;
        k_mutex_unlock(&s_cfg_mutex);

        LOG_INF("Valid SET_REPORT NET received. Deferring save to main thread.");
        if (wifi_changed) {
            s_wifi_changed = true;
        } else {
            LOG_INF("Wi-Fi credentials unchanged; not reconnecting.");
        }
        k_poll_signal_raise(&s_config_signal, 0);
        return 0;

    } else if (id == REPORT_ID_PS5) {
        /* buf[0] is the report ID; skip it */
        const uint8_t* payload = buf + 1;
        const size_t payload_size = sizeof(struct app_config_report_ps5);

        int err = verify_report_crc_and_version("PS5", payload, len, payload_size);
        if (err) {
            return err;
        }

        const struct app_config_report_ps5* rep = (const struct app_config_report_ps5*) payload;

        k_mutex_lock(&s_cfg_mutex, K_FOREVER);
        const AppConfig old_cfg = s_pending_cfg;

        memcpy(s_pending_cfg.account_id, rep->account_id, sizeof(rep->account_id));

        snprintf(s_pending_cfg.ps5_ip_str, sizeof(s_pending_cfg.ps5_ip_str), "%u.%u.%u.%u",
            rep->ps5_ip[0], rep->ps5_ip[1], rep->ps5_ip[2], rep->ps5_ip[3]);

        bool ps5_cfg_changed = memcmp(s_pending_cfg.account_id, old_cfg.account_id, sizeof(s_pending_cfg.account_id)) != 0 ||
                               strcmp(s_pending_cfg.ps5_ip_str, old_cfg.ps5_ip_str) != 0;
        s_cfg_save_pending = true;
        k_mutex_unlock(&s_cfg_mutex);

        LOG_INF("Valid SET_REPORT PS5 received. Deferring save to main thread.");
        if (ps5_cfg_changed) {
            s_ps5_cfg_changed = true;
        } else {
            LOG_INF("PS5 configuration unchanged; not restarting session.");
        }
        k_poll_signal_raise(&s_config_signal, 0);
        return 0;

    } else if (id == REPORT_ID_REGISTER) {
        /* buf[0] is the report ID; skip it */
        const uint8_t* payload = buf + 1;
        const size_t payload_size = sizeof(struct app_config_report_register);

        int err = verify_report_crc_and_version("REGISTER", payload, len, payload_size);
        if (err) {
            return err;
        }

        const struct app_config_report_register* rep = (const struct app_config_report_register*) payload;

        uint32_t pin = sys_get_le32((const uint8_t*) &rep->pin);
        LOG_INF("Received Link command with PIN %08u. Deferring registration to main thread.", pin);

        /* Do NOT call registration_run() here - this callback runs in
         * USB stack context with limited stack. Signal main thread instead. */
        s_pending_pin = pin;
        k_sem_give(&s_link_sem);
        return 0;
    }

    return -ENOTSUP;
}

static void output_report(const struct device* dev, const uint16_t len, const uint8_t* const buf) {
    LOG_HEXDUMP_DBG(buf, len, "");
}

static void input_report_done(const struct device* dev, const uint8_t* const report) {
}

static struct hid_device_ops ops = {
    .iface_ready = iface_ready,
    .get_report = get_report,
    .set_report = set_report,
    .output_report = output_report,
    .input_report_done = input_report_done,
};

static void usbd_msg_cb(struct usbd_context* const ctx, const struct usbd_msg* msg) {
    switch (msg->type) {
        case USBD_MSG_VBUS_READY:
            LOG_INF("USBD_MSG_VBUS_READY");
            CHK(usbd_enable(ctx));
            break;
        case USBD_MSG_VBUS_REMOVED:
            LOG_INF("USBD_MSG_VBUS_REMOVED");
            CHK(usbd_disable(ctx));
            break;
        default:
            break;
    }
}

static bool initialize_usb() {
    if (!device_is_ready(hid_dev)) {
        LOG_ERR("hid_dev not ready");
        return false;
    }

    if (!CHK(hid_device_register(hid_dev, report_descriptor, sizeof(report_descriptor), &ops))) {
        return false;
    }

    if (!CHK(usbd_add_descriptor(&context, &desc_lang))) {
        return false;
    }

    if (!CHK(usbd_add_descriptor(&context, &desc_manufacturer))) {
        return false;
    }

    if (!CHK(usbd_add_descriptor(&context, &desc_product))) {
        return false;
    }

    if (!CHK(usbd_add_descriptor(&context, &desc_serial_number))) {
        return false;
    }

    if (USBD_SUPPORTS_HIGH_SPEED && usbd_caps_speed(&context) == USBD_SPEED_HS) {
        if (!CHK(usbd_add_configuration(&context, USBD_SPEED_HS, &hs_config))) {
            return false;
        }

        if (!CHK(usbd_register_class(&context, "hid_0", USBD_SPEED_HS, 1))) {
            return false;
        }

        usbd_device_set_code_triple(&context, USBD_SPEED_HS, 0, 0, 0);
    }

    if (!CHK(usbd_add_configuration(&context, USBD_SPEED_FS, &fs_config))) {
        return false;
    }

    if (!CHK(usbd_register_class(&context, "hid_0", USBD_SPEED_FS, 1))) {
        return false;
    }

    usbd_device_set_code_triple(&context, USBD_SPEED_FS, 0, 0, 0);

    usbd_self_powered(&context, false);

    if (!CHK(usbd_msg_register_cb(&context, usbd_msg_cb))) {
        return false;
    }

    if (!CHK(usbd_init(&context))) {
        return false;
    }

    if (!usbd_can_detect_vbus(&context)) {
        if (!CHK(usbd_enable(&context))) {
            return false;
        }
    }

    return true;
}

/* ---------------------------------------------------------------------------
 * State machine helpers
 * -------------------------------------------------------------------------*/

static void set_state(AppState* state, AppState next) {
    if (*state != next) {
        LOG_INF("State: %s -> %s", state_name(*state), state_name(next));
        *state = next;
    }
}

/* Stop and tear down a session if one is in flight. */
static void teardown_session(AppState* state) {
    if (*state == APP_STATE_SESSION_CONNECTING ||
        *state == APP_STATE_SESSION_RUNNING) {
        LOG_INF("Tearing down active session...");
        session_fini();
        /* A couple of call sites log a STATUS_LOG line (which refreshes the
         * LED) just before calling this, so refresh again now that the
         * session is actually gone -- otherwise the LED can briefly keep
         * showing the "PS5 connected" pattern into a state where it no
         * longer applies. */
        led_status_refresh();
    }
}

#define WIFI_RETRY_INTERVAL_SECONDS 5

/* Blocks until Wi-Fi connects or the driver reports the password as
 * definitively wrong. Any other failure (AP not found, generic failure,
 * timeout) is treated as transient and retried forever every few seconds --
 * a flaky AP shouldn't require power-cycling the controller.
 *
 * Re-reads the live config on every attempt, so credentials pushed via
 * WebHID while this is retrying take effect on the next attempt without
 * needing a reboot.
 *
 * Returns true if connected, false if we gave up (wrong password, or the
 * SSID got cleared out from under us).
 */
static bool connect_wifi_with_retry(void) {
    while (1) {
        const AppConfig* cfg = settings_get_config();
        if (cfg->wifi_ssid[0] == '\0') {
            return false;
        }

        STATUS_LOG("Connecting to Wi-Fi \"%s\"...", cfg->wifi_ssid);
        int ret = wifi_manager_init_and_connect(cfg->wifi_ssid, cfg->wifi_password);
        if (ret == 0) {
            STATUS_LOG("Wi-Fi connected.");
            return true;
        }

        if (ret == -EACCES) {
            STATUS_LOG("Wi-Fi connection failed: wrong password. Update credentials via the config website.");
            return false;
        }

        STATUS_LOG("Wi-Fi connection failed (%d). Retrying in %d s...", ret, WIFI_RETRY_INTERVAL_SECONDS);
        k_sleep(K_SECONDS(WIFI_RETRY_INTERVAL_SECONDS));
    }
}

/* Connects to Wi-Fi (retrying as above) and transitions state on the
 * outcome: SESSION_STARTING/UNREGISTERED on success (depending on whether
 * we're already registered), NO_WIFI on failure. If no SSID is configured,
 * logs no_ssid_message and goes straight to NO_WIFI. */
static void connect_wifi_and_transition(AppState* state, const char* no_ssid_message) {
    const AppConfig* cfg = settings_get_config();
    if (cfg->wifi_ssid[0] == '\0') {
        STATUS_LOG("%s", no_ssid_message);
        set_state(state, APP_STATE_NO_WIFI);
        return;
    }

    set_state(state, APP_STATE_WIFI_CONNECTING);
    if (connect_wifi_with_retry()) {
        set_state(state, settings_has_registration()
                             ? APP_STATE_SESSION_STARTING
                             : APP_STATE_UNREGISTERED);
    } else {
        set_state(state, APP_STATE_NO_WIFI);
    }
}

/* ---------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/

int main(void) {
    LOG_INF("================================");
    LOG_INF("   PS5 Remote Play Controller   ");
    LOG_INF("================================");

    int ret = app_settings_init();
    if (ret != 0) {
        LOG_ERR("app_settings_init failed: %d", ret);
        return ret;
    }

    /* Seeds the config-save staging buffer from whatever was just loaded
     * from flash. set_report() only ever merges onto it from here on --
     * never re-reads settings_get_config() -- so it has no need to touch
     * s_app_config at all, and the main thread is free to save outside
     * the lock without any risk of a set_report() reseeding from a save
     * that's still in flight. */
    s_pending_cfg = *settings_get_config();

    if (!initialize_usb()) {
        LOG_ERR("initialize_usb() failed");
        return -1;
    }

    status_init(hid_dev);

    ChiakiErrorCode lib_err = chiaki_lib_init();
    if (lib_err != CHIAKI_ERR_SUCCESS) {
        LOG_ERR("chiaki_lib_init: %s (%d)", chiaki_error_string(lib_err), lib_err);
        return -1;
    }

    /* Initialise the controller state to all-idle */
    chiaki_controller_state_set_idle(&g_controller_state);

    buttons_init(&g_controller_state, &g_input_sem);

    /* -----------------------------------------------------------------------
     * k_poll event table.
     *
     * EVT_CONFIG  - k_poll_signal raised by set_report on wifi/ps5 changes
     * EVT_LINK    - semaphore given by set_report when Link button is clicked
     * EVT_SESSION - semaphore given by session module on CONNECTED or QUIT
     * EVT_INPUT   - semaphore given by GPIO code when controller state changes
     *
     * AUTO_RESET: for semaphores the count is decremented when the event fires;
     * for signals the signaled flag is cleared.  This means each event is
     * consumed exactly once per poll iteration.
     * ---------------------------------------------------------------------*/
    enum { EVT_CONFIG,
        EVT_LINK,
        EVT_SESSION,
        EVT_INPUT,
        EVT_COUNT };
    /* K_POLL_MODE_NOTIFY_ONLY: k_poll wakes us but does NOT consume the
     * semaphore / signal automatically.  We consume manually below so we
     * stay in control of exactly when each resource is taken. */
    struct k_poll_event events[EVT_COUNT] = {
        [EVT_CONFIG] = K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
            K_POLL_MODE_NOTIFY_ONLY, &s_config_signal),
        [EVT_LINK] = K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
            K_POLL_MODE_NOTIFY_ONLY, &s_link_sem),
        [EVT_SESSION] = K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
            K_POLL_MODE_NOTIFY_ONLY, &session_event_sem),
        [EVT_INPUT] = K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
            K_POLL_MODE_NOTIFY_ONLY, &g_input_sem),
    };

    /* -----------------------------------------------------------------------
     * Initial state: try to connect to WiFi if credentials are available.
     * ---------------------------------------------------------------------*/
    AppState state = APP_STATE_NO_WIFI;
    connect_wifi_and_transition(&state, "Waiting for Wi-Fi credentials.");

    /* -----------------------------------------------------------------------
     * Main loop
     * ---------------------------------------------------------------------*/
    while (1) {
        /* --- 1. Compute poll timeout --------------------------------------- */
        k_timeout_t timeout = K_FOREVER;
        if (state == APP_STATE_SESSION_STARTING) {
            /* Don't sleep - the switch case below needs to run immediately
             * to call session_init(). */
            timeout = K_NO_WAIT;
        }

        /* --- 2. Reset event states and poll -------------------------------- */
        for (int i = 0; i < EVT_COUNT; i++) {
            events[i].state = K_POLL_STATE_NOT_READY;
        }
        k_poll(events, EVT_COUNT, timeout);

        /* --- 3. WiFi / PS5 config changed ---------------------------------- */
        if (events[EVT_CONFIG].state == K_POLL_STATE_SIGNALED) {
            /* Manually consume the signal */
            s_config_signal.signaled = 0;
            s_config_signal.result = 0;

            /* Just a snapshot -- safe to release the lock before the save
             * itself, since set_report() never reads settings_get_config()
             * and so can't reseed from a save that's still in flight. */
            k_mutex_lock(&s_cfg_mutex, K_FOREVER);
            bool save_pending = s_cfg_save_pending;
            AppConfig cfg_to_save = s_pending_cfg;
            s_cfg_save_pending = false;
            k_mutex_unlock(&s_cfg_mutex);

            if (save_pending) {
                LOG_INF("Saving pending configuration to NVS...");
                settings_save_config(&cfg_to_save);
            }

            if (s_wifi_changed) {
                s_wifi_changed = false;

                STATUS_LOG("Wi-Fi credentials changed. Reconnecting...");
                teardown_session(&state);
                wifi_manager_disconnect();

                connect_wifi_and_transition(&state, "Wi-Fi SSID cleared - not connecting.");
            }

            if (s_ps5_cfg_changed) {
                s_ps5_cfg_changed = false;
                if (state == APP_STATE_SESSION_CONNECTING || state == APP_STATE_SESSION_RUNNING) {
                    STATUS_LOG("PS5 configuration changed - restarting session.");
                } else {
                    STATUS_LOG("PS5 configuration changed.");
                }
                teardown_session(&state);
                set_state(&state, APP_STATE_SESSION_STARTING);
            }
        }

        /* --- 4. Link (registration) request -------------------------------- */
        if (events[EVT_LINK].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&s_link_sem, K_NO_WAIT);
            uint32_t pin = s_pending_pin;
            STATUS_LOG("Linking with PS5 (PIN %08u)...", pin);
            teardown_session(&state);
            set_state(&state, APP_STATE_REGISTERING);
            ret = registration_run(pin);
            if (ret == 0) {
                STATUS_LOG("Linked with PS5 successfully.");
                set_state(&state, APP_STATE_SESSION_STARTING);
            } else {
                STATUS_LOG("Linking failed (%d). Check the PIN and try again.", ret);
                set_state(&state, APP_STATE_UNREGISTERED);
            }
        }

        /* --- 5. Session connected / quit event ----------------------------- */
        if (events[EVT_SESSION].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&session_event_sem, K_NO_WAIT);
            if (session_is_connected()) {
                STATUS_LOG("Connected to PS5. Streaming controller input.");
                set_state(&state, APP_STATE_SESSION_RUNNING);
            } else {
                /* Session dropped - clean up and schedule a reconnect */
                STATUS_LOG("Session with PS5 dropped. Retrying in 5 s...");
                session_fini();
                k_sleep(K_SECONDS(5));
                set_state(&state, APP_STATE_SESSION_STARTING);
            }
        }

        /* --- 6. Controller input ------------------------------------------- */
        if (events[EVT_INPUT].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&g_input_sem, K_NO_WAIT);
            if (state == APP_STATE_SESSION_RUNNING) {
                session_send_controller_state(&g_controller_state);
            }
        }

        /* --- 7. State-driven transitions ----------------------------------- */
        switch (state) {
            case APP_STATE_NO_WIFI:
                /* Nothing to do - waiting for s_wifi_changed via EVT_CONFIG */
                break;

            case APP_STATE_WIFI_CONNECTING:
                /* wifi_manager_init_and_connect() was called synchronously above;
                 * we should never idle in this state. */
                break;

            case APP_STATE_UNREGISTERED:
                /* Waiting for EVT_LINK - k_poll will sleep until it arrives */
                break;

            case APP_STATE_REGISTERING:
                /* registration_run() was called synchronously above */
                break;

            case APP_STATE_SESSION_STARTING:
                if (wifi_manager_is_connected() &&
                    settings_has_registration() &&
                    settings_get_config()->ps5_ip_str[0] != '\0') {
                    STATUS_LOG("Starting PS5 Remote Play session...");
                    ret = session_init();
                    if (ret == 0) {
                        set_state(&state, APP_STATE_SESSION_CONNECTING);
                    } else {
                        STATUS_LOG("Failed to start session (%d). Retrying in 5 s...", ret);
                        k_sleep(K_SECONDS(5));
                        /* stay in SESSION_STARTING to retry */
                    }
                } else {
                    LOG_WRN("Cannot start session yet: wifi=%d reg=%d ip=%d",
                        wifi_manager_is_connected(),
                        settings_has_registration(),
                        settings_get_config()->ps5_ip_str[0] != '\0');
                    set_state(&state, wifi_manager_is_connected()
                                          ? APP_STATE_UNREGISTERED
                                          : APP_STATE_NO_WIFI);
                }
                break;

            case APP_STATE_SESSION_CONNECTING:
                /* Waiting for EVT_SESSION (CONNECTED or early QUIT) */
                break;

            case APP_STATE_SESSION_RUNNING:
                /* Waiting for EVT_SESSION (QUIT) or EVT_INPUT */
                break;
        }
    }

    return 0;
}
