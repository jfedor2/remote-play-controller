// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#include "registration.h"
#include "chiaki_log.h"
#include "settings.h"

#include <chiaki/common.h>
#include <chiaki/log.h>
#include <chiaki/regist.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(registration, LOG_LEVEL_DBG);

static K_SEM_DEFINE(s_regist_sem, 0, 1);
static ChiakiRegistEventType s_regist_event_type;
static ChiakiRegisteredHost s_registered_host;

static void regist_callback(ChiakiRegistEvent* event, void* user) {
    (void) user;
    s_regist_event_type = event->type;
    if (event->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS && event->registered_host) {
        memcpy(&s_registered_host, event->registered_host, sizeof(ChiakiRegisteredHost));
    }
    k_sem_give(&s_regist_sem);
}

int registration_run(uint32_t pin) {
    const AppConfig* cfg = settings_get_config();

    if (cfg->wifi_ssid[0] == '\0') {
        LOG_ERR("No Wi-Fi network configured! Please configure Wi-Fi via WebHID before linking.");
        return -EINVAL;
    }

    if (cfg->ps5_ip_str[0] == '\0') {
        LOG_ERR("No PS5 IP address configured! Please configure PS5 IP address via WebHID before linking.");
        return -EINVAL;
    }

    if (!settings_has_account_id()) {
        LOG_ERR("No PSN Account ID configured! Please configure PSN Account ID via WebHID before linking.");
        return -EINVAL;
    }

    if (pin == 0) {
        LOG_ERR("Invalid Registration PIN (0). Please enter a valid 8-digit PIN shown on PS5 screen.");
        return -EINVAL;
    }

    LOG_INF("=================================================");
    LOG_INF("Starting PS5 Registration Procedure");
    LOG_INF("Target Console IP : %s", cfg->ps5_ip_str);
    LOG_HEXDUMP_INF(cfg->account_id, sizeof(cfg->account_id), "PSN Account ID (8 raw bytes):");
    LOG_INF("Registration PIN  : %u", pin);
    LOG_INF("=================================================");

    uint8_t psn_account_id[CHIAKI_PSN_ACCOUNT_ID_SIZE];
    settings_get_account_id_reversed(psn_account_id);

    LOG_HEXDUMP_INF(psn_account_id, sizeof(psn_account_id), "Chiaki 8-Byte PSN Account ID:");

    ChiakiLog log;
    chiaki_log_init(&log, CHIAKI_LOG_ALL, chiaki_log_zephyr_cb, (void*) (intptr_t) true);

    ChiakiRegistInfo info;
    memset(&info, 0, sizeof(info));
    info.target = CHIAKI_TARGET_PS5_1;
    info.host = cfg->ps5_ip_str;
    info.broadcast = false;
    memcpy(info.psn_account_id, psn_account_id, sizeof(psn_account_id));
    info.pin = pin;

    ChiakiRegist regist;
    memset(&regist, 0, sizeof(regist));

    LOG_INF("Initiating Chiaki registration sequence...");
    ChiakiErrorCode err = chiaki_regist_start(&regist, &log, &info, regist_callback, NULL);
    if (err != CHIAKI_ERR_SUCCESS) {
        LOG_ERR("chiaki_regist_start failed with error: %s (%d)",
            chiaki_error_string(err), err);
        return -EIO;
    }

    LOG_INF("Waiting for registration process to complete...");
    k_sem_take(&s_regist_sem, K_FOREVER);

    chiaki_regist_fini(&regist);

    if (s_regist_event_type != CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS) {
        LOG_ERR("PS5 Registration failed or was canceled! Result event type: %d", s_regist_event_type);
        return -EIO;
    }

    LOG_INF("-------------------------------------------------");
    LOG_INF("PS5 REGISTRATION SUCCESSFUL!");
    LOG_INF("Server Nickname: %s", s_registered_host.server_nickname);
    LOG_HEXDUMP_INF(s_registered_host.server_mac, sizeof(s_registered_host.server_mac), "PS5 MAC Address:");
    LOG_HEXDUMP_INF(s_registered_host.rp_regist_key, sizeof(s_registered_host.rp_regist_key), "Registration Key (rp_regist_key):");
    LOG_HEXDUMP_INF(s_registered_host.rp_key, sizeof(s_registered_host.rp_key), "RP Key:");
    LOG_INF("-------------------------------------------------");

    int save_err = settings_save_registration(&s_registered_host);
    if (save_err != 0) {
        LOG_ERR("Failed to persist registration key to settings flash storage!");
        return save_err;
    }

    LOG_INF("Registration key persisted to flash NVS successfully.");
    return 0;
}
