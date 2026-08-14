// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#ifndef LED_STATUS_H
#define LED_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Re-derive the "led0" blink pattern from current Wi-Fi / PS5 session state
 * and apply it:
 *   - not connected to Wi-Fi:              100ms on / 100ms off
 *   - connected to Wi-Fi, no PS5 session:  1800ms on / 200ms off
 *   - PS5 session active:                  100ms on / 1900ms off
 *
 * Cheap enough to call after every state-machine transition; a dedicated
 * thread does the actual blinking, so this just tells it what pattern to
 * use next.
 */
void led_status_refresh(void);

#ifdef __cplusplus
}
#endif

#endif  // LED_STATUS_H
