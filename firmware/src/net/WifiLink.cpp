// WiFi observability + OTA, mirroring cauldnz/raedian-probe's failsafe idiom. The ENTIRE
// body is compiled only when USE_WIFI=1 (the esp32c3-ota env), so the default build never
// needs firmware/wifi_secret.h. Note: WiFi + dual-role NimBLE share the C3 radio (coex) —
// fine for OTA/observability; heavy concurrent use is a later tuning job.
#if defined(USE_WIFI) && USE_WIFI

#include "net/WifiLink.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "../../wifi_secret.h"  // gitignored; copy from wifi_secret.example.h

using namespace sb20proxy;

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 15000  // how long begin() blocks trying to join
#endif
#ifndef WIFI_HEALTH_DEADLINE_MS
#define WIFI_HEALTH_DEADLINE_MS 35000  // reset if not healthy (WiFi + HTTP up) within this
#endif

// Boot-guard: if we never become healthy in time, reset and retry — a bad OTA that can't
// rejoin the network recovers on its own (the raedian-probe failsafe).
static esp_timer_handle_t s_bootGuard = nullptr;
static void bootGuardCb(void*) { esp_restart(); }

void WifiLink::begin(const char* hostname, StatusProvider provider) {
    provider_ = provider;

    // Arm the boot-guard before anything that could hang.
    esp_timer_create_args_t guardArgs = {};
    guardArgs.callback = &bootGuardCb;
    guardArgs.dispatch_method = ESP_TIMER_TASK;
    guardArgs.name = "bootguard";
    esp_timer_create(&guardArgs, &s_bootGuard);
    esp_timer_start_once(s_bootGuard, (uint64_t)WIFI_HEALTH_DEADLINE_MS * 1000);

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(hostname);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) {
        delay(100);
    }

    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.begin();

    server_ = new WebServer(80);
    server_->on("/", HTTP_GET, [this]() {
        std::string j = provider_ ? renderStatusJson(provider_()) : std::string("{}");
        server_->send(200, "application/json", j.c_str());
    });
    server_->on("/update", HTTP_GET, [this]() {
        server_->send(200, "text/html",
                      "<form method='POST' action='/update' enctype='multipart/form-data'>"
                      "<input type='file' name='firmware'><input type='submit' value='Flash'></form>");
    });
    server_->on(
        "/update", HTTP_POST,
        [this]() {
            bool ok = !Update.hasError();
            server_->send(200, "text/plain", ok ? "OTA OK - rebooting\n" : "OTA FAILED\n");
            delay(400);
            esp_restart();
        },
        [this]() {
            HTTPUpload& up = server_->upload();
            if (up.status == UPLOAD_FILE_START) {
                Update.begin(UPDATE_SIZE_UNKNOWN);
            } else if (up.status == UPLOAD_FILE_WRITE) {
                Update.write(up.buf, up.currentSize);
            } else if (up.status == UPLOAD_FILE_END) {
                Update.end(true);
            }
        });
    server_->begin();
}

void WifiLink::handle() {
    if (server_) server_->handleClient();
    ArduinoOTA.handle();
    if (!healthy_ && server_ && WiFi.status() == WL_CONNECTED) {
        healthy_ = true;
        if (s_bootGuard) esp_timer_stop(s_bootGuard);
        esp_ota_mark_app_valid_cancel_rollback();  // confirm this image is good
    }
}

#endif  // USE_WIFI
