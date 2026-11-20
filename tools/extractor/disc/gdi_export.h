#pragma once
// Director's Cut (docs/DIRECTORS-CUT-ANALISE.md): o patcher do Director's Cut (GPL, baixado do repositorio do autor e
// rodado fora do nosso codigo) so le GDI (trilhas brutas de 2 352 bytes e a folha .gdi). Aqui qualquer imagem que o
// leitor abre com os setores brutos (GDI, CUE/BIN, CHD) vira um GDI: uma trilha por faixa, a partir do inicio dela
// (depois do pregap), e a folha com o LBA de cada uma. So nas ferramentas.
#include "disc/disc_image.h"

#include <atomic>
#include <filesystem>
#include <functional>

namespace bs::disc {

// Grava <dir>/<nome>.gdi e as trilhas trackNN.bin (dados) / trackNN.raw (audio). Devolve o caminho da folha. Lanca
// DiscError (ou "cancelado"). `progress` recebe a fracao (0..1).
std::filesystem::path export_gdi(const DiscImage& img, const std::filesystem::path& dir, const std::string& nome,
                                 const std::function<void(double)>& progress = {}, const std::atomic<bool>* cancel = nullptr);

} // namespace bs::disc
