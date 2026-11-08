#pragma once

#include <cstdint>
#include "hid_text.h"

namespace usb_keyboard {
void init(const uint8_t mac[6]);
uint8_t leds();

class Transport : public hidweb::Transport {
public:
    bool connected() override;
    bool send(hidweb::Stroke stroke) override;
};
}  // namespace usb_keyboard
