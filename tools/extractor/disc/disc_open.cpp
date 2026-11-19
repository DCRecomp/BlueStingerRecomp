#include "disc/disc_open.h"

#include "disc/cdi_image.h"
#include "disc/track_files_image.h"
#if BS_DISC_CHD
#include "disc/chd_image.h"
#endif

#include <algorithm>
#include <cctype>

namespace bs::disc {

std::unique_ptr<DiscImage> disc_open(const std::filesystem::path& path, const std::filesystem::path& confine) {
    const auto     u8 = path.extension().u8string();
    std::string    ext(u8.begin(), u8.end());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == ".cdi") return std::make_unique<CdiImage>(path);
    if (ext == ".gdi") return TrackFilesImage::openGdi(path, confine);
    if (ext == ".cue") return TrackFilesImage::openCue(path, confine);
#if BS_DISC_CHD
    if (ext == ".chd") return std::make_unique<ChdImage>(path);
#endif
    if (ext == ".zip" || ext == ".7z")
        throw DiscError("'" + ext + "' e um pacote: extraia a imagem antes (o Builder faz isso sozinho)");
    throw DiscError("formato '" + ext + "' nao suportado (use " + disc_formats_text() + ")");
}

const char* disc_formats_text() {
#if BS_DISC_CHD
    return "GDI, CUE/BIN, CHD ou CDI";
#else
    return "GDI, CUE/BIN ou CDI";
#endif
}

} // namespace bs::disc
