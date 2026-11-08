#include "api_json.h"
#include "hid_keys.h"
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; std::exit(1); \
} } while (0)

using namespace hidweb;

int main() {
    const char* error;
    auto doc = parseJson(R"({"text":"\u4f60\u597d\ud83d\ude00\r\nA\tB","append_enter":true})", error);
    CHECK(doc && !error);
    TypeRequest input;
    CHECK(!parseType(doc.get(), input));
    CHECK(input.text == "你好😀\r\nA\tB\n");
    CHECK(input.mode == Mode::LinuxUnicode && input.interval == 25 && input.delay == 0);
    size_t count;
    CHECK(!normalize(input.text, input.mode, count) && count == 8);
    CHECK(input.text == "你好😀\nA\tB\n");

    const std::vector<std::string> badJson = {
        R"({"text":"a\u0000b"})", R"({"key":"CTRL_ALT_F1\u0000x"})",
        std::string("{\"text\":\"a\0b\"}", 14), R"({"text":"x"} garbage)",
        "[]", "{", "", R"({"x":[[[[[[[[[]]]]]]]]]})", "{\"text\":\"a\nb\"}"
    };
    for (const auto& body : badJson) {
        auto invalid = parseJson(body, error);
        CHECK(!invalid && error);
    }
    auto literal = parseJson(R"({"text":"\\u0000"})", error);
    CHECK(literal && !error);  // Literal backslash-u is ordinary text, not a NUL.
    TypeRequest literalInput;
    CHECK(!parseType(literal.get(), literalInput) && literalInput.text == "\\u0000");

    const std::vector<std::string> badTypes = {
        R"({"text":42})", R"({"text":""})", R"({"text":"x","mode":4})",
        R"({"text":"x","mode":"unknown"})", R"({"text":"x","interval_ms":9})",
        R"({"text":"x","interval_ms":201})", R"({"text":"x","interval_ms":25.5})",
        R"({"text":"x","interval_ms":"25"})", R"({"text":"x","start_delay_ms":-1})",
        R"({"text":"x","start_delay_ms":10001})", R"({"text":"x","append_enter":1})",
        R"({"text":"x","interval_ms":1e999})",
        R"({"text":"x","system":"other"})", R"({"text":"x","system":1})",
        R"({"text":"x","system":"macos","mode":"linux_unicode"})",
        R"({"text":"x","system":"windows","mode":"macos_unicode"})",
        R"({"text":"x","mode":"windows_unicode"})"
    };
    for (const auto& body : badTypes) {
        auto invalid = parseJson(body, error);
        CHECK(invalid);
        TypeRequest parsed;
        CHECK(parseType(invalid.get(), parsed));
    }
    auto bounds = parseJson(R"({"text":"Hello","mode":"us_ascii","interval_ms":200,"start_delay_ms":0})", error);
    TypeRequest boundInput;
    CHECK(bounds && !parseType(bounds.get(), boundInput));
    CHECK(boundInput.mode == Mode::UsAscii && boundInput.interval == 200 && boundInput.delay == 0);

    auto oversized = jsonObject();
    cJSON_AddStringToObject(oversized.get(), "text", std::string(4097, 'a').c_str());
    TypeRequest oversizedInput;
    CHECK(parseType(oversized.get(), oversizedInput));

    Stroke shortcut;
    CHECK(findKey("CTRL_ALT_F1", shortcut));
    CHECK(shortcut.modifiers == 0x05 && shortcut.key == 0x3a);
    CHECK(findKey("ENTER", shortcut) && shortcut.modifiers == 0 && shortcut.key == 0x28);
    CHECK(!findKey("INVALID", shortcut));
    CHECK(!findKey(std::string("ENTER\0x", 7), shortcut));
    for (const char* name : {"macos", "windows", "linux"}) {
        auto systemDoc = jsonObject();
        cJSON_AddStringToObject(systemDoc.get(), "system", name);
        cJSON_AddStringToObject(systemDoc.get(), "text", "Hello");
        TypeRequest parsed;
        CHECK(!parseType(systemDoc.get(), parsed));
        CHECK(parsed.mode == (parsed.system == System::Linux ? Mode::LinuxUnicode : Mode::UsAscii));
        cJSON_AddStringToObject(systemDoc.get(), "mode", parsed.system == System::MacOS ? "macos_unicode" :
                                parsed.system == System::Windows ? "windows_unicode" : "linux_unicode");
        CHECK(!parseType(systemDoc.get(), parsed));
        CHECK(modeMatchesSystem(parsed.mode, parsed.system) && parsed.mode != Mode::UsAscii);
        const uint8_t modifier = parsed.system == System::MacOS ? 8 : 1;
        CHECK(findKey("SELECT_ALL", shortcut, parsed.system) && shortcut.modifiers == modifier && shortcut.key == 4);
        CHECK(findKey("COPY", shortcut, parsed.system) && shortcut.modifiers == modifier && shortcut.key == 6);
        CHECK(findKey("PASTE", shortcut, parsed.system) && shortcut.modifiers == modifier && shortcut.key == 25);
        CHECK(findKey("UNDO", shortcut, parsed.system) && shortcut.modifiers == modifier && shortcut.key == 29);
        CHECK(findKey("CTRL_ALT_F1", shortcut, parsed.system) && shortcut.modifiers == 5 && shortcut.key == 58);
        CHECK(findKey("CTRL_A", shortcut, parsed.system) && shortcut.modifiers == 1);
        CHECK(findKey("CMD_A", shortcut, parsed.system) && shortcut.modifiers == 8);
    }
    std::cout << "PASS: native API (Unicode JSON, null rejection, limits, target systems, modes, Ctrl/Command mapping)\n";
}
