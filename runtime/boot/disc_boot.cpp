#include "boot/disc_boot.h"

#include "boot_identity.h"
#include "disc/disc_open.h"
#include "disc/ip_bin.h"
#include "disc/iso9660.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace bs {

BootFiles boot_read_disc(const std::filesystem::path& image, const std::filesystem::path& confine) {
    BootFiles f;
    try {
        const auto  imgp = disc::disc_open(image, confine);
        const auto& img  = *imgp;
        const auto& boot = img.bootTrack();
        f.ipBin.resize(disc::kIpBinSize);
        for (uint32_t i = 0; i < disc::kIpBinSize / disc::kUserDataSize; ++i)
            img.readUserData(boot.start_lba + i, f.ipBin.data() + i * disc::kUserDataSize);
        const disc::IpBin ip = disc::parseIpBin(f.ipBin.data(), f.ipBin.size());
        f.product = ip.product, f.version = ip.version, f.title = ip.title;
        disc::Iso9660 iso(img, boot.start_lba + 16);
        const auto*   e = iso.find(ip.boot_file);
        if (!e || e->is_dir) throw std::runtime_error("executavel de boot " + ip.boot_file + " nao encontrado");
        BootIdentity id = identify_boot(iso.readAll(*e), boot.start_lba);
        if (!id.version) throw std::runtime_error(boot_rejection_text(ip.product, ip.version, id));
        f.versionId = id.version->id;
        f.firstRead = std::move(id.data);
    } catch (const disc::DiscError& err) {
        throw std::runtime_error(std::string("imagem de disco: ") + err.what());
    }
    return f;
}

} // namespace bs
