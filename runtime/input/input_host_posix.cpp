// BS_INPUT=host le o teclado e o XInput do Windows direto. No Linux a entrada do jogo e a do InputSystem, pelo SDL3.
#include "input/input.h"

#include <stdexcept>

namespace bs {

bool host_key_down(int) { return false; }
std::unique_ptr<InputSource> make_xinput_input() { throw std::runtime_error("BS_INPUT=host so existe no Windows"); }

} // namespace bs
