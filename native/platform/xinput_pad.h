#pragma once
// N9-L: o XInput de reserva do InputSystem. So o Windows tem; no Linux a entrada e toda pelo SDL3.
#include "platform/input.h"

namespace bs::native::input {

bool read_system_xinput(int slot, InputSystem::XInputPad& out);  // false: sem controle nessa porta

} // namespace bs::native::input
