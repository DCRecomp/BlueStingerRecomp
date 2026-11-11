#include "platform/edition.h"

namespace bs::native::edition {

namespace st = settings;

const char* edition_name(Edition e) { return e == Edition::Remastered ? "remastered" : "original"; }

bool parse_edition(const std::string& v, Edition* out) {
    if (v == "original") return *out = Edition::Original, true;
    if (v == "remastered") return *out = Edition::Remastered, true;
    return false;
}

EditionProfile profile_for(Edition e) {
    EditionProfile p;
    p.edition              = e;
    p.free_camera_required = e == Edition::Remastered;
    p.native_menus         = e == Edition::Remastered;
    p.wide_frame           = e == Edition::Remastered;
    p.enhanced_water       = e == Edition::Remastered;
    p.lighting_effects     = e == Edition::Remastered;
    return p;
}

st::CameraSettings clamp_to(st::CameraSettings s, const EditionProfile& p, std::vector<std::string>* notes) {
    if (!p.free_camera_required) return s;
    if (s.mode != st::CameraMode::Free) {
        s.mode = st::CameraMode::Free;
        if (notes) notes->push_back("camera original ignorada: a edicao exige a camera livre");
    }
    if (s.movement != st::MoveMode::Camera) {
        s.movement = st::MoveMode::Camera;
        if (notes) notes->push_back("movimento original ignorado: a edicao exige o movimento relativo a camera");
    }
    return s;
}

st::CameraSettings for_file(const st::CameraSettings& applied, const st::CameraSettings& persisted, const EditionProfile& p) {
    st::CameraSettings d = applied;
    if (p.free_camera_required) d.mode = persisted.mode, d.movement = persisted.movement;
    return d;
}

} // namespace bs::native::edition
