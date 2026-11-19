#pragma once
// Abre uma imagem de disco pela extensao: .cdi (DiscJuggler), .gdi (TOSEC) e .cue (Redump). Um lugar so para o
// extrator, o Builder e as ferramentas (docs/IMAGENS-DISCO-PLANO.md). So ferramentas: o jogo le os dados preparados.
#include "disc/disc_image.h"

#include <filesystem>
#include <memory>

namespace bs::disc {

// `confine` (nao vazio, imagem extraida de um pacote): as faixas citadas por GDI/CUE precisam ficar dentro dessa pasta
std::unique_ptr<DiscImage> disc_open(const std::filesystem::path& path, const std::filesystem::path& confine = {});

// "CDI, GDI ou CUE": para mensagens
const char* disc_formats_text();

} // namespace bs::disc
