// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

import crc32 from './crc.js';

const CONFIG_VERSION = 1;
const CONFIG_SIZE = 63;
const REPORT_ID_NET = 1;
const REPORT_ID_PS5 = 2;
const REPORT_ID_REGISTER = 3;
const REPORT_ID_STATUS = 4;
const REPORT_ID_STATUS_FEATURE = 5;

const STATUS_FLAG_WIFI_CONNECTED = 1 << 0;
const STATUS_FLAG_HAS_REGISTRATION = 1 << 1;
const STATUS_FLAG_SESSION_ACTIVE = 1 << 2;

const STATUS_HISTORY_MAX = 50;

let device = null;
let statusHistory = [];

document.addEventListener("DOMContentLoaded", function () {
    document.getElementById("open_device").addEventListener("click", open_device);
    document.getElementById("load_from_device").addEventListener("click", load_from_device);
    document.getElementById("save_to_device").addEventListener("click", save_to_device);
    document.getElementById("link_device").addEventListener("click", link_device);

    device_buttons_set_disabled_state(true);
    render_status();

    if ("hid" in navigator) {
        navigator.hid.addEventListener('disconnect', hid_on_disconnect);
    } else {
        display_error("Your browser doesn't support WebHID. Try Chrome (desktop version) or a Chrome-based browser.");
    }
});

async function open_device() {
    clear_messages();
    let success = false;
    const devices = await navigator.hid.requestDevice({
        filters: [{ usagePage: 0xFF00, usage: 0x0022 }]
    }).catch((err) => { display_error(err); });

    const config_interface = devices?.find(d => d.collections.some(c => c.usagePage == 0xff00));
    if (config_interface !== undefined) {
        device = config_interface;
        if (!device.opened) {
            await device.open().catch((err) => {
                display_error(err + "\nIf you're on Linux, you might need to give yourself permissions to the appropriate /dev/hidraw* device.");
            });
        }
        success = device.opened;
    }

    device_buttons_set_disabled_state(!success);

    if (!success) {
        device = null;
    } else {
        statusHistory = [];
        device.addEventListener('inputreport', handle_input_report);
        await fetch_initial_status();
    }
}

async function fetch_initial_status() {
    try {
        const status_report = await device.receiveFeatureReport(REPORT_ID_STATUS_FEATURE);
        // Unlike HIDInputReportEvent.data below, this browser's
        // receiveFeatureReport() includes the report ID as the first byte
        // (see the same pattern in load_from_device()), so skip it.
        const dv_status = new DataView(status_report.buffer, 1);
        const { flags, message } = parse_status_payload(dv_status);
        if (message !== '') {
            add_status_entry(flags, message);
        }
    } catch (e) {
        display_error(e);
    }
}

function handle_input_report(event) {
    if (event.reportId !== REPORT_ID_STATUS) {
        return;
    }
    // Per the WebHID spec, HIDInputReportEvent.data excludes the report ID.
    const { flags, message } = parse_status_payload(event.data);
    add_status_entry(flags, message);
}

function parse_status_payload(dv) {
    const flags = dv.getUint8(1);
    let message = '';
    for (let i = 0; i < 61; i++) {
        const c = dv.getUint8(2 + i);
        if (c === 0) break;
        message += String.fromCharCode(c);
    }
    return { flags, message };
}

function add_status_entry(flags, message) {
    statusHistory.push({ flags, message, timestamp: new Date() });
    if (statusHistory.length > STATUS_HISTORY_MAX) {
        statusHistory.shift();
    }
    render_status();
}

function render_not_connected() {
    document.getElementById("status_latest").innerText = "Not connected.";
    set_status_badge("status_badge_wifi", "Wi-Fi", false);
    set_status_badge("status_badge_link", "Paired", false);
    set_status_badge("status_badge_session", "Session", false);
}

function render_status() {
    const latestEl = document.getElementById("status_latest");
    const logEl = document.getElementById("status_log");

    if (statusHistory.length === 0) {
        render_not_connected();
        logEl.innerHTML = '';
        return;
    }

    const latest = statusHistory[statusHistory.length - 1];
    latestEl.innerText = latest.message;
    set_status_badge("status_badge_wifi", "Wi-Fi", latest.flags & STATUS_FLAG_WIFI_CONNECTED);
    set_status_badge("status_badge_link", "Paired", latest.flags & STATUS_FLAG_HAS_REGISTRATION);
    set_status_badge("status_badge_session", "Session", latest.flags & STATUS_FLAG_SESSION_ACTIVE);

    logEl.innerHTML = '';
    for (const entry of statusHistory) {
        const line = document.createElement('div');
        line.innerText = entry.timestamp.toLocaleTimeString() + " - " + entry.message;
        logEl.appendChild(line);
    }
    logEl.scrollTop = logEl.scrollHeight;
}

function set_status_badge(id, label, active) {
    const el = document.getElementById(id);
    el.innerText = label;
    el.classList.toggle('bg-success', !!active);
    el.classList.toggle('bg-secondary', !active);
}

async function load_from_device() {
    if (device == null) {
        return;
    }
    clear_messages();

    try {
        // Load Report ID 1: Network Config
        const net_report = await device.receiveFeatureReport(REPORT_ID_NET);
        const dv_net = new DataView(net_report.buffer, 1);
        check_crc(dv_net);
        check_received_version(dv_net.getUint8(0));

        let ssid = '';
        for (let i = 0; i < 16; i++) {
            const c = dv_net.getUint8(1 + i);
            if (c === 0) break;
            ssid += String.fromCharCode(c);
        }
        document.getElementById("wifi_ssid_input").value = ssid;
        document.getElementById("wifi_password_input").value = '';

        // Load Report ID 2: PS5 Config
        const ps5_report = await device.receiveFeatureReport(REPORT_ID_PS5);
        const dv_ps5 = new DataView(ps5_report.buffer, 1);
        check_crc(dv_ps5);
        check_received_version(dv_ps5.getUint8(0));

        let accountIdHex = '';
        for (let i = 0; i < 8; i++) {
            accountIdHex += dv_ps5.getUint8(1 + i).toString(16).padStart(2, '0');
        }
        document.getElementById("account_id_input").value = accountIdHex;

        const ipParts = [
            dv_ps5.getUint8(9),
            dv_ps5.getUint8(10),
            dv_ps5.getUint8(11),
            dv_ps5.getUint8(12)
        ];
        document.getElementById("ps5_ip_input").value = ipParts.join('.');
    } catch (e) {
        display_error(e);
    }
}

async function save_to_device() {
    if (device == null) {
        return;
    }
    clear_messages();

    try {
        // Report ID 1: Network Config
        let buf_net = new ArrayBuffer(CONFIG_SIZE);
        let dv_net = new DataView(buf_net);
        dv_net.setUint8(0, CONFIG_VERSION);

        const ssid = document.getElementById("wifi_ssid_input").value;
        if (ssid.length > 16) {
            throw new Error('Maximum Wi-Fi network name length is 16 characters.');
        }
        for (let i = 0; i < 16; i++) {
            const c = ssid.charCodeAt(i);
            dv_net.setUint8(1 + i, isNaN(c) ? 0 : c);
        }

        const password = document.getElementById("wifi_password_input").value;
        if (password.length > 32) {
            throw new Error('Maximum Wi-Fi password length is 32 characters.');
        }
        for (let i = 0; i < 32; i++) {
            const c = password.charCodeAt(i);
            dv_net.setUint8(17 + i, isNaN(c) ? 0 : c);
        }

        add_crc(dv_net);

        // Report ID 2: PS5 Config
        let buf_ps5 = new ArrayBuffer(CONFIG_SIZE);
        let dv_ps5 = new DataView(buf_ps5);
        dv_ps5.setUint8(0, CONFIG_VERSION);

        const accountIdStr = document.getElementById("account_id_input").value.trim();
        if (!/^[0-9a-fA-F]{16}$/.test(accountIdStr)) {
            throw new Error('PSN Account ID must be exactly 16 hex characters.');
        }
        for (let i = 0; i < 8; i++) {
            const byteVal = parseInt(accountIdStr.substr(i * 2, 2), 16);
            dv_ps5.setUint8(1 + i, byteVal);
        }

        const ipStr = document.getElementById("ps5_ip_input").value.trim();
        const ipParts = ipStr.split('.');
        if (ipParts.length !== 4 || ipParts.some(p => isNaN(p) || p === '' || p < 0 || p > 255)) {
            throw new Error('PS5 IP address must be a valid IPv4 address (a.b.c.d).');
        }
        for (let i = 0; i < 4; i++) {
            dv_ps5.setUint8(9 + i, parseInt(ipParts[i], 10));
        }

        add_crc(dv_ps5);

        // Send Feature Reports (Report 1 & Report 2)
        await device.sendFeatureReport(REPORT_ID_NET, buf_net);
        await device.sendFeatureReport(REPORT_ID_PS5, buf_ps5);
    } catch (e) {
        display_error(e);
    }
}

async function link_device() {
    if (device == null) {
        return;
    }
    clear_messages();

    try {
        const pinStr = document.getElementById("ps5_pin_input").value.trim();
        if (!/^\d{8}$/.test(pinStr)) {
            throw new Error('Registration PIN must be exactly 8 digits.');
        }

        let buf_reg = new ArrayBuffer(CONFIG_SIZE);
        let dv_reg = new DataView(buf_reg);

        dv_reg.setUint8(0, CONFIG_VERSION);
        const pinVal = parseInt(pinStr, 10);
        dv_reg.setUint32(1, pinVal, true);

        for (let i = 5; i < 59; i++) {
            dv_reg.setUint8(i, 0);
        }

        add_crc(dv_reg);

        await device.sendFeatureReport(REPORT_ID_REGISTER, buf_reg);
    } catch (e) {
        display_error(e);
    }
}

function clear_messages() {
    document.getElementById("error").classList.add("d-none");
    document.getElementById("success").classList.add("d-none");
}

function display_error(message) {
    document.getElementById("error").innerText = message;
    document.getElementById("error").classList.remove("d-none");
}

function display_success(message) {
    document.getElementById("success").innerText = message;
    document.getElementById("success").classList.remove("d-none");
}

function check_crc(data) {
    if (data.getUint32(CONFIG_SIZE - 4, true) != crc32(data, CONFIG_SIZE - 4)) {
        throw new Error('CRC-32 verification failed.');
    }
}

function add_crc(data) {
    data.setUint32(CONFIG_SIZE - 4, crc32(data, CONFIG_SIZE - 4), true);
}

function check_received_version(config_version) {
    if (config_version != CONFIG_VERSION) {
        throw new Error("Incompatible configuration version.");
    }
}

function hid_on_disconnect(event) {
    if (event.device === device) {
        device = null;
        device_buttons_set_disabled_state(true);
        render_not_connected();
    }
}

function device_buttons_set_disabled_state(state) {
    document.getElementById("load_from_device").disabled = state;
    document.getElementById("save_to_device").disabled = state;
    document.getElementById("link_device").disabled = state;
}
