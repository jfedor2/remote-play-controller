// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#ifndef CHIAKI_LOG_ZEPHYR_H
#define CHIAKI_LOG_ZEPHYR_H

#include <chiaki/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ChiakiLog callback that routes ERROR/WARNING/INFO to the corresponding
 * Zephyr log level. CHIAKI_LOG_VERBOSE/CHIAKI_LOG_DEBUG go to LOG_INF
 * (tagged "[Chiaki Verbose]") if the ChiakiLog user cookie is (void*)true,
 * or LOG_DBG (tagged "[Chiaki]") if it's (void*)false -- pass whichever
 * suits the call site's verbosity needs to chiaki_log_init().
 */
void chiaki_log_zephyr_cb(ChiakiLogLevel level, const char* msg, void* user);

#ifdef __cplusplus
}
#endif

#endif  // CHIAKI_LOG_ZEPHYR_H
