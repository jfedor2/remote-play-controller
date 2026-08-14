// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#ifndef SETTINGS_H
#define SETTINGS_H

#include <chiaki/regist.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_REPORT_VERSION 1
#define CONFIG_REPORT_SIZE 63
#define REPORT_ID_NET 1
#define REPORT_ID_PS5 2
#define REPORT_ID_REGISTER 3
#define REPORT_ID_STATUS 4
#define REPORT_ID_STATUS_FEATURE 5

typedef struct {
    char wifi_ssid[17];      // null-terminated string (max 16 chars)
    char wifi_password[33];  // null-terminated string (max 32 chars)
    uint8_t account_id[8];   // 8 raw bytes (PSN account ID)
    char ps5_ip_str[16];     // null-terminated IPv4 string "a.b.c.d"
} AppConfig;

struct __attribute__((packed)) app_config_report_net {
    uint8_t version;         // Offset 0: 1
    char wifi_ssid[16];      // Offset 1..16: SSID string
    char wifi_password[32];  // Offset 17..48: Password string
    uint8_t reserved[10];    // Offset 49..58: zeroes
    uint32_t crc32;          // Offset 59..62: little-endian CRC-32
};

struct __attribute__((packed)) app_config_report_ps5 {
    uint8_t version;        // Offset 0: 1
    uint8_t account_id[8];  // Offset 1..8: 8 raw bytes
    uint8_t ps5_ip[4];      // Offset 9..12: IPv4 bytes
    uint8_t reserved[46];   // Offset 13..58: zeroes
    uint32_t crc32;         // Offset 59..62: little-endian CRC-32
};

struct __attribute__((packed)) app_config_report_register {
    uint8_t version;       // Offset 0: 1
    uint32_t pin;          // Offset 1..4: uint32_t little-endian PIN
    uint8_t reserved[54];  // Offset 5..58: zeroes
    uint32_t crc32;        // Offset 59..62: little-endian CRC-32
};

BUILD_ASSERT(sizeof(struct app_config_report_net) == CONFIG_REPORT_SIZE, "app_config_report_net size mismatch");
BUILD_ASSERT(sizeof(struct app_config_report_ps5) == CONFIG_REPORT_SIZE, "app_config_report_ps5 size mismatch");
BUILD_ASSERT(sizeof(struct app_config_report_register) == CONFIG_REPORT_SIZE, "app_config_report_register size mismatch");

#define STATUS_FLAG_WIFI_CONNECTED (1 << 0)
#define STATUS_FLAG_HAS_REGISTRATION (1 << 1)
#define STATUS_FLAG_SESSION_ACTIVE (1 << 2)

// Pushed by the device as Input Report 4 whenever its status changes, and
// readable on demand as Feature Report 5 (e.g. to seed a freshly opened
// config page with the current status). A separate report ID from the Input
// push, rather than reusing ID 4 for both types, just to keep GET_REPORT
// (Feature) and the async push (Input) unambiguous.
// No CRC.
struct __attribute__((packed)) app_status_report {
    uint8_t version;   // Offset 0: 1
    uint8_t flags;     // Offset 1: STATUS_FLAG_* bitmask
    char message[61];  // Offset 2..62: null-terminated human-readable status
};

BUILD_ASSERT(sizeof(struct app_status_report) == CONFIG_REPORT_SIZE, "app_status_report size mismatch");

/**
 * Initialize the Zephyr settings subsystem and load saved PS5 host registration & user configuration.
 *
 * @return 0 on success, negative error code on failure.
 */
int app_settings_init(void);

/**
 * Get pointer to current AppConfig.
 */
const AppConfig* settings_get_config(void);

/**
 * Save AppConfig to flash NVS settings ("ps5/config").
 */
int settings_save_config(const AppConfig* config);

/**
 * Check if a valid PS5 registration key and host info are currently loaded.
 */
bool settings_has_registration(void);

/**
 * Check if a non-zero PSN account ID is currently configured.
 */
bool settings_has_account_id(void);

/**
 * Get the configured PSN account ID in the byte order chiaki expects
 * (reversed relative to how it's stored in AppConfig/the HID report).
 *
 * @param out Buffer of at least CHIAKI_PSN_ACCOUNT_ID_SIZE (8) bytes.
 */
void settings_get_account_id_reversed(uint8_t* out);

/**
 * Get pointer to the currently loaded registered host structure.
 * Returns NULL if no registration data is available.
 */
const ChiakiRegisteredHost* settings_get_registered_host(void);

/**
 * Save registered host structure to flash (NVS settings subsystem).
 *
 * @param host Pointer to registered host structure
 * @return 0 on success, negative error code on failure.
 */
int settings_save_registration(const ChiakiRegisteredHost* host);

/**
 * Discard and delete the stored PS5 registration key from flash.
 *
 * @return 0 on success, negative error code on failure.
 */
int settings_clear_registration(void);

#ifdef __cplusplus
}
#endif

#endif  // SETTINGS_H
