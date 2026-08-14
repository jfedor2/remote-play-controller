// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#include "led_status.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "session.h"
#include "wifi_manager.h"

LOG_MODULE_REGISTER(led_status, LOG_LEVEL_INF);

enum led_pattern {
    LED_PATTERN_NO_WIFI,
    LED_PATTERN_WIFI_ONLY,
    LED_PATTERN_PS5_CONNECTED,
};

struct blink_timing {
    uint32_t on_ms;
    uint32_t off_ms;
};

static const struct blink_timing k_patterns[] = {
    [LED_PATTERN_NO_WIFI] = { .on_ms = 100, .off_ms = 100 },
    [LED_PATTERN_WIFI_ONLY] = { .on_ms = 1800, .off_ms = 200 },
    [LED_PATTERN_PS5_CONNECTED] = { .on_ms = 100, .off_ms = 1900 },
};

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static atomic_t s_pattern = ATOMIC_INIT(LED_PATTERN_NO_WIFI);

void led_status_refresh(void) {
    enum led_pattern pattern;

    if (!wifi_manager_is_connected()) {
        pattern = LED_PATTERN_NO_WIFI;
    } else if (!session_is_connected()) {
        pattern = LED_PATTERN_WIFI_ONLY;
    } else {
        pattern = LED_PATTERN_PS5_CONNECTED;
    }

    atomic_set(&s_pattern, pattern);
}

static void led_thread_fn(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (int attempts = 0; !gpio_is_ready_dt(&led); attempts++) {
        if (attempts >= 10) {
            LOG_ERR("Status LED device never became ready");
            return;
        }
        k_sleep(K_MSEC(10));
    }

    if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) != 0) {
        LOG_ERR("Failed to configure status LED");
        return;
    }

    while (1) {
        const struct blink_timing* t = &k_patterns[atomic_get(&s_pattern)];

        gpio_pin_set_dt(&led, 1);
        k_sleep(K_MSEC(t->on_ms));
        gpio_pin_set_dt(&led, 0);
        k_sleep(K_MSEC(t->off_ms));
    }
}

K_THREAD_DEFINE(led_status_thread, 512, led_thread_fn, NULL, NULL, NULL, 7, 0, 0);
