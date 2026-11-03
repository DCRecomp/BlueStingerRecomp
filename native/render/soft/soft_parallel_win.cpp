#include "soft_parallel.h"

#include <algorithm>
#include <execution>

namespace bs::native::render::soft {

void for_each_row(const int* rows, size_t count, const std::function<void(int)>& body) {
    std::for_each(std::execution::par, rows, rows + count, body);
}

} // namespace bs::native::render::soft
