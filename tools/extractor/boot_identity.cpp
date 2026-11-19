#include "boot_identity.h"

#include "disc/scramble.h"
#include "util/hash.h"

#include <cstring>

namespace bs {

// O disco original (GD-ROM, igual ao Redump "Blue Stinger (USA)"): o mesmo codigo do CDI, so os literais de FAD do
// layout do GD. O C++ gerado e identico byte a byte (prova em docs/IMAGENS-DISCO-PLANO.md, secao 1).
const KnownVersion kExtraVersions[] = {
    {"bluestinger_us_v1000_gd", "T13001N", "V1.000", 1975556,
     "491fd69cd64100786372169e95e50ca2733bbfb88a54ef15da6a1f66297f5593"},
};
const size_t kExtraVersionCount = sizeof(kExtraVersions) / sizeof(kExtraVersions[0]);

namespace {

// Familia mascarada: o executavel com os 3 literais zerados. O CDI do projeto e o GD original dao o mesmo hash.
constexpr uint32_t   kMaskedSize       = 1975556;
constexpr uint32_t   kLowerBound1      = 0x14AEE4;  // 8C15AEE4
constexpr uint32_t   kLowerBound2      = 0x14B1C8;  // 8C15B1C8
constexpr uint32_t   kPvdFad           = 0x1CAC38;  // 8C1DAC38
constexpr const char kMaskedSha256[]   = "09411e3e0a6f8d2792b34bdbaccc85d9c4f3657b8d7e6bf03e9b93f5943335ee";
constexpr uint32_t   kFadOffset        = 150;
constexpr KnownVersion kVariant = {"bluestinger_us_v1000_variante", "T13001N", "V1.000", kMaskedSize, kMaskedSha256};

uint32_t le32(const std::vector<uint8_t>& d, uint32_t o) {
    return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}

const KnownVersion* exact(const std::string& h) {
    for (const auto& v : kKnownVersions)
        if (h == v.boot_sha256) return &v;
    for (size_t i = 0; i < kExtraVersionCount; ++i)
        if (h == kExtraVersions[i].boot_sha256) return &kExtraVersions[i];
    return nullptr;
}

bool masked(const std::vector<uint8_t>& d) {
    if (d.size() != kMaskedSize) return false;
    std::vector<uint8_t> m = d;
    for (uint32_t o : {kLowerBound1, kLowerBound2, kPvdFad}) std::memset(&m[o], 0, 4);
    return util::sha256Hex(m.data(), m.size()) == kMaskedSha256;
}

bool literalsFit(const FadLiterals& f, uint32_t l1, uint32_t l2, uint32_t bootTrackLba) {
    if (l1 != l2 || l1 < kFadOffset) return false;
    if (bootTrackLba == UINT32_MAX) return true;
    return f.pvdFad == bootTrackLba + 16 + kFadOffset && l1 <= bootTrackLba + kFadOffset;
}

} // namespace

BootIdentity identify_boot(const std::vector<uint8_t>& raw, uint32_t bootTrackLba) {
    BootIdentity r;
    // 1. hash exato (como no disco, depois desembaralhado)
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<uint8_t> d = pass ? disc::descramble(raw) : raw;
        const std::string    h = util::sha256Hex(d.data(), d.size());
        if (const KnownVersion* v = exact(h)) {
            r.sha256 = h, r.scrambled = pass == 1;
            // da familia mascarada (CDI do projeto, GD original): os literais tambem precisam bater com a faixa de boot
            // desta imagem (ex.: o executavel do GD num layout de CDI nao acharia os dados; revisao Codex 2026-11-19)
            if (masked(d)) {
                FadLiterals    f;
                const uint32_t l1 = le32(d, kLowerBound1), l2 = le32(d, kLowerBound2);
                f.lowerBound = l1, f.pvdFad = le32(d, kPvdFad);
                r.fad        = f;
                if (!literalsFit(f, l1, l2, bootTrackLba)) {
                    r.data = std::move(d);
                    return r;
                }
            }
            r.version = v, r.data = std::move(d);
            return r;
        }
    }
    // 2. variante: so os 3 literais de FAD diferentes, e coerentes com a faixa de boot da imagem
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<uint8_t> d = pass ? disc::descramble(raw) : raw;
        if (!masked(d)) continue;
        FadLiterals    f;
        const uint32_t l1 = le32(d, kLowerBound1), l2 = le32(d, kLowerBound2);
        f.lowerBound = l1, f.pvdFad = le32(d, kPvdFad);
        r.sha256 = util::sha256Hex(d.data(), d.size()), r.scrambled = pass == 1, r.fad = f;
        if (!literalsFit(f, l1, l2, bootTrackLba)) {  // reconhecida, mas incoerente: recusa com o hash
            r.data = std::move(d);
            return r;
        }
        r.version = &kVariant, r.variant = true, r.data = std::move(d);
        return r;
    }
    r.data   = raw;
    r.sha256 = util::sha256Hex(raw.data(), raw.size());
    return r;
}

std::string boot_rejection_text(const std::string& product, const std::string& version, const BootIdentity& id) {
    std::string s = "versao nao suportada: " + product + " " + version + ". O executavel de boot (" + std::to_string(id.data.size()) +
                    " bytes, SHA-256 " + id.sha256 + ") nao e o da versao recompilada (Blue Stinger NTSC-U T13001N V1.000)";
    if (!id.fad.pvdFad) return s + ". Imagens aceitas: o disco original (GDI, CUE/BIN ou CHD, como o Redump; tambem em .zip ou .7z) e CDIs que so mudam a "
                                   "posicao dos dados. Ao relatar, informe esta linha (so o hash, nunca a imagem).";
    return s + ": o codigo e o esperado, mas as posicoes do disco gravadas nele (FAD " + std::to_string(id.fad.lowerBound) + " e " +
           std::to_string(id.fad.pvdFad) + ") nao batem com esta imagem. Ao relatar, informe esta linha.";
}

} // namespace bs
