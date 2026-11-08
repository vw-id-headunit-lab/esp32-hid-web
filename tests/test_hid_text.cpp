#include "hid_text.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; std::exit(1); \
} } while (0)

using namespace hidweb;

bool equals(Stroke a, Stroke b) { return a.modifiers == b.modifiers && a.key == b.key; }

struct FakeUsb : Transport {
    bool up = true;
    bool failNext = false;
    std::vector<Stroke> reports;
    bool connected() override { return up; }
    bool send(Stroke stroke) override {
        if (failNext) { failNext = false; return false; }
        reports.push_back(stroke);
        return true;
    }
};

void drain(Sender& sender, uint32_t& clock) {
    for (unsigned ticks = 0; ticks < 100000 && (sender.busy() || sender.releasing()); ++ticks)
        sender.tick(clock++);
    CHECK(!sender.busy() && !sender.releasing());
}

void testValidation() {
    std::string text = "你好😀\r\nA\rB\tC";
    size_t count;
    CHECK(!normalize(text, Mode::LinuxUnicode, count));
    CHECK(text == "你好😀\nA\nB\tC" && count == 9);
    std::string ascii = "a你好";
    CHECK(normalize(ascii, Mode::UsAscii, count));
    CHECK(ascii == "a你好");
    const std::vector<std::string> invalid = {
        "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe4\xb8", "\x80",
        std::string("a\0b", 3), "\x1b", "\x7f", "\xc2\x85"
    };
    for (auto bad : invalid) CHECK(normalize(bad, Mode::LinuxUnicode, count));
    auto largest = std::string("\xf4\x8f\xbf\xbf");
    CHECK(!normalize(largest, Mode::LinuxUnicode, count) && count == 1);
}

void testAsciiMapping() {
    for (uint32_t cp = 32; cp <= 126; ++cp) CHECK(asciiStroke(cp).key != 0);
    CHECK(equals(asciiStroke('a'), {0, 4}));
    CHECK(equals(asciiStroke('A'), {2, 4}));
    CHECK(equals(asciiStroke('@'), {2, 31}));
    CHECK(equals(asciiStroke('"'), {2, 52}));
    CHECK(equals(asciiStroke('\\'), {0, 49}));
    CHECK(equals(asciiStroke('~'), {2, 53}));
    CHECK(equals(asciiStroke('\n'), {0, 40}));
    CHECK(equals(asciiStroke('\t'), {0, 43}));
}

void testUnicodeSequence() {
    Encoder encoder;
    encoder.reset("你😀");
    // U+4F60, U+1F600: chord, hex digits, Enter; every character ends at Enter.
    const Stroke expected[] = {
        {3,24}, {0,33}, {0,9}, {0,35}, {0,39}, {0,40},
        {3,24}, {0,30}, {0,9}, {0,35}, {0,39}, {0,39}, {0,40}
    };
    for (size_t i = 0; i < sizeof(expected)/sizeof(expected[0]); ++i) {
        Stroke stroke; bool end;
        CHECK(encoder.next(stroke, end));
        CHECK(equals(stroke, expected[i]));
        CHECK(end == (i == 5 || i == 12));
    }
    Stroke stroke; bool end;
    CHECK(!encoder.next(stroke, end));
}

void testSendingAndDelay() {
    FakeUsb usb;
    Sender sender(usb);
    uint32_t clock = 100;
    drain(sender, clock);
    usb.reports.clear();
    CHECK(!sender.start("A你\r\n", Mode::LinuxUnicode, 25, 2000, clock));
    CHECK(sender.total == 3 && sender.remainingDelay(clock) == 2000);
    sender.tick(clock + 1999);
    CHECK(usb.reports.empty());
    CHECK(sender.start("duplicate", Mode::UsAscii, 25, 0, clock));
    clock += 2000;
    drain(sender, clock);
    CHECK(sender.state == State::Done && sender.completed == 3);
    CHECK(usb.reports.size() == 16);  // A + six Unicode strokes + Enter, each with a release.
    CHECK(equals(usb.reports.front(), {2,4}));
    for (size_t i = 1; i < usb.reports.size(); i += 2) CHECK(equals(usb.reports[i], {}));
}

void testStopAndReconnect() {
    FakeUsb usb;
    Sender sender(usb);
    uint32_t clock = 0;
    drain(sender, clock);
    CHECK(!sender.start("你好", Mode::LinuxUnicode, 25, 0, clock));
    sender.tick(clock++);  // Unicode composition started, Ctrl+Shift+U down.
    CHECK(sender.holdsKeys());
    sender.stop();
    CHECK(sender.state == State::Stopped && sender.completed == 0);
    CHECK(sender.start("too early", Mode::UsAscii, 25, 0, clock));
    const size_t before = usb.reports.size();
    drain(sender, clock);
    CHECK(usb.reports.size() == before + 3);
    CHECK(equals(usb.reports[before], {}));
    CHECK(equals(usb.reports[before+1], {0,41}));  // Escape cancels partial Unicode input.
    CHECK(equals(usb.reports.back(), {}));
    CHECK(!sender.holdsKeys());
    CHECK(!sender.start("abc", Mode::UsAscii, 25, 0, clock));
    sender.tick(clock++);
    usb.up = false;
    sender.tick(clock++);
    CHECK(sender.state == State::Error && sender.completed == 0);
    CHECK(sender.start("offline", Mode::UsAscii, 25, 0, clock));
    usb.up = true;
    drain(sender, clock);
    CHECK(sender.state == State::Error);  // Reconnection must not resume the old text.
    CHECK(equals(usb.reports.back(), {}));
}

void testTransferFailureAndKeys() {
    FakeUsb usb;
    Sender sender(usb);
    uint32_t clock = 0;
    drain(sender, clock);
    CHECK(!sender.startKey({1,4}, clock));
    sender.tick(clock++);
    usb.failNext = true;  // Fail the release, which must not count as a completed key.
    clock += 8;
    sender.tick(clock++);
    CHECK(sender.state == State::Error && sender.completed == 0);
    drain(sender, clock);
    CHECK(equals(usb.reports.back(), {}));
    CHECK(!sender.startKey({0,40}, clock));
    drain(sender, clock);
    CHECK(sender.state == State::Done && sender.completed == 1);
}

void testMillisWraparoundAndAtomicValidation() {
    FakeUsb usb;
    Sender sender(usb);
    uint32_t clock = 0xfffffff0;
    drain(sender, clock);
    usb.reports.clear();
    CHECK(sender.start("abc中文", Mode::UsAscii, 25, 0, clock));
    CHECK(usb.reports.empty() && sender.state == State::Idle);
    CHECK(!sender.start("Z", Mode::UsAscii, 25, 50, clock));
    sender.tick(clock + 49);
    CHECK(usb.reports.empty());
    clock += 50;
    drain(sender, clock);
    CHECK(sender.state == State::Done && sender.completed == 1);
}

void checkAltReports(const std::vector<Stroke>& actual, const std::vector<uint8_t>& keys,
                     const std::vector<size_t>& ends) {
    CHECK(actual.size() == keys.size() * 2);
    for (size_t i = 0; i < keys.size(); ++i) {
        CHECK(equals(actual[i * 2], {4, keys[i]}));
        bool last = false;
        for (size_t end : ends) if (i == end) last = true;
        CHECK(equals(actual[i * 2 + 1], {static_cast<uint8_t>(last ? 0 : 4), 0}));
    }
}

void testMacOSUnicode() {
    FakeUsb usb;
    Sender sender(usb);
    uint32_t clock = 0;
    drain(sender, clock);
    usb.reports.clear();
    CHECK(!sender.start("你😀", Mode::MacOSUnicode, 25, 0, clock));
    drain(sender, clock);
    // UTF-16: 4f60, then d83d de00, with Option held across all eight surrogate digits.
    checkAltReports(usb.reports, {33,9,35,39, 7,37,32,7,7,8,39,39}, {3,11});
    CHECK(sender.state == State::Done && sender.completed == 2 && !sender.holdsKeys());
    CHECK(!sender.start("A\n", Mode::MacOSUnicode, 25, 0, clock));
    const size_t before = usb.reports.size();
    drain(sender, clock);
    CHECK(equals(usb.reports[before], {2,4}));
    CHECK(equals(usb.reports[before+1], {}));
    CHECK(equals(usb.reports[before+2], {0,40}));
    CHECK(equals(usb.reports.back(), {}));
}

void testWindowsUnicode() {
    FakeUsb usb;
    Sender sender(usb);
    uint32_t clock = 0;
    drain(sender, clock);
    usb.reports.clear();
    CHECK(sender.start("abc😀", Mode::WindowsUnicode, 25, 0, clock));
    CHECK(usb.reports.empty() && sender.state == State::Idle);
    CHECK(!sender.start("你é", Mode::WindowsUnicode, 25, 0, clock));
    drain(sender, clock);
    // Alt + keypad Plus + four hex digits. Digits use keypad, letters use US keys.
    checkAltReports(usb.reports, {87,92,9,94,98, 87,98,98,8,97}, {4,9});
    CHECK(sender.state == State::Done && sender.completed == 2 && !sender.holdsKeys());
}

void testHeldAltCancellationAndFailure() {
    for (Mode mode : {Mode::MacOSUnicode, Mode::WindowsUnicode}) {
        FakeUsb usb;
        Sender sender(usb);
        uint32_t clock = 0;
        drain(sender, clock);
        CHECK(!sender.start("你好", mode, 25, 0, clock));
        sender.tick(clock++);
        clock += 8;
        sender.tick(clock++);  // Digit released, Alt is still down.
        CHECK(sender.holdsKeys() && equals(usb.reports.back(), {4,0}));
        const size_t before = usb.reports.size();
        sender.stop();
        drain(sender, clock);
        CHECK(sender.state == State::Stopped && sender.completed == 0);
        CHECK(usb.reports.size() == before + 1 && equals(usb.reports.back(), {}));
        CHECK(!sender.holdsKeys());

        CHECK(!sender.start("你", mode, 25, 0, clock));
        sender.tick(clock++);
        clock += 8;
        sender.tick(clock++);
        usb.failNext = true;
        clock += 25;
        sender.tick(clock++);
        CHECK(sender.state == State::Error && sender.completed == 0);
        drain(sender, clock);
        CHECK(!sender.holdsKeys() && equals(usb.reports.back(), {}));

        CHECK(!sender.start("你", mode, 25, 0, clock));
        sender.tick(clock++);
        usb.up = false;
        sender.tick(clock++);
        CHECK(sender.state == State::Error);
        usb.up = true;
        drain(sender, clock);
        CHECK(sender.state == State::Error && sender.completed == 0);
        CHECK(!sender.holdsKeys() && equals(usb.reports.back(), {}));
    }
}

int main() {
    testValidation();
    testAsciiMapping();
    testUnicodeSequence();
    testSendingAndDelay();
    testStopAndReconnect();
    testTransferFailureAndKeys();
    testMillisWraparoundAndAtomicValidation();
    testMacOSUnicode();
    testWindowsUnicode();
    testHeldAltCancellationAndFailure();
    std::cout << "PASS: 10 suites (UTF-8, ASCII, Linux/macOS/Windows Unicode, held modifiers, cancellation, USB failures, clock wrap)\n";
}
