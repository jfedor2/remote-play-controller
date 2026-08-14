// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#ifndef SESSION_H
#define SESSION_H

#include <chiaki/controller.h>
#include <zephyr/kernel.h>

/**
 * @brief Start the PS5 remote play session.
 *
 * Non-blocking: initialises the Chiaki session and starts its internal
 * threads, then returns immediately.  The caller must poll
 * session_event_sem and call session_is_connected() to learn when
 * the connection is established or lost.
 *
 * @return 0 on success, negative errno on failure.
 */
int session_init(void);

/**
 * @brief Stop and tear down the current session.
 *
 * Signals Chiaki to quit and blocks until its threads have exited.
 * Safe to call even if the session never connected.
 */
void session_fini(void);

/**
 * @brief Send a controller state snapshot to the PS5.
 *
 * No-op if not currently connected.
 */
void session_send_controller_state(const ChiakiControllerState* state);

/**
 * @brief Return true while the session is connected.
 */
bool session_is_connected(void);

/**
 * @brief Semaphore given by the session event callback on both CONNECTED and
 * QUIT events.  The main loop polls this to react to connection state changes
 * without busy-waiting.
 */
extern struct k_sem session_event_sem;

#endif  // SESSION_H
