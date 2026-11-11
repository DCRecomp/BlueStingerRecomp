#include "render/water.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bs::native::render {

bool water_extent(const CommandList& cl, const std::function<bool(const Draw&)>& is_water, float& top, float& bottom) {
    float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
    for (const Draw& d : cl.draws) {
        if (d.first_vertex > cl.vertices.size() || d.vertex_count > cl.vertices.size() - d.first_vertex || !is_water(d))
            continue;
        for (uint32_t k = 0; k < d.vertex_count; ++k) {
            const float y = cl.vertices[d.first_vertex + k].y;
            if (std::isfinite(y)) lo = std::min(lo, y), hi = std::max(hi, y);
        }
    }
    if (!(lo <= hi)) return false;
    top = lo, bottom = std::max(hi, lo + 1.f);
    return true;
}

} // namespace bs::native::render
