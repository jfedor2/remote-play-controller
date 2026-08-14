// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#ifndef STATUS_H
#define STATUS_H

#include <zephyr/device.h>

#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Give the status module the HID device to push Input Report 4 on.
 * Must be called once before status_logf().
 */
void status_init(const struct device* hid_dev);

/**
 * Record a new human-readable status line and push it to the host as
 * Input Report 4. Flags (STATUS_FLAG_*) are derived automatically from
 * current Wi-Fi / registration / session state.
 */
void status_logf(const char* fmt, ...);

/**
 * Copy the last status into a wire-format app_status_report, for serving
 * Feature Report 5 GET_REPORT requests.
 */
void status_get_report(struct app_status_report* out);

#ifdef __cplusplus
}
#endif

#endif  // STATUS_H
