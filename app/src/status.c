// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#include "status.h"

#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usbd_hid.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "session.h"
#include "settings.h"
#include "wifi_manager.h"

LOG_MODULE_REGISTER(status, LOG_LEVEL_INF);

static const struct device* s_hid_dev;
static struct app_status_report s_status;

static uint8_t s_input_frame[1 + sizeof(struct app_status_report)] __aligned(4);

static uint8_t current_flags(void) {
    uint8_t flags = 0;

    if (wifi_manager_is_connected()) {
        flags |= STATUS_FLAG_WIFI_CONNECTED;
    }
    if (settings_has_registration()) {
        flags |= STATUS_FLAG_HAS_REGISTRATION;
    }
    if (session_is_connected()) {
        flags |= STATUS_FLAG_SESSION_ACTIVE;
    }

    return flags;
}

void status_init(const struct device* hid_dev) {
    s_hid_dev = hid_dev;
    memset(&s_status, 0, sizeof(s_status));
    s_status.version = CONFIG_REPORT_VERSION;
}

void status_logf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_status.message, sizeof(s_status.message), fmt, args);
    va_end(args);

    s_status.version = CONFIG_REPORT_VERSION;
    s_status.flags = current_flags();

    LOG_INF("Status: %s", s_status.message);

    if (s_hid_dev == NULL) {
        return;
    }

    s_input_frame[0] = REPORT_ID_STATUS;
    memcpy(&s_input_frame[1], &s_status, sizeof(s_status));

    int ret = hid_device_submit_report(s_hid_dev, sizeof(s_input_frame), s_input_frame);
    if (ret != 0) {
        LOG_DBG("hid_device_submit_report failed: %d (host likely not listening yet)", ret);
    }
}

void status_get_report(struct app_status_report* out) {
    memcpy(out, &s_status, sizeof(s_status));
}
