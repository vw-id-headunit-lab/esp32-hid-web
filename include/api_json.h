#pragma once

#include <cmath>
#include <cstring>
#include <memory>
#include "cJSON.h"
#include "config.h"
#include "hid_text.h"

namespace hidweb {

using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
inline Json jsonObject() { return Json(cJSON_CreateObject(), cJSON_Delete); }

inline Json parseJson(const std::string& body, const char*& error) {
    error = nullptr;
    bool quoted = false;
    unsigned depth = 0;
    // cJSON uses NUL-terminated strings. Explicitly reject embedded U+0000
    // rather than silently truncating text, modes or named keys at that point.
    for (size_t i = 0; i < body.size(); ++i) {
        const unsigned char c = body[i];
        if (c == 0 || (quoted && c < 0x20)) { error = "JSON 包含无效控制字符"; break; }
        if (quoted && c == '\\') {
            if (body.compare(i + 1, 5, "u0000") == 0) { error = "不支持 U+0000 空字符"; break; }
            if (i + 1 < body.size()) ++i;
        } else if (c == '"') quoted = !quoted;
        else if (!quoted && (c == '{' || c == '[')) {
            if (++depth > 8) { error = "JSON 嵌套过深"; break; }
        } else if (!quoted && (c == '}' || c == ']') && depth) --depth;
    }
    Json doc(nullptr, cJSON_Delete);
    if (error) return doc;
    doc.reset(cJSON_ParseWithLengthOpts(body.c_str(), body.size() + 1, nullptr, true));
    if (!doc || !cJSON_IsObject(doc.get())) {
        doc.reset(); error = "JSON 格式不正确";
    }
    return doc;
}

struct TypeRequest {
    std::string text;
    System system = System::Linux;
    Mode mode = Mode::LinuxUnicode;
    uint32_t interval = 25;
    uint32_t delay = 0;
};

inline const char* parseSystem(const cJSON* doc, System& system) {
    system = System::Linux;
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(doc, "system");
    if (!value || cJSON_IsNull(value)) return nullptr;
    if (!cJSON_IsString(value)) return "system 必须为 linux、macos 或 windows";
    if (std::strcmp(value->valuestring, "linux") == 0) system = System::Linux;
    else if (std::strcmp(value->valuestring, "macos") == 0) system = System::MacOS;
    else if (std::strcmp(value->valuestring, "windows") == 0) system = System::Windows;
    else return "未知目标系统";
    return nullptr;
}

inline bool readUnsigned(const cJSON* value, uint32_t min, uint32_t max, uint32_t& output) {
    if (!value || cJSON_IsNull(value)) return true;
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble)) return false;
    const double number = value->valuedouble;
    if (number < min || number > max || std::floor(number) != number) return false;
    output = static_cast<uint32_t>(number);
    return true;
}

inline const char* parseType(const cJSON* doc, TypeRequest& request) {
    request = TypeRequest{};
    if (const char* error = parseSystem(doc, request.system)) return error;
    request.mode = defaultMode(request.system);
    const cJSON* text = cJSON_GetObjectItemCaseSensitive(doc, "text");
    if (!cJSON_IsString(text)) return "text 必须是文字";
    const size_t size = std::strlen(text->valuestring);
    if (size == 0 || size > config::MaxTextBytes) return "请输入 1 至 4096 字节的文字";
    const cJSON* mode = cJSON_GetObjectItemCaseSensitive(doc, "mode");
    if (mode && !cJSON_IsNull(mode)) {
        if (!cJSON_IsString(mode)) return "mode 必须为有效的输入模式名称";
        if (std::strcmp(mode->valuestring, "us_ascii") == 0) request.mode = Mode::UsAscii;
        else if (std::strcmp(mode->valuestring, "linux_unicode") == 0) request.mode = Mode::LinuxUnicode;
        else if (std::strcmp(mode->valuestring, "macos_unicode") == 0) request.mode = Mode::MacOSUnicode;
        else if (std::strcmp(mode->valuestring, "windows_unicode") == 0) request.mode = Mode::WindowsUnicode;
        else return "未知输入模式";
    }
    if (!modeMatchesSystem(request.mode, request.system)) return "输入模式与目标系统不匹配";
    if (!readUnsigned(cJSON_GetObjectItemCaseSensitive(doc, "interval_ms"),
                      config::MinIntervalMs, config::MaxIntervalMs, request.interval) ||
        !readUnsigned(cJSON_GetObjectItemCaseSensitive(doc, "start_delay_ms"),
                      0, config::MaxStartDelayMs, request.delay))
        return "按键间隔须为 10–200 ms，开始延时须为 0–10000 ms 的整数";
    const cJSON* enter = cJSON_GetObjectItemCaseSensitive(doc, "append_enter");
    if (enter && !cJSON_IsNull(enter) && !cJSON_IsBool(enter)) return "append_enter 必须为布尔值";
    request.text.assign(text->valuestring, size);
    if (cJSON_IsTrue(enter)) request.text += '\n';
    return nullptr;
}

}  // namespace hidweb
