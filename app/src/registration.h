// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#ifndef REGISTRATION_H
#define REGISTRATION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute the PS5 registration procedure over the local network using the provided PIN.
 *
 * @param pin 8-digit registration PIN shown on the PS5 screen
 * @return 0 on success, negative error code on failure.
 */
int registration_run(uint32_t pin);

#ifdef __cplusplus
}
#endif

#endif  // REGISTRATION_H
