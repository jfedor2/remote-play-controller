// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#include "settings.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(settings, LOG_LEVEL_INF);

static ChiakiRegisteredHost s_registered_host;
static bool s_has_registered_host = false;

static AppConfig s_app_config;
static bool s_has_app_config = false;

static int settings_set_cb(const char* name, size_t len, settings_read_cb read_cb, void* cb_arg) {
    const char* next;

    if (settings_name_steq(name, "host", &next) && !next) {
        if (len == sizeof(ChiakiRegisteredHost)) {
            int rc = read_cb(cb_arg, &s_registered_host, sizeof(s_registered_host));
            if (rc >= 0) {
                s_has_registered_host = true;
                LOG_INF("Loaded PS5 registration from settings NVS!");
                return 0;
            }
        } else {
            LOG_WRN("Size mismatch loading ps5/host: stored %zu, expected %zu",
                len, sizeof(ChiakiRegisteredHost));
        }
    } else if (settings_name_steq(name, "config", &next) && !next) {
        if (len == sizeof(AppConfig)) {
            int rc = read_cb(cb_arg, &s_app_config, sizeof(s_app_config));
            if (rc >= 0) {
                s_has_app_config = true;
                LOG_INF("Loaded AppConfig from settings NVS! SSID='%s', IP='%s'",
                    s_app_config.wifi_ssid, s_app_config.ps5_ip_str);
                return 0;
            }
        } else {
            LOG_WRN("Size mismatch loading ps5/config: stored %zu, expected %zu",
                len, sizeof(AppConfig));
        }
    }

    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(app_settings, "ps5", NULL, settings_set_cb, NULL, NULL);

int app_settings_init(void) {
    memset(&s_app_config, 0, sizeof(s_app_config));

    int err = settings_subsys_init();
    if (err) {
        LOG_ERR("settings_subsys_init failed: %d", err);
        return err;
    }

    err = settings_load_subtree("ps5");
    if (err) {
        LOG_WRN("settings_load_subtree ps5 returned: %d", err);
    }

    if (s_has_app_config) {
        LOG_INF("Using user configuration from flash settings NVS.");
    } else {
        LOG_INF("No saved user configuration found in flash settings.");
    }

    if (s_has_registered_host) {
        LOG_INF("Found saved PS5 registration key in flash settings.");
    } else {
        LOG_INF("No saved PS5 registration key found in flash settings.");
    }

    return 0;
}

const AppConfig* settings_get_config(void) {
    return &s_app_config;
}

int settings_save_config(const AppConfig* config) {
    if (!config) {
        return -EINVAL;
    }

    memcpy(&s_app_config, config, sizeof(AppConfig));
    s_has_app_config = true;

    int rc = settings_save_one("ps5/config", config, sizeof(AppConfig));
    if (rc == 0) {
        LOG_INF("Successfully saved AppConfig to flash settings NVS!");
    } else {
        LOG_ERR("Failed to save AppConfig to flash settings: %d", rc);
    }

    return rc;
}

bool settings_has_registration(void) {
    return s_has_registered_host;
}

bool settings_has_account_id(void) {
    for (size_t i = 0; i < sizeof(s_app_config.account_id); i++) {
        if (s_app_config.account_id[i] != 0) {
            return true;
        }
    }
    return false;
}

void settings_get_account_id_reversed(uint8_t* out) {
    size_t size = sizeof(s_app_config.account_id);
    for (size_t i = 0; i < size; i++) {
        out[i] = s_app_config.account_id[size - 1 - i];
    }
}

const ChiakiRegisteredHost* settings_get_registered_host(void) {
    return s_has_registered_host ? &s_registered_host : NULL;
}

int settings_save_registration(const ChiakiRegisteredHost* host) {
    if (!host) {
        return -EINVAL;
    }

    memcpy(&s_registered_host, host, sizeof(ChiakiRegisteredHost));
    s_has_registered_host = true;

    int rc = settings_save_one("ps5/host", host, sizeof(ChiakiRegisteredHost));
    if (rc == 0) {
        LOG_INF("Successfully saved PS5 registration key and host info to flash settings!");
    } else {
        LOG_ERR("Failed to save PS5 registration key to flash settings: %d", rc);
    }

    return rc;
}

int settings_clear_registration(void) {
    s_has_registered_host = false;
    memset(&s_registered_host, 0, sizeof(s_registered_host));

    int rc = settings_delete("ps5/host");
    if (rc == 0) {
        LOG_INF("Deleted PS5 registration key from flash settings.");
    } else {
        LOG_WRN("Failed to delete PS5 registration key from flash settings: %d", rc);
    }

    return rc;
}
