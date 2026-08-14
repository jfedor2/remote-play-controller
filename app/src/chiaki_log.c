// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#include "chiaki_log.h"
#include "status.h"

#include <zephyr/logging/log.h>

#include <stdbool.h>
#include <stdint.h>

LOG_MODULE_REGISTER(chiaki, LOG_LEVEL_DBG);

void chiaki_log_zephyr_cb(ChiakiLogLevel level, const char* msg, void* user) {
    bool verbose_as_info = (bool) (intptr_t) user;
    switch (level) {
        case CHIAKI_LOG_ERROR:
            LOG_ERR("[Chiaki] %s", msg);
            /* Also visible on the config website regardless of whether
             * Zephyr logging is compiled in -- chiaki-ng's own internal
             * errors (e.g. "Ctrl init failed") otherwise only ever go
             * through LOG_ERR. */
            status_logf("[Chiaki] %s", msg);
            break;
        case CHIAKI_LOG_WARNING:
            LOG_WRN("[Chiaki] %s", msg);
            break;
        case CHIAKI_LOG_INFO:
            LOG_INF("[Chiaki] %s", msg);
            break;
        default:
            if (verbose_as_info) {
                LOG_INF("[Chiaki Verbose] %s", msg);
            } else {
                LOG_DBG("[Chiaki] %s", msg);
            }
            break;
    }
}
