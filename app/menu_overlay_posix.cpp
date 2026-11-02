// N9-L: o desenho do menu com FreeType entra na L4. Sem backend, ok() e falso.
#include "menu_overlay.h"

namespace bs {

struct MenuOverlay::Impl {
    std::vector<uint32_t> empty;
};

MenuOverlay::MenuOverlay() : impl_(std::make_unique<Impl>()) {}
MenuOverlay::~MenuOverlay() = default;
bool MenuOverlay::ok() const { return false; }
const std::vector<uint32_t>& MenuOverlay::layer(const MenuView&, uint32_t, uint32_t, float, float, float, float) { return impl_->empty; }
std::optional<MenuHit> MenuOverlay::hit(float, float) const { return std::nullopt; }
uint64_t MenuOverlay::version() const { return 0; }

} // namespace bs
