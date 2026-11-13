#include "platform/graphics_settings.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace bs::native::settings {
namespace {

template <class E> struct Names;
template <> struct Names<Resolution> {
    static constexpr const char* v[] = {"original", "720p", "1080p", "1440p", "2160p"};
};
template <> struct Names<WindowMode> {
    static constexpr const char* v[] = {"janela", "tela_cheia", "sem_bordas"};
};
template <> struct Names<Aspect> {
    static constexpr const char* v[] = {"4:3", "16:9", "auto"};
};
template <> struct Names<Filter> {
    static constexpr const char* v[] = {"original", "bilinear", "anisotropica"};
};
template <> struct Names<Shadows> {
    static constexpr const char* v[] = {"original", "aprimoradas"};
};
template <> struct Names<Water> {
    static constexpr const char* v[] = {"original", "aprimorada", "alta"};
};
template <> struct Names<SsaoQuality> {
    static constexpr const char* v[] = {"baixa", "media", "alta"};
};

template <class E> const char* name(E e) { return Names<E>::v[size_t(e)]; }
template <class E> bool parse(const std::string& s, E& out) {
    for (size_t i = 0; i < std::size(Names<E>::v); ++i)
        if (s == Names<E>::v[i]) {
            out = E(i);
            return true;
        }
    return false;
}

// a parte do preset (o resto e do jogador)
bool same_quality(const GraphicsSettings& a, const GraphicsSettings& b) {
    return a.resolution == b.resolution && a.scale_pct == b.scale_pct && a.filter == b.filter &&
           (a.filter != Filter::Anisotropic || a.anisotropy == b.anisotropy) && a.msaa == b.msaa &&
           a.hd_textures == b.hd_textures && a.lighting == b.lighting && a.shadows == b.shadows && a.water == b.water &&
           a.shader == b.shader &&
           a.bloom == b.bloom && a.ssao == b.ssao && (!a.ssao || a.ssao_quality == b.ssao_quality) && a.tonemap == b.tonemap &&
           a.crt == b.crt && a.sharpen == b.sharpen &&
           a.gamma == b.gamma && a.saturation == b.saturation && a.color == b.color && a.brightness == b.brightness &&
           a.contrast == b.contrast && a.shader_params == b.shader_params && a.exposure == b.exposure &&
           a.highlights == b.highlights && a.tone_shadows == b.tone_shadows && a.bloom_threshold == b.bloom_threshold &&
           a.bloom_radius == b.bloom_radius && a.bloom_strength == b.bloom_strength && a.crt_scanlines == b.crt_scanlines &&
           a.crt_mask == b.crt_mask && a.crt_vignette == b.crt_vignette;
}

} // namespace

GraphicsSettings preset(Preset p) {
    GraphicsSettings s;  // Original Dreamcast: o padrao da estrutura
    switch (p) {
    case Preset::Balanced:
        s.resolution = Resolution::P1080, s.filter = Filter::Bilinear, s.msaa = 2, s.hd_textures = true;
        s.color = true, s.contrast = 1.05f, s.saturation = 1.10f;  // aprimorado: um pouco mais de contraste e cor
        break;
    case Preset::High:
        s.resolution = Resolution::P1440, s.filter = Filter::Anisotropic, s.anisotropy = 16, s.msaa = 4;
        // N9.6 (mantenedor, 2026-11-11): as sombras originais tambem no Alto; as aprimoradas (borda suave e sombras de contato),
        // a luz das fontes e o SSAO ficam so na escolha manual (na GPU integrada em 1080, as aprimoradas dobram o custo)
        s.hd_textures = true, s.sharpen = 0.25f;
        // N9.7 (D7-1, mantenedor 2026-11-11): a agua original em todos os presets; a aprimorada so pela escolha manual
        s.color = true, s.contrast = 1.10f, s.saturation = 1.15f;
        break;
    default: break;
    }
    return s;
}

GraphicsSettings remastered_best(const GraphicsSettings& base) {
    GraphicsSettings s = preset(Preset::High);
    s.aspect = Aspect::Wide16x9, s.shadows = Shadows::Enhanced, s.lighting = true;
    s.ssao = true, s.ssao_quality = SsaoQuality::High, s.water = Water::High;
    s.window = base.window, s.window_w = base.window_w, s.window_h = base.window_h, s.vsync = base.vsync;
    s.vulkan = base.vulkan, s.shader = base.shader, s.shader_params = base.shader_params;
    s.remaster_defaults = kRemasterDefaults;
    return s;
}

Preset classify(const GraphicsSettings& s) {
    for (Preset p : {Preset::Original, Preset::Balanced, Preset::High})
        if (same_quality(s, preset(p))) return p;
    return Preset::Custom;
}

uint32_t internal_height(const GraphicsSettings& s) {
    static constexpr uint32_t h[] = {480, 720, 1080, 1440, 2160};
    return std::max<uint32_t>(240, h[size_t(s.resolution)] * s.scale_pct / 100);
}

GraphicsSettings clamp_to(const GraphicsSettings& in, const Capabilities& caps, std::vector<std::string>* notes) {
    auto note = [&](const std::string& m) {
        if (notes) notes->push_back(m);
    };
    GraphicsSettings s = in;
    {  // nao finitos (std::clamp nao os corrige: NaN compara falso) voltam ao padrao (revisao Codex 2026-10-22)
        const GraphicsSettings d;
        auto fix = [](float& v, float def) {
            if (!std::isfinite(v)) v = def;
        };
        fix(s.sharpen, d.sharpen), fix(s.gamma, d.gamma), fix(s.saturation, d.saturation), fix(s.brightness, d.brightness);
        fix(s.contrast, d.contrast), fix(s.exposure, d.exposure), fix(s.highlights, d.highlights);
        fix(s.tone_shadows, d.tone_shadows), fix(s.bloom_threshold, d.bloom_threshold), fix(s.bloom_radius, d.bloom_radius);
        fix(s.bloom_strength, d.bloom_strength), fix(s.crt_scanlines, d.crt_scanlines), fix(s.crt_mask, d.crt_mask);
        fix(s.crt_vignette, d.crt_vignette);
        for (auto it = s.shader_params.begin(); it != s.shader_params.end();)
            it = std::isfinite(it->second) ? std::next(it) : s.shader_params.erase(it);
    }
    s.scale_pct        = std::clamp<uint32_t>(s.scale_pct, 50, 200);
    s.anisotropy       = std::clamp<uint32_t>(s.anisotropy, 2, 16);
    s.sharpen          = std::clamp(s.sharpen, 0.f, 1.f);
    s.gamma            = std::clamp(s.gamma, 0.5f, 2.5f);
    s.saturation       = std::clamp(s.saturation, 0.f, 2.f);
    s.brightness       = std::clamp(s.brightness, -0.5f, 0.5f);
    s.contrast         = std::clamp(s.contrast, 0.5f, 1.5f);
    s.exposure         = std::clamp(s.exposure, -2.f, 2.f);
    s.highlights       = std::clamp(s.highlights, 0.f, 1.f);
    s.tone_shadows     = std::clamp(s.tone_shadows, -0.5f, 0.5f);
    s.bloom_threshold  = std::clamp(s.bloom_threshold, 0.f, 1.f);
    s.bloom_radius     = std::clamp(s.bloom_radius, 0.005f, 0.05f);
    s.bloom_strength   = std::clamp(s.bloom_strength, 0.f, 2.f);
    s.crt_scanlines    = std::clamp(s.crt_scanlines, 0.f, 1.f);
    s.crt_mask         = std::clamp(s.crt_mask, 0.f, 1.f);
    s.crt_vignette     = std::clamp(s.crt_vignette, 0.f, 1.f);
    if (s.msaa != 1 && s.msaa != 2 && s.msaa != 4 && s.msaa != 8) s.msaa = 1;
    s.window_w = s.window_w ? std::clamp<uint32_t>(s.window_w, 320, 16384) : 0;
    s.window_h = s.window_h ? std::clamp<uint32_t>(s.window_h, 240, 16384) : 0;
    if (!caps.vulkan) {  // o software so faz o original
        const GraphicsSettings o = preset(Preset::Original);
        if (!same_quality(s, o)) note("sem Vulkan: so a imagem original (renderizador em software)");
        GraphicsSettings r = o;
        r.vulkan = false;  // recuo seguro: sem dispositivo, o software
        r.window = s.window, r.vsync = s.vsync, r.aspect = Aspect::Original4x3;
        r.window_w = s.window_w, r.window_h = s.window_h;  // o tamanho da janela nao depende do renderizador
        return r;
    }
    // o que ainda nao existe no renderizador volta ao original (o menu o mostra indisponivel)
    const Features& f = caps.features;
    const GraphicsSettings o = preset(Preset::Original);
    auto off = [&](bool have, bool changed, auto reset, const char* what) {
        if (!have && changed) reset(), note(std::string(what) + ": ainda nao implementado");
    };
    off(f.scaling, s.resolution != o.resolution || s.scale_pct != o.scale_pct,
        [&] { s.resolution = o.resolution, s.scale_pct = o.scale_pct; }, "resolucao interna");
    off(f.msaa, s.msaa != 1, [&] { s.msaa = 1; }, "antialiasing");
    off(f.filtering, s.filter != o.filter, [&] { s.filter = o.filter; }, "filtragem");
    off(f.window_modes, s.window != WindowMode::Windowed, [&] { s.window = WindowMode::Windowed; }, "modo de tela");
    off(f.vsync, !s.vsync, [&] { s.vsync = true; }, "V-Sync");
    // N9.5 fase 5 (D5-1): o automatico saiu; vale 4:3
    if (s.aspect == Aspect::Auto) s.aspect = Aspect::Original4x3, note("proporcao automatica removida (N9.5 fase 5): 4:3");
    off(f.aspect_wide || caps.wide_frame, s.aspect != Aspect::Original4x3, [&] { s.aspect = Aspect::Original4x3; }, "16:9");
    off(f.hd_textures, s.hd_textures, [&] { s.hd_textures = false; }, "texturas HD");
    off(f.lighting, s.lighting, [&] { s.lighting = false; }, "iluminacao");
    off(f.shadows, s.shadows != Shadows::Original, [&] { s.shadows = Shadows::Original; }, "sombras");
    off(f.water, s.water != Water::Original, [&] { s.water = Water::Original; }, "agua aprimorada");
    off(f.shaders, !s.shader.empty(), [&] { s.shader.clear(); }, "shaders");
    off(f.color, s.color || s.brightness != 0 || s.contrast != 1.0f || s.gamma != 1.0f || s.saturation != 1.0f,
        [&] { s.color = false, s.brightness = 0, s.contrast = 1.0f, s.gamma = 1.0f, s.saturation = 1.0f; }, "correcao de cor");
    off(f.tonemap, s.tonemap || s.exposure != 0 || s.highlights != 0.5f || s.tone_shadows != 0,
        [&] { s.tonemap = false, s.exposure = 0, s.highlights = 0.5f, s.tone_shadows = 0; }, "tonemapping");
    off(f.sharpen, s.sharpen != 0, [&] { s.sharpen = 0; }, "nitidez");
    off(f.bloom, s.bloom || s.bloom_threshold != 0.7f || s.bloom_radius != 0.02f || s.bloom_strength != 0.6f,
        [&] { s.bloom = false, s.bloom_threshold = 0.7f, s.bloom_radius = 0.02f, s.bloom_strength = 0.6f; }, "bloom");
    off(f.crt, s.crt, [&] { s.crt = false; }, "filtro CRT");
    off(f.ssao, s.ssao, [&] { s.ssao = false; }, "SSAO");  // N9.6 fase 1: recurso proprio (era o `post` generico)
    if (!f.shaders) s.shader_params.clear();
    while (internal_height(s) > caps.max_height && (s.resolution != Resolution::Original || s.scale_pct > 100)) {
        if (s.scale_pct > 100) s.scale_pct = 100;
        else s.resolution = Resolution(uint8_t(s.resolution) - 1);
        note("resolucao interna reduzida para o limite da GPU");
    }
    if (s.crt && internal_height(s) < kCrtMinHeight) {
        s.crt = false;
        note("filtro CRT desligado: precisa de resolucao interna de 1080p ou mais");
    }
    while (s.msaa > caps.max_msaa) s.msaa /= 2, note("MSAA reduzido para o suportado");
    if (s.filter == Filter::Anisotropic && caps.max_anisotropy < 2) s.filter = Filter::Bilinear, note("sem filtragem anisotropica");
    if (s.filter == Filter::Anisotropic) s.anisotropy = std::min(s.anisotropy, caps.max_anisotropy);
    if (s.aspect == Aspect::Wide16x9 && !caps.wide_hud && !caps.wide_frame) s.aspect = Aspect::Original4x3, note("16:9 ainda indisponivel");
    return s;
}

bool requires_confirmation(const GraphicsSettings& a, const GraphicsSettings& b) {
    return a.resolution != b.resolution || a.scale_pct != b.scale_pct || a.window != b.window || a.window_w != b.window_w ||
           a.window_h != b.window_h;
}

AspectChoice aspect_choice(const GraphicsSettings& s, const Capabilities& caps, bool edition_wide) {
    if (!edition_wide) return AspectChoice::Fixed;
    if (!caps.wide_frame) return AspectChoice::Unavailable;
    if (!s.vulkan) return AspectChoice::NeedsVulkan;
    return AspectChoice::Free;
}

Aspect effective_aspect(const GraphicsSettings& s, const Capabilities& caps, bool edition_wide) {
    return aspect_choice(s, caps, edition_wide) == AspectChoice::Free && s.aspect == Aspect::Wide16x9 ? Aspect::Wide16x9
                                                                                                      : Aspect::Original4x3;
}

uint32_t max_internal_height(Aspect effective, uint32_t gpu_max_4x3, uint32_t cap) {
    uint32_t h = std::min(cap, gpu_max_4x3);
    if (effective == Aspect::Wide16x9) h = std::min(h, uint32_t(double(gpu_max_4x3) * (480.0 / 854.0) / 0.75));
    return std::max(480u, h);
}

uint32_t effective_internal_height(const GraphicsSettings& s, uint32_t max_height) {
    GraphicsSettings t = s;
    while (internal_height(t) > max_height && (t.resolution != Resolution::Original || t.scale_pct > 100)) {
        if (t.scale_pct > 100) t.scale_pct = 100;
        else t.resolution = Resolution(uint8_t(t.resolution) - 1);
    }
    return internal_height(t);
}

uint32_t effective_internal_height(uint32_t wanted, uint32_t max_height) {
    if (wanted <= max_height) return wanted;
    for (uint32_t h : {2160u, 1440u, 1080u, 720u})  // os degraus do menu
        if (h <= max_height) return h;
    return 480;
}

Aspect aspect_for_file(Aspect selected, Aspect effective, Aspect on_file, AspectChoice choice, bool from_command_line) {
    if (choice != AspectChoice::Free || from_command_line || selected != effective) return on_file;
    return selected;
}

Apply apply_kind(const GraphicsSettings& a, const GraphicsSettings& b) {
    if (a == b) return Apply::None;
    if (requires_confirmation(a, b) || a.aspect != b.aspect || a.msaa != b.msaa || a.vsync != b.vsync) return Apply::Recreate;
    return Apply::Immediate;
}

const char* preset_name(Preset p) {
    static constexpr const char* n[] = {"original", "equilibrado", "alta", "personalizado"};
    return n[size_t(p)];
}

std::string to_toml(const GraphicsSettings& s) {
    std::ostringstream o;
    o << kGraphicsHeader << " (gravadas pelo menu do jogo; editar a mao tambem vale)\n"
      << "preset = \"" << preset_name(classify(s)) << "\"\n\n[imagem]\n"
      << "resolucao = \"" << name(s.resolution) << "\"\n"
      << "escala = " << s.scale_pct << "\n"
      << "filtragem = \"" << name(s.filter) << "\"\n"
      << "anisotropia = " << s.anisotropy << "\n"
      << "msaa = " << s.msaa << "\n"
      << "texturas_hd = " << (s.hd_textures ? "true" : "false") << "\n"
      << "iluminacao = " << (s.lighting ? "true" : "false") << "\n"
      << "sombras = \"" << name(s.shadows) << "\"\n"
      << "agua = \"" << name(s.water) << "\"\n"
      << "shader = \"" << s.shader << "\"\n\n[efeitos]\n"
      << "bloom = " << (s.bloom ? "true" : "false") << "\n"
      << "ssao = " << (s.ssao ? "true" : "false") << "\n"
      << "ssao_qualidade = \"" << name(s.ssao_quality) << "\"\n"
      << "tonemap = " << (s.tonemap ? "true" : "false") << "\n"
      << "crt = " << (s.crt ? "true" : "false") << "\n"
      << "nitidez = " << s.sharpen << "\n"
      << "gamma = " << s.gamma << "\n"
      << "saturacao = " << s.saturation << "\n"
      << "cor = " << (s.color ? "true" : "false") << "\n"
      << "brilho = " << s.brightness << "\n"
      << "contraste = " << s.contrast << "\n"
      << "exposicao = " << s.exposure << "\n"
      << "altas_luzes = " << s.highlights << "\n"
      << "sombras_tom = " << s.tone_shadows << "\n"
      << "bloom_limiar = " << s.bloom_threshold << "\n"
      << "bloom_raio = " << s.bloom_radius << "\n"
      << "bloom_intensidade = " << s.bloom_strength << "\n"
      << "crt_linhas = " << s.crt_scanlines << "\n"
      << "crt_mascara = " << s.crt_mask << "\n"
      << "crt_vinheta = " << s.crt_vignette << "\n\n[tela]\n"
      << "modo = \"" << name(s.window) << "\"\n"
      << "largura = " << s.window_w << "\n"
      << "altura = " << s.window_h << "\n"
      << "proporcao = \"" << name(s.aspect) << "\"\n"
      << "vsync = " << (s.vsync ? "true" : "false") << "\n"
      << "renderizador = \"" << (s.vulkan ? "vulkan" : "software") << "\"\n";
    if (s.remaster_defaults) o << "\n[padroes]\nremastered = " << s.remaster_defaults << "\n";  // v0.9.7-1
    if (!s.shader_params.empty()) {
        o << "\n[shader_parametros]\n";
        for (const auto& [k, v] : s.shader_params) o << "\"" << k << "\" = " << v << "\n";
    }
    return o.str();
}

GraphicsSettings from_toml(const std::string& text, std::string* warning) {
    GraphicsSettings s;
    toml::table      doc;
    try {
        doc = toml::parse(text);
    } catch (const toml::parse_error& e) {
        if (warning) *warning = std::string("graficos.toml ilegivel (") + std::string(e.description()) + "): padrao";
        return s;
    }
    std::string bad;
    auto        str = [&](const char* sec, const char* key, auto& field) {
        if (auto v = doc[sec][key].value<std::string>()) {
            if (!parse(*v, field)) bad += std::string(bad.empty() ? "" : ", ") + sec + "." + key;
        }
    };
    auto num = [&](const char* sec, const char* key, uint32_t& field) {
        if (auto v = doc[sec][key].value<int64_t>()) field = uint32_t(std::max<int64_t>(0, *v));
    };
    auto flt = [&](const char* sec, const char* key, float& field) {  // nan/inf (validos no TOML) ficam com o padrao
        if (auto v = doc[sec][key].value<double>(); v && std::isfinite(*v)) field = float(*v);
    };
    auto bln = [&](const char* sec, const char* key, bool& field) {
        if (auto v = doc[sec][key].value<bool>()) field = *v;
    };
    // um preset nomeado vale como base; as chaves explicitas valem sobre ele
    if (auto p = doc["preset"].value<std::string>()) {
        for (Preset q : {Preset::Original, Preset::Balanced, Preset::High})
            if (*p == preset_name(q)) s = preset(q);
    }
    str("imagem", "resolucao", s.resolution), num("imagem", "escala", s.scale_pct), str("imagem", "filtragem", s.filter);
    num("imagem", "anisotropia", s.anisotropy), num("imagem", "msaa", s.msaa), bln("imagem", "texturas_hd", s.hd_textures);
    bln("imagem", "iluminacao", s.lighting), str("imagem", "sombras", s.shadows), str("imagem", "agua", s.water);
    if (auto v = doc["imagem"]["shader"].value<std::string>()) s.shader = *v;
    bln("efeitos", "bloom", s.bloom), bln("efeitos", "ssao", s.ssao), bln("efeitos", "tonemap", s.tonemap);
    str("efeitos", "ssao_qualidade", s.ssao_quality);
    bln("efeitos", "crt", s.crt), flt("efeitos", "nitidez", s.sharpen), flt("efeitos", "gamma", s.gamma);
    flt("efeitos", "saturacao", s.saturation), bln("efeitos", "cor", s.color), flt("efeitos", "brilho", s.brightness);
    flt("efeitos", "contraste", s.contrast), flt("efeitos", "exposicao", s.exposure);
    flt("efeitos", "altas_luzes", s.highlights), flt("efeitos", "sombras_tom", s.tone_shadows);
    flt("efeitos", "bloom_limiar", s.bloom_threshold), flt("efeitos", "bloom_raio", s.bloom_radius);
    flt("efeitos", "bloom_intensidade", s.bloom_strength), flt("efeitos", "crt_linhas", s.crt_scanlines);
    flt("efeitos", "crt_mascara", s.crt_mask), flt("efeitos", "crt_vinheta", s.crt_vignette);
    if (const auto* t = doc["shader_parametros"].as_table())
        for (const auto& [k, v] : *t)
            if (auto d = v.value<double>(); d && std::isfinite(*d)) s.shader_params[std::string(k.str())] = float(*d);
    str("tela", "modo", s.window), bln("tela", "vsync", s.vsync);
    num("padroes", "remastered", s.remaster_defaults);  // v0.9.7-1
    // N9.5 fase 5 (D5-1): "auto" (o nome gravado ate a fase 4) e "automatico" valem 4:3, com aviso; a proxima gravacao
    // escreve "4:3"
    std::string oldAspect;
    if (auto v = doc["tela"]["proporcao"].value<std::string>(); v && (*v == "automatico" || *v == "auto")) oldAspect = *v;
    else str("tela", "proporcao", s.aspect);
    num("tela", "largura", s.window_w), num("tela", "altura", s.window_h);
    if (auto v = doc["tela"]["renderizador"].value<std::string>()) {
        if (*v == "vulkan") s.vulkan = true;
        else if (*v == "software") s.vulkan = false;
        else bad += std::string(bad.empty() ? "" : ", ") + "tela.renderizador";
    }
    if (!bad.empty() && warning) *warning = "graficos.toml: valor desconhecido em " + bad + " (padrao nesses campos)";
    if (!oldAspect.empty()) {
        s.aspect = Aspect::Original4x3;
        if (warning)
            *warning += std::string(warning->empty() ? "" : "; ") + "graficos.toml: tela.proporcao = \"" + oldAspect +
                        "\" vale 4:3 (proporcao automatica removida temporariamente, N9.5 fase 5)";
    }
    // faixas: o que estiver fora volta para a faixa (a GPU decide o resto em clamp_to)
    Capabilities all;
    all.vulkan = true, all.max_msaa = 8, all.max_anisotropy = 16, all.max_height = 1u << 30, all.wide_hud = true;
    all.features = Features::all();
    return clamp_to(s, all);
}

GraphicsSettings read_graphics(const fs::path& dir, std::string* warning) {
    std::ifstream in(dir / kGraphicsFile, std::ios::binary);
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return from_toml(ss.str(), warning);
}

bool write_graphics(const fs::path& dir, const GraphicsSettings& s, std::string* err, std::error_code* code) {
    const fs::path p = dir / kGraphicsFile, tmp = dir / (std::wstring(kGraphicsFile) + L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << to_toml(s);
        const int e = errno;
        out.close();
        if (!out) {
            if (code) *code = std::error_code(e ? e : EIO, std::generic_category());
            if (err) *err = "nao foi possivel gravar " + tmp.string();
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        if (code) *code = ec;
        std::error_code ignore;
        fs::remove(tmp, ignore);
        if (err) *err = "nao foi possivel gravar " + p.string() + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

} // namespace bs::native::settings
