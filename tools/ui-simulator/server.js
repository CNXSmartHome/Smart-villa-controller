#!/usr/bin/env node
"use strict";

/**
 * SVC-100 UI Simulator v2 — mock embedded Web UI + REST API.
 *
 * Dependency-free (Node core only). Mirrors the firmware security posture so the
 * future embedded Web UI can be previewed before it is ported back:
 *   - GET /api/csrf requires X-Auth-Token.
 *   - Every mutating POST requires BOTH X-Auth-Token AND X-SVC-CSRF.
 *   - Privileged reads (GET /api/config, GET /api/io) require X-Auth-Token.
 *   - Secrets (wifi_pass, mqtt_pass, setup_password) are NEVER returned by GET.
 *   - Passwords are accepted only in the request body, never the URL/query.
 *   - Config writes are whitelist-based (fail closed); a write can never toggle
 *     provisioning/auth or protected board fields.
 *
 * This is a SIMULATOR ONLY. It does not touch firmware and drives no hardware.
 */

const http = require("http");
const fs = require("fs");
const path = require("path");
const { URLSearchParams } = require("url");

const HOST = "127.0.0.1";
const PORT = Number(process.env.PORT || 3200);
const SETUP_PASSWORD = process.env.SVC_SIM_PASSWORD || "admin1234";
const CSRF = "simcsrf0123456789abcdef012345";
const MAX_BODY = 8192;

const root = __dirname;

/* Fields never writable via POST /api/config (fail closed) — mirrors firmware. */
const protectedFields = new Set([
  "board_id",
  "version",
  "crc",
  "provisioned",
  "setup_password",
  "webui_require_auth",
  "relay_active_high",
  "relay_safe_on",
]);

/* Installer-writable scalar fields. `rules` is handled separately. */
const writableFields = new Set([
  "device_name",
  "wifi_enabled",
  "wifi_ssid",
  "wifi_pass",
  "eth_enabled",
  "din_active_low",
  "din_debounce_ms",
  "room_empty_delay_sec",
  "sensor_fault_policy",
  "presence_sensor_count",
  "presence_1_type",
  "presence_1_rs485_port",
  "presence_1_modbus_addr",
  "presence_1_din_index",
  "presence_1_reg",
  "presence_1_present_min",
  "presence_2_type",
  "presence_2_rs485_port",
  "presence_2_modbus_addr",
  "presence_2_din_index",
  "presence_2_reg",
  "presence_2_present_min",
  "fallback_din_enabled",
  "fallback_din_chan",
  // Network / MQTT (SVC-015 preview)
  "mqtt_enabled",
  "mqtt_host",
  "mqtt_port",
  "mqtt_user",
  "mqtt_pass",
  "mqtt_topic_prefix",
  "mqtt_tls",
]);

/* Secrets stripped from every GET response. */
const secretFields = ["wifi_pass", "mqtt_pass", "setup_password"];

const RELAY_COUNT = 4;
const INPUT_COUNT = 8;
const RELAY_LABELS = ["Light", "Fan", "Pump", "Spare"];
const INPUT_LABELS = ["Door", "Keycard", "Leak", "Smoke", "S1", "S2", "Override", "Spare"];

let config = {
  device_name: "Villa 12",
  wifi_enabled: 1,
  wifi_ssid: "Villa-Control",
  wifi_pass: "",
  eth_enabled: 0,
  din_active_low: 255,
  din_debounce_ms: 30,
  room_empty_delay_sec: 30,
  sensor_fault_policy: 0,
  presence_sensor_count: 2,
  presence_1_type: 1,
  presence_1_rs485_port: 0,
  presence_1_modbus_addr: 1,
  presence_1_din_index: 0,
  presence_1_reg: 0,
  presence_1_present_min: 1,
  presence_2_type: 2,
  presence_2_rs485_port: 0,
  presence_2_modbus_addr: 2,
  presence_2_din_index: 5,
  presence_2_reg: 0,
  presence_2_present_min: 1,
  fallback_din_enabled: 1,
  fallback_din_chan: 5,
  mqtt_enabled: 0,
  mqtt_host: "mqtt.local",
  mqtt_port: 1883,
  mqtt_user: "villa",
  mqtt_pass: "",
  mqtt_topic_prefix: "svc100/villa12",
  mqtt_tls: 0,
  provisioned: true,
  rules: [
    { trigger: "presence", channel: 0, for_ms: 0, action: 1, target_relay: 0, off_delay_ms: 30000 },
    { trigger: "din", channel: 0, for_ms: 0, action: 1, target_relay: 2, off_delay_ms: 0 },
  ],
};

/* ----- runtime (non-persistent) simulated hardware state ----- */
let relayMask = 0; // bit i = relay i energized
let inputMask = 0b00100001; // Door + S1 asserted by default
let presence = 1; // 0 empty, 1 occupied, 2 unknown
let health = "OK";
let fault = false;
let lastEvent = "boot: controller ready";
let ota = { in_progress: false, health_gate: "idle", rollback: "armed", last_url: "" };

/* Gentle liveliness so the Dashboard/Control tabs feel real (deterministic-ish). */
setInterval(() => {
  // Toggle the "Keycard" input occasionally and recompute presence/room.
  inputMask ^= 0b00000010;
  presence = inputMask & 0b00110000 ? 1 : 0; // S1/S2 bits => occupied
  lastEvent = `di: input mask now 0x${inputMask.toString(16)}`;
}, 5000).unref?.();

function send(res, status, body, type = "application/json") {
  res.writeHead(status, {
    "Content-Type": type,
    "Cache-Control": "no-store",
    "X-Content-Type-Options": "nosniff",
  });
  res.end(body);
}

function json(res, status, obj) {
  send(res, status, JSON.stringify(obj));
}

function authed(req) {
  return req.headers["x-auth-token"] === SETUP_PASSWORD;
}

function csrfOk(req) {
  return req.headers["x-svc-csrf"] === CSRF;
}

/** Enforce auth (+CSRF for mutating) once; returns true if the guard sent a response. */
function guard(req, res, mutating) {
  if (!authed(req)) {
    json(res, 401, { error: "authentication required" });
    return true;
  }
  if (mutating && !csrfOk(req)) {
    json(res, 403, { error: "missing/invalid CSRF" });
    return true;
  }
  return false;
}

function readBody(req, max = MAX_BODY) {
  return new Promise((resolve, reject) => {
    let data = "";
    req.setEncoding("utf8");
    req.on("data", (chunk) => {
      data += chunk;
      if (data.length > max) {
        reject(new Error("oversized body"));
        req.destroy();
      }
    });
    req.on("end", () => resolve(data));
    req.on("error", reject);
  });
}

/** Parse a body as JSON (if Content-Type says so) or urlencoded key/values. */
function parseBody(req, body) {
  const ctype = String(req.headers["content-type"] || "");
  if (ctype.includes("application/json")) {
    const obj = JSON.parse(body || "{}");
    return obj && typeof obj === "object" ? obj : {};
  }
  const params = new URLSearchParams(body);
  const obj = {};
  for (const [k, v] of params.entries()) obj[k] = v;
  // urlencoded callers may pass a JSON `rules` field.
  if (typeof obj.rules === "string") {
    try {
      obj.rules = JSON.parse(obj.rules);
    } catch {
      delete obj.rules;
    }
  }
  return obj;
}

function numeric(value, min, max, fallback) {
  const n = Number.parseInt(value, 10);
  if (!Number.isFinite(n)) return fallback;
  return Math.min(max, Math.max(min, n));
}

function str(value, maxLen, fallback) {
  if (typeof value !== "string") return fallback;
  return value.slice(0, maxLen);
}

function sanitizeRules(rules) {
  if (!Array.isArray(rules)) return config.rules;
  return rules.slice(0, 16).map((r) => ({
    trigger: ["presence", "din", "schedule", "manual"].includes(r.trigger) ? r.trigger : "presence",
    channel: numeric(r.channel, 0, 31, 0),
    for_ms: numeric(r.for_ms, 0, 600000, 0),
    action: numeric(r.action, 0, 1, 1),
    target_relay: numeric(r.target_relay, 0, RELAY_COUNT - 1, 0),
    off_delay_ms: numeric(r.off_delay_ms, 0, 600000, 0),
  }));
}

function sanitize(work) {
  work.wifi_enabled = numeric(work.wifi_enabled, 0, 1, 0);
  work.eth_enabled = numeric(work.eth_enabled, 0, 1, 0);
  work.din_active_low = numeric(work.din_active_low, 0, 0xffffffff, 0);
  work.din_debounce_ms = numeric(work.din_debounce_ms, 1, 2000, 30);
  work.room_empty_delay_sec = numeric(work.room_empty_delay_sec, 0, 3600, 30);
  work.sensor_fault_policy = numeric(work.sensor_fault_policy, 0, 2, 0);
  work.presence_sensor_count = numeric(work.presence_sensor_count, 0, 2, 1);
  for (const i of [1, 2]) {
    work[`presence_${i}_type`] = numeric(work[`presence_${i}_type`], 0, 2, 0);
    work[`presence_${i}_rs485_port`] = numeric(work[`presence_${i}_rs485_port`], 0, 3, 0);
    work[`presence_${i}_modbus_addr`] = numeric(work[`presence_${i}_modbus_addr`], 1, 247, 1);
    work[`presence_${i}_din_index`] = numeric(work[`presence_${i}_din_index`], 0, 31, 0);
  }
  work.fallback_din_enabled = numeric(work.fallback_din_enabled, 0, 1, 0);
  work.fallback_din_chan = numeric(work.fallback_din_chan, 0, 31, 0);
  work.device_name = str(work.device_name, 31, "SVC-100");
  work.wifi_ssid = str(work.wifi_ssid, 31, "");
  work.wifi_pass = str(work.wifi_pass, 63, "");
  // MQTT
  work.mqtt_enabled = numeric(work.mqtt_enabled, 0, 1, 0);
  work.mqtt_host = str(work.mqtt_host, 63, "");
  work.mqtt_port = numeric(work.mqtt_port, 1, 65535, 1883);
  work.mqtt_user = str(work.mqtt_user, 63, "");
  work.mqtt_pass = str(work.mqtt_pass, 63, "");
  work.mqtt_topic_prefix = str(work.mqtt_topic_prefix, 63, "");
  work.mqtt_tls = numeric(work.mqtt_tls, 0, 1, 0);
  work.rules = sanitizeRules(work.rules);
  // Security invariants a config write can NEVER flip in the simulator:
  work.provisioned = true;
  return work;
}

function publicConfig() {
  const clone = { ...config };
  for (const k of secretFields) delete clone[k];
  return clone;
}

function roomState() {
  if (presence === 2) return "unknown";
  return presence === 1 ? "occupied" : "empty";
}

async function handle(req, res) {
  const url = new URL(req.url, `http://${HOST}:${PORT}`);
  const p = url.pathname;

  if (req.method === "GET" && p === "/") {
    const file = path.join(root, "index.html");
    return send(res, 200, fs.readFileSync(file, "utf8"), "text/html; charset=utf-8");
  }

  // Public status (parity: firmware GET /api/status is unauthenticated, no secrets).
  if (req.method === "GET" && p === "/api/status") {
    return json(res, 200, {
      product: "SVC-100",
      fw: "sim-v2",
      name: config.device_name,
      uptime_ms: Math.floor(process.uptime() * 1000),
      presence,
      room: roomState(),
      provisioned: true,
      health,
      fault,
      network: {
        wifi: config.wifi_enabled ? "connected" : "off",
        ssid: config.wifi_enabled ? config.wifi_ssid : "",
        eth: config.eth_enabled ? "up" : "off",
        mqtt: config.mqtt_enabled ? "enabled" : "off",
        ip: "192.168.1.42",
      },
      last_event: lastEvent,
      events_dropped: 0,
    });
  }

  if (req.method === "GET" && p === "/api/csrf") {
    if (guard(req, res, false)) return;
    return json(res, 200, { csrf: CSRF });
  }

  if (req.method === "GET" && p === "/api/config") {
    if (guard(req, res, false)) return;
    return json(res, 200, publicConfig());
  }

  if (req.method === "POST" && p === "/api/config") {
    if (guard(req, res, true)) return;
    let obj;
    try {
      obj = parseBody(req, await readBody(req));
    } catch (err) {
      return json(res, 400, { error: err.message });
    }
    const work = { ...config };
    let applied = 0;
    let rejected = 0;
    for (const [key, value] of Object.entries(obj)) {
      if (key === "rules") {
        work.rules = value;
        applied++;
        continue;
      }
      if (protectedFields.has(key) || !writableFields.has(key)) {
        rejected++;
        continue;
      }
      work[key] = value;
      applied++;
    }
    config = sanitize(work);
    lastEvent = `config: saved (${applied} applied, ${rejected} rejected)`;
    return json(res, 200, { ok: true, applied, rejected });
  }

  if (req.method === "GET" && p === "/api/io") {
    if (guard(req, res, false)) return;
    return json(res, 200, {
      board_id: "svc100_reva",
      board_name: "SVC-100 Rev A",
      device_name: config.device_name,
      capabilities: 0x7f,
      relay_count: RELAY_COUNT,
      input_count: INPUT_COUNT,
      relays: relayMask,
      inputs: inputMask,
      relay_labels: RELAY_LABELS,
      input_labels: INPUT_LABELS,
    });
  }

  if (req.method === "POST" && p === "/api/io") {
    if (guard(req, res, true)) return;
    const relay = Number.parseInt(url.searchParams.get("relay"), 10);
    const on = url.searchParams.get("on") === "1";
    if (!Number.isInteger(relay) || relay < 0 || relay >= RELAY_COUNT) {
      return json(res, 400, { error: "relay index out of range for this board" });
    }
    if (on) relayMask |= 1 << relay;
    else relayMask &= ~(1 << relay);
    relayMask >>>= 0;
    lastEvent = `relay: ${RELAY_LABELS[relay]} -> ${on ? "ON" : "OFF"}`;
    return json(res, 200, { ok: true, relay, on, relays: relayMask });
  }

  if (req.method === "POST" && p === "/api/mqtt/test") {
    if (guard(req, res, true)) return;
    let obj;
    try {
      obj = parseBody(req, await readBody(req));
    } catch (err) {
      return json(res, 400, { error: err.message });
    }
    const host = str(obj.mqtt_host, 63, "");
    const port = numeric(obj.mqtt_port, 1, 65535, 0);
    if (!host || !port) {
      return json(res, 400, { ok: false, error: "broker host and port required" });
    }
    // Simulated probe: succeed for a plausible host, otherwise report failure.
    const reachable = /^[a-z0-9.\-]+$/i.test(host);
    lastEvent = `mqtt: test ${host}:${port} -> ${reachable ? "ok" : "fail"}`;
    return json(res, reachable ? 200 : 502, {
      ok: reachable,
      host,
      port,
      tls: numeric(obj.mqtt_tls, 0, 1, 0) === 1,
      latency_ms: reachable ? 40 + Math.floor(Math.random() * 60) : null,
      message: reachable ? "CONNACK received (simulated)" : "connection refused (simulated)",
    });
  }

  if (req.method === "POST" && p === "/api/ota") {
    if (guard(req, res, true)) return;
    let obj;
    try {
      obj = parseBody(req, await readBody(req));
    } catch (err) {
      return json(res, 400, { error: err.message });
    }
    const fwUrl = str(obj.url, 255, "");
    if (!/^https?:\/\//i.test(fwUrl)) {
      return json(res, 400, { ok: false, error: "valid http(s) firmware URL required" });
    }
    // Simulate the health-gated OTA: apply -> validate -> confirm (no rollback).
    ota = { in_progress: false, health_gate: "pass", rollback: "not-triggered", last_url: fwUrl };
    lastEvent = `ota: image from ${fwUrl} validated by health gate`;
    return json(res, 200, {
      ok: true,
      url: fwUrl,
      stages: ["downloaded", "written", "boot-health-check", "confirmed"],
      health_gate: ota.health_gate,
      rollback: ota.rollback,
      message: "update validated by boot health checks (simulated)",
    });
  }

  return json(res, 404, { error: "not found" });
}

const server = http.createServer((req, res) => {
  handle(req, res).catch((err) => {
    json(res, 500, { error: err.message });
  });
});

server.listen(PORT, HOST, () => {
  console.log(`SVC-100 UI simulator v2 running at http://${HOST}:${PORT}/`);
  console.log(`Default setup password: ${SETUP_PASSWORD}`);
});
