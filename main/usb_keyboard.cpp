#include "usb_keyboard.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

namespace {
std::atomic<bool> mounted{false}, suspended{false};
std::atomic<uint8_t> ledState{0};
SemaphoreHandle_t reportDone;
char serialNumber[13];
const char language[] = {0x09, 0x04};
const char* strings[] = {
    language, "HID Web", "ESP32-S3 Web Keyboard", serialNumber, "USB Keyboard"
};
const uint8_t reportDescriptor[] = {TUD_HID_REPORT_DESC_KEYBOARD()};
const uint8_t configurationDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN, 0, 250),
    TUD_HID_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_KEYBOARD, sizeof(reportDescriptor), 0x81, 8, 2)
};

void deviceEvent(tinyusb_event_t* event, void*) {
    switch (event->id) {
        case TINYUSB_EVENT_ATTACHED:
            suspended = false; mounted = true;
            ESP_LOGI("usb", "USB HID keyboard mounted");
            break;
        case TINYUSB_EVENT_DETACHED:
            mounted = false; suspended = false; ledState = 0;
            ESP_LOGI("usb", "USB HID keyboard detached");
            break;
        case TINYUSB_EVENT_SUSPENDED: suspended = true; break;
        case TINYUSB_EVENT_RESUMED: suspended = false; break;
    }
}
}  // namespace

namespace usb_keyboard {

void init(const uint8_t mac[6]) {
    reportDone = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(reportDone ? ESP_OK : ESP_ERR_NO_MEM);
    snprintf(serialNumber, sizeof(serialNumber), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG(deviceEvent);
    config.descriptor.string = strings;
    config.descriptor.string_count = sizeof(strings) / sizeof(strings[0]);
    config.descriptor.full_speed_config = configurationDescriptor;
    ESP_ERROR_CHECK(tinyusb_driver_install(&config));
}

uint8_t leds() { return ledState.load(); }
bool Transport::connected() { return mounted.load() && !suspended.load(); }

bool Transport::send(hidweb::Stroke stroke) {
    if (!connected()) return false;
    const TickType_t start = xTaskGetTickCount();
    while (!tud_hid_ready()) {
        if (!connected() || xTaskGetTickCount() - start >= pdMS_TO_TICKS(50)) return false;
        vTaskDelay(1);
    }
    xSemaphoreTake(reportDone, 0);  // Discard a late completion from a timed-out report.
    const uint8_t keys[6] = {stroke.key, 0, 0, 0, 0, 0};
    if (!tud_hid_keyboard_report(0, stroke.modifiers, keys)) return false;
    return xSemaphoreTake(reportDone, pdMS_TO_TICKS(50)) == pdTRUE;
}

}  // namespace usb_keyboard

extern "C" const uint8_t* tud_hid_descriptor_report_cb(uint8_t) {
    return reportDescriptor;
}

extern "C" uint16_t tud_hid_get_report_cb(uint8_t, uint8_t, hid_report_type_t type,
                                         uint8_t* buffer, uint16_t length) {
    if (type != HID_REPORT_TYPE_INPUT) return 0;
    const uint16_t size = std::min<uint16_t>(length, sizeof(hid_keyboard_report_t));
    std::memset(buffer, 0, size);
    return size;
}

extern "C" void tud_hid_set_report_cb(uint8_t, uint8_t, hid_report_type_t type,
                                     const uint8_t* buffer, uint16_t size) {
    if (type == HID_REPORT_TYPE_OUTPUT && size) ledState = buffer[0];
}

extern "C" void tud_hid_report_complete_cb(uint8_t, const uint8_t*, uint16_t) {
    if (reportDone) xSemaphoreGive(reportDone);
}
