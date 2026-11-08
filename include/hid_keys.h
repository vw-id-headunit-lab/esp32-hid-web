#pragma once

#include "hid_text.h"

namespace hidweb {

inline bool findKey(const std::string& name, Stroke& stroke, System system = System::Linux) {
    const uint8_t editModifier = system == System::MacOS ? 0x08 : 0x01;
    if (name == "SELECT_ALL") { stroke = {editModifier, 0x04}; return true; }
    if (name == "COPY") { stroke = {editModifier, 0x06}; return true; }
    if (name == "PASTE") { stroke = {editModifier, 0x19}; return true; }
    if (name == "UNDO") { stroke = {editModifier, 0x1d}; return true; }
    struct NamedKey { const char* name; Stroke stroke; };
    static const NamedKey keys[] = {
        {"ENTER", {0, 0x28}}, {"TAB", {0, 0x2b}}, {"ESC", {0, 0x29}},
        {"BACKSPACE", {0, 0x2a}}, {"DELETE", {0, 0x4c}},
        {"UP", {0, 0x52}}, {"DOWN", {0, 0x51}}, {"LEFT", {0, 0x50}}, {"RIGHT", {0, 0x4f}},
        {"HOME", {0, 0x4a}}, {"END", {0, 0x4d}},
        {"CTRL_A", {1, 0x04}}, {"CTRL_C", {1, 0x06}}, {"CTRL_V", {1, 0x19}},
        {"CTRL_Z", {1, 0x1d}}, {"CTRL_SHIFT_SPACE", {3, 0x2c}},
        {"CMD_A", {8, 0x04}}, {"CMD_C", {8, 0x06}}, {"CMD_V", {8, 0x19}}, {"CMD_Z", {8, 0x1d}},
        {"CTRL_ALT_F1", {0x05, 0x3a}}
    };
    for (const auto& key : keys) {
        if (name == key.name) { stroke = key.stroke; return true; }
    }
    return false;
}

}  // namespace hidweb
