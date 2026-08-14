// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#include "session.h"
#include "chiaki_log.h"
#include "settings.h"
#include "status.h"

#include <chiaki/common.h>
#include <chiaki/log.h>
#include <chiaki/session.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stdint.h>
#include <string.h>

LOG_MODULE_REGISTER(session, LOG_LEVEL_DBG);

/* Given by session_event_cb on both CONNECTED and QUIT so the main loop can
 * react without polling.  The count is capped at 1; if the main loop misses
 * an event it will catch up on the next iteration. */
K_SEM_DEFINE(session_event_sem, 0, 1);

static bool s_connected = false;
static ChiakiSession s_session;
static ChiakiLog s_log;

static void session_event_cb(ChiakiEvent* event, void* user) {
    (void) user;
    switch (event->type) {
        case CHIAKI_EVENT_CONNECTED:
            LOG_INF("=================================================");
            LOG_INF("   PS5 SESSION CONNECTED SUCCESSFULLY!          ");
            LOG_INF("=================================================");
            s_connected = true;
            k_sem_give(&session_event_sem);
            break;
        case CHIAKI_EVENT_QUIT:
            LOG_WRN("PS5 session QUIT. reason=%d%s%s",
                event->quit.reason,
                event->quit.reason_str ? ": " : "",
                event->quit.reason_str ? event->quit.reason_str : "");
            s_connected = false;
            k_sem_give(&session_event_sem);
            break;
        default:
            LOG_DBG("Session event type=%d", event->type);
            break;
    }
}

int session_init(void) {
    const AppConfig* cfg = settings_get_config();

    if (cfg->ps5_ip_str[0] == '\0') {
        LOG_ERR("No PS5 IP address configured.");
        return -EINVAL;
    }
    if (!settings_has_account_id()) {
        LOG_ERR("No PSN Account ID configured.");
        return -EINVAL;
    }
    if (!settings_has_registration()) {
        LOG_ERR("No saved registration found in flash.");
        return -EINVAL;
    }

    const ChiakiRegisteredHost* reg_host = settings_get_registered_host();

    ChiakiConnectInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.ps5 = true;
    ci.host = cfg->ps5_ip_str;
    memcpy(ci.regist_key, reg_host->rp_regist_key, sizeof(ci.regist_key));
    memcpy(ci.morning, reg_host->rp_key, sizeof(ci.morning));
    settings_get_account_id_reversed(ci.psn_account_id);
    ci.audio_video_disabled = CHIAKI_AUDIO_VIDEO_DISABLED;
    ci.enable_keyboard = false;
    ci.enable_dualsense = true;
    ci.auto_regist = false;
    chiaki_connect_video_profile_preset(&ci.video_profile,
        CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
        CHIAKI_VIDEO_FPS_PRESET_60);

    chiaki_log_init(&s_log, CHIAKI_LOG_ALL, chiaki_log_zephyr_cb, (void*) (intptr_t) false);

    memset(&s_session, 0, sizeof(s_session));
    s_connected = false;
    /* Reset so a stale signal from a previous session is discarded */
    k_sem_reset(&session_event_sem);

    LOG_INF("Initialising Chiaki session (target: %s)...", cfg->ps5_ip_str);
    ChiakiErrorCode err = chiaki_session_init(&s_session, &ci, &s_log);
    if (err != CHIAKI_ERR_SUCCESS) {
        status_logf("chiaki_session_init failed: %s (%d)", chiaki_error_string(err), err);
        return -EIO;
    }

    chiaki_session_set_event_cb(&s_session, session_event_cb, NULL);

    err = chiaki_session_start(&s_session);
    if (err != CHIAKI_ERR_SUCCESS) {
        status_logf("chiaki_session_start failed: %s (%d)", chiaki_error_string(err), err);
        chiaki_session_fini(&s_session);
        return -EIO;
    }

    LOG_INF("Chiaki session started; waiting for CONNECTED event...");
    return 0;
}

void session_fini(void) {
    LOG_INF("Stopping Chiaki session...");
    s_connected = false;
    chiaki_session_stop(&s_session);
    chiaki_session_join(&s_session);
    chiaki_session_fini(&s_session);
    LOG_INF("Chiaki session stopped.");
}

bool session_is_connected(void) {
    return s_connected;
}

void session_send_controller_state(const ChiakiControllerState* state) {
    if (s_connected) {
        chiaki_session_set_controller_state(&s_session, state);
    }
}
