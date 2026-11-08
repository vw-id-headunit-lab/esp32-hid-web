#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace hidweb {

enum class System { Linux, MacOS, Windows };
enum class Mode { LinuxUnicode, UsAscii, MacOSUnicode, WindowsUnicode };

inline Mode defaultMode(System system) {
    return system == System::Linux ? Mode::LinuxUnicode : Mode::UsAscii;
}

inline bool modeMatchesSystem(Mode mode, System system) {
    return mode == Mode::UsAscii ||
        (mode == Mode::LinuxUnicode && system == System::Linux) ||
        (mode == Mode::MacOSUnicode && system == System::MacOS) ||
        (mode == Mode::WindowsUnicode && system == System::Windows);
}

struct Stroke {
    uint8_t modifiers = 0;
    uint8_t key = 0;
    Stroke(uint8_t mods = 0, uint8_t code = 0) : modifiers(mods), key(code) {}
};

// Strict UTF-8: reject truncation, overlong encodings, surrogates and > U+10FFFF.
inline bool decode(const std::string& text, size_t& offset, uint32_t& cp) {
    if (offset >= text.size()) return false;
    const uint8_t first = static_cast<uint8_t>(text[offset++]);
    if (first < 0x80) { cp = first; return true; }
    unsigned rest;
    uint32_t minimum;
    if (first >= 0xc2 && first <= 0xdf) {
        rest = 1; cp = first & 0x1f; minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
        rest = 2; cp = first & 0x0f; minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
        rest = 3; cp = first & 7; minimum = 0x10000;
    } else return false;
    if (text.size() - offset < rest) return false;
    while (rest--) {
        const uint8_t next = static_cast<uint8_t>(text[offset++]);
        if ((next & 0xc0) != 0x80) return false;
        cp = (cp << 6) | (next & 0x3f);
    }
    return cp >= minimum && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
}

// Validate the whole request before sending any key. Normalize CRLF / CR to LF.
inline const char* normalize(std::string& text, Mode mode, size_t& count) {
    std::string result;
    result.reserve(text.size());
    size_t offset = 0;
    count = 0;
    while (offset < text.size()) {
        const size_t start = offset;
        uint32_t cp;
        if (!decode(text, offset, cp)) return "文字不是有效的 UTF-8 编码";
        if (cp == '\r') {
            if (offset < text.size() && text[offset] == '\n') ++offset;
            result += '\n';
        } else {
            if ((cp < 0x20 && cp != '\n' && cp != '\t') || (cp >= 0x7f && cp <= 0x9f))
                return "文字包含不支持的控制字符";
            if (mode == Mode::UsAscii && cp > 0x7e)
                return "美式键盘模式仅支持英文、数字和符号；中文请选择对应系统的 Unicode 模式";
            if (mode == Mode::WindowsUnicode && cp > 0xffff)
                return "Windows Unicode 模式仅支持 U+FFFF 以内的字符，请移除超出范围的字符";
            result.append(text, start, offset - start);
        }
        ++count;
    }
    text = std::move(result);
    return nullptr;
}

inline Stroke asciiStroke(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return {0, static_cast<uint8_t>(4 + cp - 'a')};
    if (cp >= 'A' && cp <= 'Z') return {2, static_cast<uint8_t>(4 + cp - 'A')};
    if (cp == ' ') return {0, 0x2c};
    if (cp == '\n') return {0, 0x28};
    if (cp == '\t') return {0, 0x2b};
    const char plain[] = "1234567890-=[]\\;'`,./";
    const char shifted[] = "!@#$%^&*()_+{}|:\"~<>?";
    const uint8_t codes[] = {30,31,32,33,34,35,36,37,38,39,45,46,47,48,49,51,52,53,54,55,56};
    for (size_t i = 0; i < sizeof(codes); ++i) {
        if (cp == static_cast<uint8_t>(plain[i])) return {0, codes[i]};
        if (cp == static_cast<uint8_t>(shifted[i])) return {2, codes[i]};
    }
    return {};
}

// At most eight strokes per codepoint, independent of total text length.
class Encoder {
public:
    void reset(std::string text = {}, Mode mode = Mode::LinuxUnicode) {
        text_ = std::move(text); mode_ = mode; offset_ = index_ = length_ = 0;
    }

    bool next(Stroke& stroke, bool& endsCharacter, uint8_t* releaseModifiers = nullptr) {
        if (index_ == length_) {
            if (offset_ == text_.size()) return false;
            uint32_t cp;
            if (!decode(text_, offset_, cp)) return false;
            index_ = length_ = 0;
            if (cp <= 0x7e) {
                append(asciiStroke(cp));
            } else if (mode_ == Mode::LinuxUnicode) {
                append({3, 0x18});  // Ctrl + Shift + U.
                appendHex(cp, 0);
                append({0, 0x28});  // Commit this codepoint.
            } else if (mode_ == Mode::MacOSUnicode) {
                // Unicode Hex Input consumes UTF-16 units while Option stays down.
                if (cp <= 0xffff) appendHex(cp, 4);
                else {
                    cp -= 0x10000;
                    appendHex(0xd800 | (cp >> 10), 4);
                    appendHex(0xdc00 | (cp & 0x3ff), 4);
                }
                pending_[length_ - 1].release = 0;
            } else if (mode_ == Mode::WindowsUnicode) {
                if (cp > 0xffff) return false;
                append({4, 0x57}, 4);  // Hold Alt + keypad Plus (EnableHexNumpad).
                appendHex(cp, 4);
                pending_[length_ - 1].release = 0;  // Releasing Alt commits the value.
            } else return false;
        }
        const auto& step = pending_[index_++];
        stroke = step.stroke;
        if (releaseModifiers) *releaseModifiers = step.release;
        endsCharacter = index_ == length_;
        return true;
    }

private:
    void append(Stroke stroke, uint8_t release = 0) {
        pending_[length_++] = {stroke, release};
    }

    void appendHex(uint32_t cp, unsigned width) {
        bool leading = width == 0;
        for (int shift = width ? static_cast<int>(width * 4 - 4) : 20; shift >= 0; shift -= 4) {
            const uint8_t digit = (cp >> shift) & 15;
            if (digit == 0 && leading && shift != 0) continue;
            leading = false;
            Stroke stroke = asciiStroke("0123456789abcdef"[digit]);
            if (mode_ == Mode::MacOSUnicode || mode_ == Mode::WindowsUnicode) {
                stroke.modifiers = 4;  // Left Option / Alt throughout the hex sequence.
                if (mode_ == Mode::WindowsUnicode && digit < 10)
                    stroke.key = digit == 0 ? 0x62 : static_cast<uint8_t>(0x58 + digit);
                append(stroke, 4);
            } else append(stroke);
        }
    }

    std::string text_;
    Mode mode_ = Mode::LinuxUnicode;
    size_t offset_ = 0, index_ = 0, length_ = 0;
    struct Step { Stroke stroke; uint8_t release; };
    Step pending_[8];
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool connected() = 0;
    // Return true only when the USB stack confirms the report was transferred.
    virtual bool send(Stroke stroke) = 0;
};

enum class State { Idle, Waiting, Sending, Done, Stopped, Error };

class Sender {
public:
    explicit Sender(Transport& transport) : transport_(transport) {}

    const char* start(std::string text, Mode mode, uint32_t interval, uint32_t delay,
                      uint32_t now) {
        if (busy()) return "已有发送任务，请等待完成或停止";
        if (releasing()) return "键盘正在释放按键，请稍后重试";
        if (text.empty()) return "请输入要发送的文字";
        size_t count;
        if (const char* error = normalize(text, mode, count)) return error;
        if (!transport_.connected()) return "USB 未连接到目标设备";
        encoder_.reset(std::move(text), mode);
        mode_ = mode;
        begin(count, interval, delay, now);
        single_ = false;
        return nullptr;
    }

    const char* startKey(Stroke stroke, uint32_t now) {
        if (busy()) return "已有发送任务，请等待完成或停止";
        if (releasing()) return "键盘正在释放按键，请稍后重试";
        if (!transport_.connected()) return "USB 未连接到目标设备";
        begin(1, 25, 0, now);
        single_ = true;
        singleStroke_ = stroke;
        return nullptr;
    }

    void stop() {
        if (busy()) state = State::Stopped;
        encoder_.reset();
        keyDown_ = false;
        cleanup_ = 1;
    }

    void tick(uint32_t now) {
        if (!transport_.connected()) {
            if (busy()) fail("USB 已断开或挂起，发送已终止");
            cleanup_ = 1;
            return;
        }
        // Always release keys on boot / cancellation / reconnection. If stopped
        // halfway through Unicode input, dismiss that composition with Escape.
        if (cleanup_) {
            const Stroke stroke = cleanup_ == 2 ? Stroke{0, 0x29} : Stroke{};
            if (transport_.send(stroke)) {
                heldModifiers_ = 0;
                if (cleanup_ == 1 && composing_) cleanup_ = 2;
                else if (cleanup_ == 2) cleanup_ = 3;
                else { cleanup_ = 0; composing_ = false; }
            }
            return;
        }
        if (!busy() || static_cast<int32_t>(now - nextAt_) < 0) return;
        state = State::Sending;
        if (keyDown_) {
            if (!transport_.send({releaseModifiers_, 0})) { fail("USB 松键报告发送失败，任务已终止"); return; }
            keyDown_ = false;
            heldModifiers_ = releaseModifiers_;
            if (endsCharacter_) { ++completed; composing_ = false; }
            if (completed == total) { state = State::Done; encoder_.reset(); }
            nextAt_ = now + interval_;
        } else {
            Stroke stroke;
            if (single_) { stroke = singleStroke_; endsCharacter_ = true; releaseModifiers_ = 0; }
            else if (!encoder_.next(stroke, endsCharacter_, &releaseModifiers_)) {
                fail("文字编码失败，任务已终止"); return;
            }
            // Mark composition before sending: a timed-out report may still arrive.
            if (!single_ && mode_ == Mode::LinuxUnicode && stroke.modifiers == 3 && stroke.key == 0x18)
                composing_ = true;
            if (!transport_.send(stroke)) { fail("USB 按键报告发送失败，任务已终止"); return; }
            keyDown_ = true;
            heldModifiers_ = stroke.modifiers;
            nextAt_ = now + 8;
        }
    }

    bool busy() const { return state == State::Waiting || state == State::Sending; }
    bool releasing() const { return cleanup_ != 0; }
    bool holdsKeys() const { return keyDown_ || heldModifiers_ != 0 || cleanup_ == 3; }
    uint32_t remainingDelay(uint32_t now) const {
        return state == State::Waiting && static_cast<int32_t>(nextAt_ - now) > 0 ? nextAt_ - now : 0;
    }

    State state = State::Idle;
    size_t completed = 0, total = 0;
    uint32_t jobId = 0;
    const char* error = "";

private:
    void begin(size_t count, uint32_t interval, uint32_t delay, uint32_t now) {
        total = count; completed = 0; error = ""; ++jobId;
        interval_ = interval; nextAt_ = now + delay;
        keyDown_ = composing_ = false;
        releaseModifiers_ = heldModifiers_ = 0;
        state = delay ? State::Waiting : State::Sending;
    }

    void fail(const char* reason) {
        state = State::Error; error = reason; stop();
    }

    Transport& transport_;
    Encoder encoder_;
    Mode mode_ = Mode::LinuxUnicode;
    uint32_t nextAt_ = 0, interval_ = 25;
    bool keyDown_ = false, endsCharacter_ = false, single_ = false, composing_ = false;
    Stroke singleStroke_;
    uint8_t cleanup_ = 1;
    uint8_t releaseModifiers_ = 0, heldModifiers_ = 0;
};

}  // namespace hidweb
