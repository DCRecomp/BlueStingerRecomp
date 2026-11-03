#pragma once
// N9-L: linhas do rasterizador em paralelo. Windows: std::execution::par. Linux: pool proprio, sem TBB (soft_parallel_posix.cpp).
// `body` e chamado uma vez para cada elemento, em qualquer ordem e em qualquer thread; volta depois de todos.
#include <cstddef>
#include <functional>

namespace bs::native::render::soft {

void for_each_row(const int* rows, size_t count, const std::function<void(int)>& body);

} // namespace bs::native::render::soft
