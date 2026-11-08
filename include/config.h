#pragma once

#if __has_include("config.local.h")
#include "config.local.h"
#endif

#ifndef HID_AP_PASSWORD
#define HID_AP_PASSWORD "esp32hid123"
#endif

#ifndef HID_WIFI_SSID
#define HID_WIFI_SSID ""
#endif

#ifndef HID_WIFI_PASSWORD
#define HID_WIFI_PASSWORD ""
#endif

namespace config {
constexpr unsigned MaxTextBytes = 4096;
constexpr unsigned MaxRequestBytes = 32768;  // Includes JSON Unicode escapes.
constexpr unsigned MinIntervalMs = 10;
constexpr unsigned MaxIntervalMs = 200;
constexpr unsigned MaxStartDelayMs = 10000;
}  // namespace config
