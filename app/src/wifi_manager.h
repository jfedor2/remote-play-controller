// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Connect to Wi-Fi network and wait for IPv4 address assignment.
 *
 * @param ssid Wi-Fi network name
 * @param psk  Wi-Fi password
 * @return 0 on success. -EACCES if the driver reported the password as
 *         specifically wrong (caller should stop retrying and wait for new
 *         credentials). Any other negative errno is a transient failure
 *         (AP not found, timeout, etc.) that the caller should retry.
 */
int wifi_manager_init_and_connect(const char* ssid, const char* psk);

/**
 * Disconnect from the current Wi-Fi network.
 *
 * Blocks until the disconnect event is confirmed (or a short timeout expires).
 */
void wifi_manager_disconnect(void);

/**
 * Check if Wi-Fi is connected and has an assigned IPv4 address.
 */
bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif  // WIFI_MANAGER_H
