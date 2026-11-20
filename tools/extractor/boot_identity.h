#pragma once
// Identificacao da versao pelo executavel de boot, num lugar so (extrator, dados de execucao, boot do jogo).
// docs/IMAGENS-DISCO-PLANO.md (I1, I3, I7):
//  - hash exato: kKnownVersions (known_versions.h) e kExtraVersions (aqui: o disco original GD-ROM);
//  - variante: o executavel so difere do original nos 3 literais de FAD que as conversoes para CDI reescrevem
//    (decisao D2 do mantenedor, 2026-11-18). O resto do binario e exigido byte a byte (hash com a mascara).
#include "known_versions.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bs {

// Literais de FAD do Blue Stinger NTSC-U V1.000 (offsets no 1ST_READ.BIN, carregado em 8C010000)
struct FadLiterals {
    uint32_t lowerBound = 0;  // 8C15AEE4 e 8C15B1C8: menor FAD aceito nas leituras (GD 45150; CDI 150)
    uint32_t pvdFad     = 0;  // 8C1DAC38: FAD do descritor ISO9660 (faixa de boot + 16 + 150)
};

struct BootIdentity {
    const KnownVersion* version = nullptr;  // versao reconhecida (nullptr: recusada)
    std::vector<uint8_t> data;              // executavel como o jogo o carrega (desembaralhado se preciso)
    std::string          sha256;            // de `data`
    bool                 scrambled = false;
    bool                 variant   = false;  // aceito pela mascara, nao pelo hash exato
    FadLiterals          fad;                // os literais lidos (so em versao da familia mascarada)
};

// `bootTrackLba`: LBA da faixa de boot da imagem (IP.BIN). Numa variante, os literais precisam bater com ela:
// pvdFad == bootTrackLba + 16 + 150 e lowerBound <= bootTrackLba + 150. Use UINT32_MAX para nao conferir (quando a
// faixa nao e conhecida; o boot do jogo confere de novo com os dados preparados).
BootIdentity identify_boot(const std::vector<uint8_t>& raw, uint32_t bootTrackLba);

// Mensagem de recusa com o tamanho e o SHA-256 encontrados (issue #1: o jogador pode informar a linha)
std::string boot_rejection_text(const std::string& product, const std::string& version, const BootIdentity& id);

// Versao do jogo de um id reconhecido, na numeracao de BS_VERSAO_JOGO (runtime/versao/include/versao/enderecos.h):
// 0 = americano (CDI, GD, variantes), 1 = PAL alemao, 2 = PAL ingles, 3 = japones v1.006, 4 e 5 = Director's Cut EN
// e FR, -1 = outro. O executavel so roda os dados da sua versao.
int game_version_of(const std::string& versionId);

// Configuracao do bs-recomp (config/<nome>) de uma versao que o Builder ja instala; nullptr = versao ainda sem suporte na
// instalacao (reconhecida, mas recusada com motivo). PAL P8: o americano e o PAL alemao.
const char* game_version_config(int gameVersion);

// Versoes extras (hash exato), alem das de known_versions.h
extern const KnownVersion kExtraVersions[];
extern const size_t       kExtraVersionCount;

} // namespace bs
