#include "render/texture_packs.h"

#include "core/path_case.h"
#include "localization/mod_packages.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <new>

// stb_image (third_party/stb, dominio publico): so o PNG, da memoria, sem as rotinas de arquivo
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS (1 << 14)
#include "stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fs = std::filesystem;

namespace bs::native::render {

namespace {

std::string u8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool read_file(const fs::path& p, uint64_t limit, std::string& out, std::string& why) {
    std::error_code ec;
    const auto      size = fs::file_size(p, ec);
    if (ec) return why = "ilegivel (" + ec.message() + ")", false;
    if (size > limit) return why = "PNG de " + std::to_string(size) + " bytes, acima do limite de " + std::to_string(limit) + " bytes", false;
    std::ifstream in(p, std::ios::binary);
    out.resize(size_t(size));
    if (!in || !in.read(out.data(), std::streamsize(size))) return why = "ilegivel", false;
    return true;
}

} // namespace

TexturePacks TexturePacks::discover(const fs::path& modsDir, std::string_view gameVersion, std::vector<std::string>* problems) {
    const auto           mods = localization::ModPackages::discover(modsDir, gameVersion);
    std::vector<Package> list;
    for (const auto& m : mods.packages()) {  // ja na ordem de prioridade (maior primeiro)
        std::error_code ec;
        const fs::path  tex = resolve_case(m.dir, L"texturas");
        if (!fs::is_directory(tex, ec)) continue;
        Package p;
        p.id = m.id, p.name = m.name, p.version = m.version, p.priority = m.priority, p.dir = m.dir;
        list.push_back(std::move(p));
    }
    // um pacote recusado pelo mod.toml so interessa se tem texturas (os de legendas e audio sao avisados por quem os usa)
    if (problems)
        for (const auto& pr : mods.problems()) {
            std::error_code ec;
            if (fs::is_directory(resolve_case(pr.dir, L"texturas"), ec))
                problems->push_back("pacote " + u8(pr.dir.filename()) + " recusado: " + pr.message);
        }
    return from_dirs(list, problems);
}

TexturePacks TexturePacks::from_dirs(const std::vector<Package>& packages, std::vector<std::string>* problems) {
    TexturePacks out;
    out.packages_ = packages;
    for (uint32_t pi = 0; pi < out.packages_.size(); ++pi) {
        Package&        pk = out.packages_[pi];
        std::error_code ec;
        const fs::path  dir = resolve_case(pk.dir, L"texturas");
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec)) files.push_back(e.path());
        std::sort(files.begin(), files.end());
        uint64_t    bad = 0;
        std::string firstBad;
        for (const fs::path& f : files) {
            std::string ext = u8(f.extension());
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (ext != ".png") continue;  // outros arquivos (leia-me, fontes do autor) nao contam
            const auto id = parse_texture_id(u8(f.stem()));
            if (!id) {
                if (!bad++) firstBad = u8(f.filename());
                continue;
            }
            auto [it, fresh] = out.index_.try_emplace(*id);
            if (!fresh) {  // um pacote de prioridade maior ja tem esta (ou o mesmo pacote com outra caixa no nome)
                ++out.stats_.overridden;
                continue;
            }
            it->second.file = f, it->second.package = pi;
            ++pk.textures;
        }
        out.stats_.bad_names += bad;
        if (bad && problems)
            problems->push_back("pacote " + pk.id + ": " + std::to_string(bad) + " PNG com nome que nao e uma textura (" +
                                firstBad + (bad > 1 ? ", ..." : "") + "): ignorados");
    }
    out.stats_.indexed = out.index_.size();
    return out;
}

void TexturePacks::reject(const TextureId& id, Entry& e, const std::string& why) {
    e.state = Entry::State::Rejected;
    e.tex   = {};
    ++stats_.rejected;
    rejections_.push_back(id.text() + ": " + why + " (" + packages_[e.package].id + ")");
}

bool TexturePacks::deferred(const TextureId& id) const {
    const auto it = index_.find(id);
    return it != index_.end() && it->second.state == Entry::State::Deferred;
}

const ReplacementTexture* TexturePacks::find(const TextureId& id) {
    const auto it = index_.find(id);
    if (it == index_.end()) return nullptr;
    Entry& e = it->second;
    if (e.state == Entry::State::Loaded) return &e.tex;
    if (e.state == Entry::State::Rejected) return nullptr;
    // o limite do quadro: a primeira do render sempre passa (uma textura grande nunca fica adiada para sempre)
    if (frameCount_ >= limits_.per_frame || (frameCount_ && frameBytes_ >= limits_.bytes_per_frame)) {
        if (e.state != Entry::State::Deferred) e.state = Entry::State::Deferred;
        ++stats_.deferred;
        return nullptr;
    }
    const bool first = frameCount_ == 0;
    std::string file, why;
    if (!read_file(e.file, limits_.max_file, file, why)) return ++frameCount_, reject(id, e, why), nullptr;
    int w = 0, h = 0, comp = 0;
    const auto* data = reinterpret_cast<const stbi_uc*>(file.data());
    if (file.size() > size_t(INT32_MAX) || !stbi_info_from_memory(data, int(file.size()), &w, &h, &comp))
        return ++frameCount_, reject(id, e, std::string("PNG ilegivel (") + stbi_failure_reason() + ")"), nullptr;
    const uint32_t uw = uint32_t(w), uh = uint32_t(h);
    if (uw > limits_.max_side || uh > limits_.max_side)
        return ++frameCount_, reject(id, e, std::to_string(uw) + "x" + std::to_string(uh) + " acima do limite de " +
                                                std::to_string(limits_.max_side)), nullptr;
    // as dimensoes antes de decodificar (o mesmo criterio de replacement_compatible)
    if (uw % id.width || uh % id.height || uw / id.width != uh / id.height || uw / id.width < 1 || uw / id.width > 16)
        return ++frameCount_, reject(id, e, std::to_string(uw) + "x" + std::to_string(uh) + " nao e um fator inteiro de 1 a 16 de " +
                                                std::to_string(id.width) + "x" + std::to_string(id.height)), nullptr;
    // os bytes decodificados conhecidos antes de decodificar (max_side 8192: cabe em 64 bits sem estouro)
    const uint64_t decoded = uint64_t(uw) * uh * 4;
    // o orcamento da sessao (texels guardados na RAM): recusa deterministica, a textura fica com o original
    if (stats_.bytes + decoded > limits_.max_resident)
        return ++frameCount_, reject(id, e, "orcamento de " + std::to_string(limits_.max_resident >> 20) + " MiB das texturas HD esgotado"),
               nullptr;
    // o limite do quadro pela soma: a que passaria da folga fica para o render seguinte (a primeira sempre passa); as
    // seguintes deste render tambem, sem ler o arquivo
    if (!first && decoded > limits_.bytes_per_frame - std::min(frameBytes_, limits_.bytes_per_frame)) {
        frameBytes_ = limits_.bytes_per_frame;
        if (e.state != Entry::State::Deferred) e.state = Entry::State::Deferred;
        ++stats_.deferred;
        return nullptr;
    }
    ++frameCount_;
    try {
        std::unique_ptr<stbi_uc, void (*)(void*)> px(stbi_load_from_memory(data, int(file.size()), &w, &h, &comp, 4), stbi_image_free);
        if (!px || uint32_t(w) != uw || uint32_t(h) != uh)
            return reject(id, e, std::string("PNG ilegivel (") + (px ? "dimensoes" : stbi_failure_reason()) + ")"), nullptr;
        e.tex.width = uw, e.tex.height = uh;
        e.tex.argb.resize(size_t(uw) * uh);
        const stbi_uc* s = px.get();
        for (uint32_t& t : e.tex.argb)  // RGBA (o PNG) para 0xAARRGGBB (o contrato)
            t = (uint32_t(s[3]) << 24) | (uint32_t(s[0]) << 16) | (uint32_t(s[1]) << 8) | s[2], s += 4;
    } catch (const std::bad_alloc&) {  // sem memoria: so esta textura recua, o jogo segue
        return reject(id, e, "sem memoria para decodificar"), nullptr;
    }
    e.state = Entry::State::Loaded;
    frameBytes_ += decoded;
    ++stats_.loaded;
    stats_.bytes += uint64_t(e.tex.argb.size()) * 4;
    stats_.max_factor = std::max(stats_.max_factor, uw / id.width);
    return &e.tex;
}

} // namespace bs::native::render
