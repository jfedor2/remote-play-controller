// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#include "wifi_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

LOG_MODULE_REGISTER(wifi_manager, LOG_LEVEL_INF);

static K_SEM_DEFINE(s_wifi_connected_sem, 0, 1);
static K_SEM_DEFINE(s_wifi_disconnected_sem, 0, 1);
static K_SEM_DEFINE(s_ipv4_acquired_sem, 0, 1);

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static bool s_wifi_connected = false;
static bool s_ipv4_acquired = false;
static bool s_callbacks_registered = false;
static enum wifi_conn_status s_last_conn_status = WIFI_STATUS_CONN_SUCCESS;

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback* cb,
    uint64_t mgmt_event,
    struct net_if* iface) {
    const struct wifi_status* status = (const struct wifi_status*) cb->info;

    if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
        /* struct wifi_status::status and ::conn_status are the same union
         * member, so this also captures the specific failure reason. */
        s_last_conn_status = status->conn_status;
        if (status->status == 0) {
            LOG_INF("Wi-Fi connected successfully!");
            s_wifi_connected = true;
        } else {
            LOG_ERR("Wi-Fi connection failed: %s (%d)",
                wifi_conn_status_txt(status->conn_status), status->status);
            s_wifi_connected = false;
        }
        k_sem_give(&s_wifi_connected_sem);
    } else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        LOG_WRN("Wi-Fi disconnected");
        s_wifi_connected = false;
        s_ipv4_acquired = false;
        k_sem_give(&s_wifi_disconnected_sem);
    }
}

static void ipv4_mgmt_event_handler(struct net_mgmt_event_callback* cb,
    uint64_t mgmt_event,
    struct net_if* iface) {
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        char buf[NET_IPV4_ADDR_LEN];
        struct in_addr* addr = (struct in_addr*) net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
        if (addr) {
            const char* ip_str = net_addr_ntop(AF_INET, addr, buf, sizeof(buf));
            LOG_INF("IPv4 address assigned: %s", ip_str ? ip_str : "unknown");
        } else {
            LOG_INF("IPv4 address assigned");
        }
        s_ipv4_acquired = true;
        k_sem_give(&s_ipv4_acquired_sem);
    }
}

static void ensure_callbacks_registered(void) {
    if (s_callbacks_registered) {
        return;
    }
    net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
        NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    net_mgmt_init_event_callback(&ipv4_cb, ipv4_mgmt_event_handler,
        NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&ipv4_cb);
    s_callbacks_registered = true;
}

void wifi_manager_disconnect(void) {
    if (!s_wifi_connected) {
        return;
    }

    struct net_if* iface = net_if_get_default();
    if (!iface) {
        return;
    }

    LOG_INF("Requesting Wi-Fi disconnect...");
    /* Reset the semaphore before issuing the disconnect so we don't pick up
     * a stale signal from a previous disconnect event. */
    k_sem_reset(&s_wifi_disconnected_sem);

    int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
    if (ret && ret != -EALREADY) {
        LOG_ERR("NET_REQUEST_WIFI_DISCONNECT failed: %d", ret);
        return;
    }

    /* Wait for the driver to confirm the disconnect (5 s timeout) */
    if (k_sem_take(&s_wifi_disconnected_sem, K_SECONDS(5)) != 0) {
        LOG_WRN("Timed out waiting for Wi-Fi disconnect confirmation");
        /* Force the state flags anyway so a reconnect can proceed */
        s_wifi_connected = false;
        s_ipv4_acquired = false;
    }

    LOG_INF("Wi-Fi disconnected.");
}

int wifi_manager_init_and_connect(const char* ssid, const char* psk) {
    struct net_if* iface = net_if_get_default();
    if (!iface) {
        LOG_ERR("No default network interface found!");
        return -ENODEV;
    }

    ensure_callbacks_registered();

    LOG_INF("Connecting to Wi-Fi SSID: '%s'...", ssid);

    /* Reset semaphores so stale signals from a prior connection don't fire */
    k_sem_reset(&s_wifi_connected_sem);
    k_sem_reset(&s_ipv4_acquired_sem);
    s_wifi_connected = false;
    s_ipv4_acquired = false;
    s_last_conn_status = WIFI_STATUS_CONN_SUCCESS;

    struct wifi_connect_req_params params = {
        .ssid = (const uint8_t*) ssid,
        .ssid_length = strlen(ssid),
        .psk = (const uint8_t*) psk,
        .psk_length = strlen(psk),
        .security = strlen(psk) > 0 ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE,
        .channel = WIFI_CHANNEL_ANY,
        .band = WIFI_FREQ_BAND_2_4_GHZ,
    };

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
    if (ret) {
        LOG_ERR("NET_REQUEST_WIFI_CONNECT failed: %d", ret);
        return ret;
    }

    LOG_INF("Waiting for Wi-Fi connection...");
    if (k_sem_take(&s_wifi_connected_sem, K_SECONDS(30)) != 0) {
        LOG_ERR("Timed out waiting for Wi-Fi connection");
        return -ETIMEDOUT;
    }

    if (!s_wifi_connected) {
        /* Only distinguish "definitely a bad password" -- everything else
         * (AP not found, generic failure, driver-level timeout) is treated
         * as transient by the caller, which retries. */
        return s_last_conn_status == WIFI_STATUS_CONN_WRONG_PASSWORD ? -EACCES : -EAGAIN;
    }

    LOG_INF("Waiting for DHCP IPv4 address...");
    if (k_sem_take(&s_ipv4_acquired_sem, K_SECONDS(30)) != 0) {
        LOG_ERR("Timed out waiting for IPv4 address assignment");
        return -ETIMEDOUT;
    }

    LOG_INF("Wi-Fi network ready!");
    return 0;
}

bool wifi_manager_is_connected(void) {
    return s_wifi_connected && s_ipv4_acquired;
}
