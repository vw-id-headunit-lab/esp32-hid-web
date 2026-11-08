#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs_flash.h"

#include "api_json.h"
#include "config.h"
#include "hid_keys.h"
#include "usb_keyboard.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This project requires ESP32-S3 native USB OTG."
#endif

static_assert(sizeof(HID_AP_PASSWORD) >= 9 && sizeof(HID_AP_PASSWORD) <= 64,
              "Hotspot password must contain 8 to 63 bytes.");
static_assert(sizeof(HID_WIFI_SSID) <= 33, "Wi-Fi SSID must be at most 32 bytes.");
static_assert(sizeof(HID_WIFI_PASSWORD) <= 65, "Wi-Fi password must be at most 64 bytes.");

extern const uint8_t webStart[] asm("_binary_index_html_start");
extern const uint8_t webEnd[] asm("_binary_index_html_end");

namespace {
constexpr const char* Tag = "hid_web";
usb_keyboard::Transport transport;
hidweb::Sender sender(transport);
SemaphoreHandle_t senderMutex;
esp_netif_t* apNetif;
esp_netif_t* staNetif;
std::atomic<bool> wifiConnected{false};
char apName[24];
char apiToken[33];

class SenderLock {
public:
    SenderLock() { xSemaphoreTake(senderMutex, portMAX_DELAY); }
    ~SenderLock() { xSemaphoreGive(senderMutex); }
    SenderLock(const SenderLock&) = delete;
    SenderLock& operator=(const SenderLock&) = delete;
};

uint32_t milliseconds() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

const char* stateName(hidweb::State state) {
    switch (state) {
        case hidweb::State::Idle: return "idle";
        case hidweb::State::Waiting: return "waiting";
        case hidweb::State::Sending: return "sending";
        case hidweb::State::Done: return "done";
        case hidweb::State::Stopped: return "stopped";
        case hidweb::State::Error: return "error";
    }
    return "error";
}

const char* httpStatus(int code) {
    switch (code) {
        case 200: return "200 OK";
        case 202: return "202 Accepted";
        case 400: return "400 Bad Request";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 408: return "408 Request Timeout";
        case 409: return "409 Conflict";
        case 413: return "413 Content Too Large";
        case 415: return "415 Unsupported Media Type";
        case 503: return "503 Service Unavailable";
        default: return "500 Internal Server Error";
    }
}

void headers(httpd_req_t* request) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
}

esp_err_t jsonResponse(httpd_req_t* request, int code, const hidweb::Json& doc) {
    if (!doc) return httpd_resp_send_500(request);
    std::unique_ptr<char, decltype(&cJSON_free)> body(cJSON_PrintUnformatted(doc.get()), cJSON_free);
    if (!body) return httpd_resp_send_500(request);
    headers(request);
    httpd_resp_set_status(request, httpStatus(code));
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    return httpd_resp_send(request, body.get(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t errorResponse(httpd_req_t* request, int code, const char* message) {
    auto doc = hidweb::jsonObject();
    cJSON_AddBoolToObject(doc.get(), "ok", false);
    cJSON_AddStringToObject(doc.get(), "error", message);
    return jsonResponse(request, code, doc);
}

bool readRequest(httpd_req_t* request, hidweb::Json& doc) {
    char token[sizeof(apiToken)];
    if (httpd_req_get_hdr_value_len(request, "X-HID-Token") != sizeof(apiToken) - 1 ||
        httpd_req_get_hdr_value_str(request, "X-HID-Token", token, sizeof(token)) != ESP_OK ||
        std::strcmp(token, apiToken) != 0) {
        errorResponse(request, 403, "页面会话已失效，请刷新页面"); return false;
    }
    char type[80];
    if (httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof(type)) != ESP_OK) {
        errorResponse(request, 415, "请使用 application/json 请求"); return false;
    }
    for (char* p = type; *p; ++p) *p = std::tolower(static_cast<unsigned char>(*p));
    const std::string contentType(type);
    if (contentType != "application/json" && contentType.rfind("application/json;", 0) != 0) {
        errorResponse(request, 415, "请使用 application/json 请求"); return false;
    }
    if (request->content_len > config::MaxRequestBytes) {
        errorResponse(request, 413, "请求体过大"); return false;
    }
    std::string body(request->content_len, '\0');
    size_t offset = 0;
    const uint32_t start = milliseconds();
    while (offset < body.size()) {
        const int received = httpd_req_recv(request, &body[offset], body.size() - offset);
        if (received <= 0 || milliseconds() - start > 5000) {
            errorResponse(request, 408, "读取请求超时，请重试"); return false;
        }
        offset += received;
    }
    const char* error;
    doc = hidweb::parseJson(body, error);
    if (!doc) { errorResponse(request, 400, error); return false; }
    return true;
}

// Must be called under senderMutex; responses are sent after releasing it.
const char* startError(int& code) {
    if (sender.busy() || sender.releasing()) {
        code = 409; return "键盘正在工作，请等待或停止当前任务";
    }
    if (!transport.connected()) {
        code = 503; return "USB 未连接到目标设备，或设备已挂起";
    }
    return nullptr;
}

void accepted(hidweb::Json& doc) {
    cJSON_AddBoolToObject(doc.get(), "ok", true);
    cJSON_AddNumberToObject(doc.get(), "job_id", sender.jobId);
    cJSON_AddNumberToObject(doc.get(), "total", sender.total);
}

std::string ipAddress(esp_netif_t* netif) {
    esp_netif_ip_info_t info{};
    if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK) return "";
    char ip[16];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
    return ip;
}

esp_err_t status(httpd_req_t* request) {
    auto doc = hidweb::jsonObject();
    cJSON_AddStringToObject(doc.get(), "api_token", apiToken);
    cJSON_AddStringToObject(doc.get(), "device", "ESP32-S3 Web Keyboard");
    cJSON_AddStringToObject(doc.get(), "framework", "ESP-IDF");
    cJSON_AddBoolToObject(doc.get(), "usb_connected", transport.connected());
    cJSON_AddBoolToObject(doc.get(), "caps_lock", (usb_keyboard::leds() & 2) != 0);
    cJSON_AddBoolToObject(doc.get(), "num_lock", (usb_keyboard::leds() & 1) != 0);
    {
        SenderLock lock;
        cJSON_AddBoolToObject(doc.get(), "busy", sender.busy());
        cJSON_AddBoolToObject(doc.get(), "releasing", sender.releasing());
        cJSON_AddStringToObject(doc.get(), "state", stateName(sender.state));
        cJSON_AddNumberToObject(doc.get(), "job_id", sender.jobId);
        cJSON_AddNumberToObject(doc.get(), "completed", sender.completed);
        cJSON_AddNumberToObject(doc.get(), "total", sender.total);
        cJSON_AddNumberToObject(doc.get(), "delay_remaining_ms", sender.remainingDelay(milliseconds()));
        cJSON_AddStringToObject(doc.get(), "error", sender.error);
    }
    cJSON_AddNumberToObject(doc.get(), "max_text_bytes", config::MaxTextBytes);
    cJSON_AddStringToObject(doc.get(), "ap_ssid", apName);
    cJSON_AddStringToObject(doc.get(), "ap_ip", ipAddress(apNetif).c_str());
    const bool wifi = wifiConnected.load();
    cJSON_AddBoolToObject(doc.get(), "wifi_connected", wifi);
    cJSON_AddStringToObject(doc.get(), "wifi_ip", wifi ? ipAddress(staNetif).c_str() : "");
    return jsonResponse(request, 200, doc);
}

esp_err_t typeText(httpd_req_t* request) {
    hidweb::Json doc(nullptr, cJSON_Delete);
    if (!readRequest(request, doc)) return ESP_FAIL;  // Close any unconsumed body.
    hidweb::TypeRequest input;
    if (const char* error = hidweb::parseType(doc.get(), input)) return errorResponse(request, 400, error);
    int code = 400;
    const char* error;
    auto result = hidweb::jsonObject();
    {
        SenderLock lock;
        error = startError(code);
        if (!error) error = sender.start(std::move(input.text), input.mode, input.interval, input.delay, milliseconds());
        if (!error) accepted(result);
    }
    return error ? errorResponse(request, code, error) : jsonResponse(request, 202, result);
}

esp_err_t sendKey(httpd_req_t* request) {
    hidweb::Json doc(nullptr, cJSON_Delete);
    if (!readRequest(request, doc)) return ESP_FAIL;
    hidweb::System system;
    if (const char* error = hidweb::parseSystem(doc.get(), system)) return errorResponse(request, 400, error);
    const cJSON* name = cJSON_GetObjectItemCaseSensitive(doc.get(), "key");
    hidweb::Stroke stroke;
    if (!cJSON_IsString(name) || !hidweb::findKey(name->valuestring, stroke, system))
        return errorResponse(request, 400, "不支持的按键");
    int code = 400;
    const char* error;
    auto result = hidweb::jsonObject();
    {
        SenderLock lock;
        error = startError(code);
        if (!error) error = sender.startKey(stroke, milliseconds());
        if (!error) accepted(result);
    }
    return error ? errorResponse(request, code, error) : jsonResponse(request, 202, result);
}

esp_err_t stop(httpd_req_t* request) {
    hidweb::Json doc(nullptr, cJSON_Delete);
    if (!readRequest(request, doc)) return ESP_FAIL;
    { SenderLock lock; sender.stop(); }
    auto result = hidweb::jsonObject();
    cJSON_AddBoolToObject(result.get(), "ok", true);
    return jsonResponse(request, 200, result);
}

esp_err_t index(httpd_req_t* request) {
    headers(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Content-Security-Policy", "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'");
    return httpd_resp_send(request, reinterpret_cast<const char*>(webStart), webEnd - webStart - 1);
}

void setupWeb() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 2;
    config.send_wait_timeout = 2;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index, .user_ctx = nullptr},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status, .user_ctx = nullptr},
        {.uri = "/api/type", .method = HTTP_POST, .handler = typeText, .user_ctx = nullptr},
        {.uri = "/api/key", .method = HTTP_POST, .handler = sendKey, .user_ctx = nullptr},
        {.uri = "/api/stop", .method = HTTP_POST, .handler = stop, .user_ctx = nullptr}
    };
    for (const auto& route : routes) ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
    ESP_ERROR_CHECK(httpd_register_err_handler(server, HTTPD_404_NOT_FOUND,
        [](httpd_req_t* req, httpd_err_code_t) { return errorResponse(req, 404, "没有这个接口"); }));
}

void wifiEvent(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifiConnected = false;
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        wifiConnected = true;
        const auto* event = static_cast<ip_event_got_ip_t*>(data);
        ESP_LOGI(Tag, "LAN web: http://" IPSTR "/", IP2STR(&event->ip_info.ip));
    }
}

void setupWifi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    apNetif = esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(apNetif ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_netif_set_hostname(apNetif, "esp32-hid"));
    const bool useSta = std::strlen(HID_WIFI_SSID) != 0;
    if (useSta) {
        staNetif = esp_netif_create_default_wifi_sta();
        ESP_ERROR_CHECK(staNetif ? ESP_OK : ESP_ERR_NO_MEM);
        ESP_ERROR_CHECK(esp_netif_set_hostname(staNetif, "esp32-hid"));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEvent, nullptr));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifiEvent, nullptr));
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(useSta ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    wifi_config_t ap{};
    std::memcpy(ap.ap.ssid, apName, std::strlen(apName));
    ap.ap.ssid_len = std::strlen(apName);
    std::memcpy(ap.ap.password, HID_AP_PASSWORD, sizeof(HID_AP_PASSWORD));
    ap.ap.channel = 1;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 4;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    if (useSta) {
        wifi_config_t sta{};
        std::memcpy(sta.sta.ssid, HID_WIFI_SSID, sizeof(HID_WIFI_SSID) - 1);
        std::memcpy(sta.sta.password, HID_WIFI_PASSWORD, sizeof(HID_WIFI_PASSWORD) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp32-hid"));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-S3 Web Keyboard"));
    ESP_ERROR_CHECK(mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0));
}

void senderTask(void*) {
    while (true) {
        { SenderLock lock; sender.tick(milliseconds()); }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
}  // namespace

extern "C" void app_main() {
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);
    senderMutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(senderMutex ? ESP_OK : ESP_ERR_NO_MEM);
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    // Preserve the hotspot name used by the previously installed firmware.
    snprintf(apName, sizeof(apName), "S3-HID-%02X%02X%02X", mac[5], mac[4], mac[3]);
    usb_keyboard::init(mac);
    setupWifi();
    uint8_t random[16];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) snprintf(apiToken + i * 2, 3, "%02x", random[i]);
    ESP_ERROR_CHECK(xTaskCreate(senderTask, "hid_sender", 6144, nullptr, 6, nullptr) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    setupWeb();
    ESP_LOGI(Tag, "Hotspot: %s", apName);
    ESP_LOGI(Tag, "Web: http://%s/", ipAddress(apNetif).c_str());
}
