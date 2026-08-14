// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#include "buttons.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(buttons, LOG_LEVEL_INF);

#define BUTTON_POLL_INTERVAL_MS 4

/* ---------------------------------------------------------------------------
 * Button GPIO setup, ported from portable-gamepad-firmware's main.c:
 *   - Every child of the "gamepad-buttons" devicetree node becomes one
 *     enum button_idx value and one struct gpio_dt_spec.
 *   - Buttons sharing a physical GPIO port are deduplicated so that port is
 *     only read once per poll (read_all_gpio_ports()) instead of once per
 *     pin; BUTTON_GET(name) then just pulls the bit for that pin out of the
 *     cached port-wide reading.
 *   - BUTTON_GET(name) compiles down to a constant `(0)` -- "never
 *     pressed" -- if "name" isn't a child of gamepad-buttons on the current
 *     board, so boards don't all need to define the same set of buttons.
 * -------------------------------------------------------------------------*/

#define BUTTON(name) UTIL_CAT(button_, name)

#define BUTTON_FOR_ID(node_id) BUTTON(DT_NODE_FULL_NAME_TOKEN(node_id))

enum button_idx {
    DT_FOREACH_CHILD_SEP(DT_PATH(gamepad_buttons), BUTTON_FOR_ID, (, )),
    NUM_BUTTONS
};

#define BUTTON_GPIO_DEF(node_id) GPIO_DT_SPEC_GET(node_id, gpios),

static struct gpio_dt_spec buttons[] = {
    DT_FOREACH_CHILD(DT_PATH(gamepad_buttons), BUTTON_GPIO_DEF)
};

static gpio_port_value_t* button_port_states[NUM_BUTTONS];

#define _COUNT_GPIO_CONTROLLER(node_id) IF_ENABLED(DT_NODE_HAS_PROP(node_id, gpio_controller), (+1))
#define NUM_GPIO_PORTS (0 DT_FOREACH_STATUS_OKAY_NODE(_COUNT_GPIO_CONTROLLER))

static const struct device* gpio_ports[NUM_GPIO_PORTS];
static gpio_port_value_t gpio_port_states[NUM_GPIO_PORTS];

static int s_active_gpio_ports = 0;

#define BUTTON_GET(name)                                                                                        \
    COND_CODE_1(DT_NODE_EXISTS(DT_PATH(gamepad_buttons, name)),                                                 \
        ((0 != (*button_port_states[BUTTON(name)] & BIT(DT_GPIO_PIN(DT_PATH(gamepad_buttons, name), gpios))))), \
        (0))

static void read_all_gpio_ports(void) {
    for (int i = 0; i < s_active_gpio_ports; i++) {
        gpio_port_get(gpio_ports[i], &gpio_port_states[i]);
    }
}

/* ---------------------------------------------------------------------------
 * Mapping onto ChiakiControllerState. south/east/west/north -> cross/moon
 * (circle)/box (square)/pyramid (triangle) matches chiaki-ng's own Xbox-
 * layout convention (a/b/x/y), e.g. controllermanager.cpp and
 * qmlbackend.cpp. l2/r2 are digital buttons here (no ADC), reported as
 * fully-pressed (255) or released (0) analog trigger state, same as other
 * digital-only controllers chiaki-ng talks to. button14 has no obvious PS5
 * equivalent, so it's mapped to the one otherwise-unused DualSense button,
 * the touchpad click.
 * -------------------------------------------------------------------------*/

static void buttons_apply(ChiakiControllerState* state) {
    read_all_gpio_ports();

    uint32_t b = 0;
    if (BUTTON_GET(south))
        b |= CHIAKI_CONTROLLER_BUTTON_CROSS;
    if (BUTTON_GET(east))
        b |= CHIAKI_CONTROLLER_BUTTON_MOON;
    if (BUTTON_GET(west))
        b |= CHIAKI_CONTROLLER_BUTTON_BOX;
    if (BUTTON_GET(north))
        b |= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
    if (BUTTON_GET(dpad_left))
        b |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
    if (BUTTON_GET(dpad_right))
        b |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
    if (BUTTON_GET(dpad_up))
        b |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
    if (BUTTON_GET(dpad_down))
        b |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
    if (BUTTON_GET(l1))
        b |= CHIAKI_CONTROLLER_BUTTON_L1;
    if (BUTTON_GET(r1))
        b |= CHIAKI_CONTROLLER_BUTTON_R1;
    if (BUTTON_GET(l3))
        b |= CHIAKI_CONTROLLER_BUTTON_L3;
    if (BUTTON_GET(r3))
        b |= CHIAKI_CONTROLLER_BUTTON_R3;
    if (BUTTON_GET(start))
        b |= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
    if (BUTTON_GET(select))
        b |= CHIAKI_CONTROLLER_BUTTON_SHARE;
    if (BUTTON_GET(home))
        b |= CHIAKI_CONTROLLER_BUTTON_PS;
    if (BUTTON_GET(button14))
        b |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;

    uint8_t l2 = BUTTON_GET(l2) ? 255 : 0;
    uint8_t r2 = BUTTON_GET(r2) ? 255 : 0;

    state->buttons = b;
    state->l2_state = l2;
    state->r2_state = r2;
}

/* ---------------------------------------------------------------------------
 * Polling thread
 * -------------------------------------------------------------------------*/

static ChiakiControllerState* s_state;
static struct k_sem* s_input_sem;

static void buttons_thread_fn(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        k_sleep(K_MSEC(BUTTON_POLL_INTERVAL_MS));

        if (s_state == NULL) {
            continue;
        }

        uint32_t prev_buttons = s_state->buttons;
        uint8_t prev_l2 = s_state->l2_state;
        uint8_t prev_r2 = s_state->r2_state;

        buttons_apply(s_state);

        if (s_state->buttons != prev_buttons ||
            s_state->l2_state != prev_l2 ||
            s_state->r2_state != prev_r2) {
            k_sem_give(s_input_sem);
        }
    }
}

K_THREAD_DEFINE(buttons_thread, 512, buttons_thread_fn, NULL, NULL, NULL, 7, 0, 0);

void buttons_init(ChiakiControllerState* state, struct k_sem* input_sem) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (!gpio_is_ready_dt(&buttons[i])) {
            LOG_ERR("Button %d GPIO not ready", i);
            continue;
        }

        int ret = gpio_pin_configure_dt(&buttons[i], GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW);
        if (ret != 0) {
            LOG_ERR("Failed to configure button %d: %d", i, ret);
        }

        int port_idx = -1;
        for (int j = 0; j < s_active_gpio_ports; j++) {
            if (gpio_ports[j] == buttons[i].port) {
                port_idx = j;
                break;
            }
        }

        if (port_idx == -1) {
            port_idx = s_active_gpio_ports;
            gpio_ports[port_idx] = buttons[i].port;
            s_active_gpio_ports++;
        }

        button_port_states[i] = &gpio_port_states[port_idx];
    }

    LOG_INF("%d button(s) across %d GPIO port(s)", NUM_BUTTONS, s_active_gpio_ports);

    s_state = state;
    s_input_sem = input_sem;
}
