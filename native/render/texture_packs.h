#pragma once
// N9.8 fase 8.2 (docs/N9-8-AUDITORIA.md secao 9): o provedor das texturas HD. Os pacotes sao os de mods\ com mod.toml (o
// mesmo contrato das legendas e das dublagens, localization/mod_packages.h: id, nome, versao, prioridade, jogo) e uma pasta
// texturas\ com um PNG por textura, de nome "<largura>x<altura>-<16 hex>.png" (o TextureId da textura ORIGINAL, o texto de
// TextureId::text). Nenhuma arte do jogo vem com o projeto: os pacotes sao do jogador.
//
// Regras:
//   - o pacote de maior prioridade que tem a textura vence (empate: a ordem do id, a mesma do ModPackages);
//   - um nome que nao e um TextureId canonico e ignorado (contado e listado no aviso), como outros arquivos da pasta;
//   - a decodificacao e no primeiro uso (D8-6), ate `Limits::per_frame` texturas ou `Limits::bytes_per_frame` por render; o
//     resto fica para os renders seguintes (deferred), com o original enquanto isso; o tamanho decodificado e conhecido
//     antes de decodificar e a soma do render nao passa do limite (a primeira do render sempre passa);
//   - o orcamento da sessao, `Limits::max_resident` de texels guardados: a que passaria dele e recusada (recuo), e a falta
//     de memoria ao decodificar tambem so recusa aquela textura;
//   - recuo ao original, por textura, com o motivo guardado: o PNG maior que `Limits::max_file`, ilegivel, com dimensoes que
//     nao sao um fator inteiro de 1 a 16 da original na mesma proporcao (conferidas antes de decodificar) ou alem de
//     `Limits::max_side`. Uma textura recusada nao e tentada de novo nesta execucao;
//   - o canal alfa do PNG vale (sem alfa: opaco). O material (agua, emissivos, AO) continua o da textura original.
#include "render/texture_replacement.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bs::native::render {

class TexturePacks : public ITextureReplacements {
public:
    struct Limits {
        uint32_t per_frame       = 8;                  // decodificacoes por render
        uint64_t bytes_per_frame = 64ull << 20;        // texels decodificados (4 B) por render, alem da primeira
        uint64_t max_file        = 64ull << 20;        // tamanho do PNG
        uint32_t max_side        = 8192;               // lado da substituicao
        uint64_t max_resident    = 1024ull << 20;      // texels decodificados guardados na sessao (alem: recusa)
    };
    struct Package {
        std::string           id, name, version;
        int                   priority = 0;
        std::filesystem::path dir;
        uint32_t              textures = 0;  // os PNG validos dele que entraram no indice (os de maior prioridade vencem)
    };
    struct Stats {
        uint64_t indexed = 0, overridden = 0, bad_names = 0;  // no indice; cobertos por outro pacote; nomes ignorados
        uint64_t loaded = 0, rejected = 0, deferred = 0;      // decodificadas; recusadas (recuo); adiamentos pelo limite
        uint64_t bytes = 0;                                   // texels residentes (4 B cada) das carregadas
        uint32_t max_factor = 0;                              // o maior fator carregado
    };

    // Le mods\*\mod.toml (localization::ModPackages) e as pastas texturas\ dos pacotes validos. `problems` recebe os
    // pacotes recusados e os avisos (nomes ignorados), uma linha cada.
    static TexturePacks discover(const std::filesystem::path& modsDir, std::string_view gameVersion,
                                 std::vector<std::string>* problems = nullptr);
    // Os pacotes de uma lista ja pronta (os testes), em ordem de prioridade (o primeiro vence)
    static TexturePacks from_dirs(const std::vector<Package>& packages, std::vector<std::string>* problems = nullptr);

    const ReplacementTexture* find(const TextureId& id) override;
    void                      begin_frame() override { frameCount_ = 0, frameBytes_ = 0; }
    bool                      deferred(const TextureId& id) const override;

    bool                        empty() const { return index_.empty(); }
    const std::vector<Package>& packages() const { return packages_; }
    const Stats&                stats() const { return stats_; }
    Limits&                     limits() { return limits_; }
    // as recusas desta execucao ("<id>: <motivo> (<pacote>)"), na ordem
    const std::vector<std::string>& rejections() const { return rejections_; }

private:
    struct Entry {
        std::filesystem::path file;
        uint32_t              package = 0;
        enum class State : uint8_t { Indexed, Loaded, Rejected, Deferred } state = State::Indexed;
        ReplacementTexture tex;
    };
    struct Hash {
        size_t operator()(const TextureId& id) const { return size_t(id.content ^ (uint64_t(id.width) << 40) ^ id.height); }
    };
    void reject(const TextureId& id, Entry& e, const std::string& why);

    std::vector<Package>                       packages_;
    std::unordered_map<TextureId, Entry, Hash> index_;
    std::vector<std::string>                   rejections_;
    Limits                                     limits_;
    Stats                                      stats_;
    uint32_t                                   frameCount_ = 0;
    uint64_t                                   frameBytes_ = 0;
};

} // namespace bs::native::render
