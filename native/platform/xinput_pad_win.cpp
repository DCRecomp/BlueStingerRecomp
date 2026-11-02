#include "platform/xinput_pad.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>

namespace bs::native::input {

bool read_system_xinput(int slot, InputSystem::XInputPad& out) {
    XINPUT_STATE x{};
    if (XInputGetState(DWORD(slot), &x) != ERROR_SUCCESS) return false;
    out = {x.Gamepad.wButtons, x.Gamepad.bLeftTrigger, x.Gamepad.bRightTrigger, x.Gamepad.sThumbLX,
           x.Gamepad.sThumbLY,  x.Gamepad.sThumbRX,     x.Gamepad.sThumbRY};
    return true;
}

} // namespace bs::native::input
