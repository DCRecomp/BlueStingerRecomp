// Teclado e controle lidos direto do Windows, para BS_INPUT=host (desenvolvimento). No jogo a entrada e a do InputSystem.
#include "input/input.h"

#include <algorithm>
#include <cstdlib>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>

namespace bs {
namespace {

// --- XInput ---

class XInput : public InputSource {
public:
    PadState read(int port, uint64_t) override {
        PadState s;
        if (port != 0) return s;
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            XINPUT_STATE x{};
            if (XInputGetState(i, &x) != ERROR_SUCCESS) continue;
            const WORD b = x.Gamepad.wButtons;
            struct M {
                WORD     x;
                uint16_t dc;
            } static const kMap[] = {{XINPUT_GAMEPAD_A, kPadA},         {XINPUT_GAMEPAD_B, kPadB},
                                     {XINPUT_GAMEPAD_X, kPadX},         {XINPUT_GAMEPAD_Y, kPadY},
                                     {XINPUT_GAMEPAD_START, kPadStart}, {XINPUT_GAMEPAD_DPAD_UP, kPadUp},
                                     {XINPUT_GAMEPAD_DPAD_DOWN, kPadDown}, {XINPUT_GAMEPAD_DPAD_LEFT, kPadLeft},
                                     {XINPUT_GAMEPAD_DPAD_RIGHT, kPadRight}};
            for (const M& m : kMap)
                if (b & m.x) s.buttons |= m.dc;
            s.ltrig = x.Gamepad.bLeftTrigger;
            s.rtrig = x.Gamepad.bRightTrigger;
            // XInput: -32768..32767, y para cima; Dreamcast: 0..255, y para baixo
            auto axis = [](SHORT v, bool invert) {
                int t = (int(v) + 32768) >> 8;
                if (invert) t = 255 - t;
                return uint8_t(std::clamp(t, 0, 255));
            };
            const SHORT dz = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
            const SHORT lx = std::abs(x.Gamepad.sThumbLX) < dz ? 0 : x.Gamepad.sThumbLX;
            const SHORT ly = std::abs(x.Gamepad.sThumbLY) < dz ? 0 : x.Gamepad.sThumbLY;
            s.joyx = lx ? axis(lx, false) : 0x80;
            s.joyy = ly ? axis(ly, true) : 0x80;
            break;
        }
        return s;
    }
};

} // namespace

bool host_key_down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
std::unique_ptr<InputSource> make_xinput_input() { return std::make_unique<XInput>(); }

} // namespace bs
