#pragma once
// N9-L (L4): identificacao de um SFD (Sofdec) sem decodificar a imagem: cabecalho da CRI, packs e pacotes por stream, parametros
// do MPEG-1 (cabecalho de sequencia, GOPs, tipos de quadro), PTS e o cabecalho do ADX. Serve a prova de que os arquivos do disco
// sao o formato que o decodificador do Linux espera (docs/N9-L-PLANO.md, L4) e a mensagem clara quando um arquivo nao e.
#include "video/sfd_media.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace bs::native::video {

struct SfdInfo {
    bool     sofdecHeader = false;  // "Sofdec Stream" no inicio do arquivo
    size_t   headerBytes  = 0;      // bytes antes do primeiro pack
    uint32_t packs = 0, systemHeaders = 0, paddingPes = 0, otherPes = 0;
    std::array<uint32_t, 256> pesById{};  // pacotes por id de stream
    bool     endCode = false;             // 000001B9
    // video MPEG-1 (0xE0)
    uint32_t width = 0, height = 0, aspectCode = 0, frameRateCode = 0, bitRate = 0, vbvBufferSize = 0;
    double   fps = 0;
    bool     mpeg2 = false;  // extensao de sequencia (000001B5): MPEG-2, nao esperado
    uint32_t sequenceHeaders = 0, gops = 0, closedGops = 0, pictures = 0, iPictures = 0, pPictures = 0, bPictures = 0, dPictures = 0;
    int64_t  firstVideoPts = -1, lastVideoPts = -1, firstAudioPts = -1, lastAudioPts = -1;  // 90 kHz
    // audio ADX (0xC0)
    bool     hasAudio = false;
    AdxInfo  adx;
    size_t   videoBytes = 0, audioBytes = 0;
};

// Falha com `err` se o arquivo nao e um MPEG-1 Program Stream com video MPEG-1, ou se o audio presente nao e ADX suportado.
bool sfd_identify(std::span<const uint8_t> file, SfdInfo& info, std::string* err = nullptr);

// Quadros por segundo pelo codigo do cabecalho de sequencia (tabela do MPEG-1); 0 se reservado.
double mpeg1_frame_rate(uint32_t code);

} // namespace bs::native::video
