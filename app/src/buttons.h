// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#ifndef BUTTONS_H
#define BUTTONS_H

#include <chiaki/controller.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configure all GPIOs declared under the "gamepad-buttons" devicetree node
 * and start polling them.
 *
 * Whenever the polled button state differs from *state, updates its
 * `buttons`/`l2_state`/`r2_state` fields and gives input_sem -- matching the
 * "written by GPIO code" / "given by GPIO code" contract documented next to
 * g_controller_state / g_input_sem in main.c.
 *
 * Any button name (south/east/west/north/dpad-left/dpad-right/dpad-up/
 * dpad-down/l1/r1/l2/r2/l3/r3/select/start/home/button14) that isn't
 * present under gamepad-buttons on a given board is simply always "not
 * pressed" -- boards don't all need to define the same set of buttons.
 */
void buttons_init(ChiakiControllerState* state, struct k_sem* input_sem);

#ifdef __cplusplus
}
#endif

#endif  // BUTTONS_H
