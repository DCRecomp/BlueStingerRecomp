#pragma once
// N9.8 fase 8.2: um codificador de PNG proprio e minimo (sem compressao: blocos "stored" do deflate), para exportar as
// texturas originais de uma execucao local (a base de um pacote HD, BS_TEXTURAS_EXPORTAR, so na validacao) e para os
// testes do carregador. Correto e legivel por qualquer leitor de PNG; o tamanho e o dos texels (sem ganho de compressao).
#include <cstdint>
#include <span>
#include <string>

namespace bs::native::render {

// w x h texels ARGB8888 (0xAARRGGBB, de cima para baixo) em PNG RGBA de 8 bits (alpha = true) ou RGB (alpha = false: o
// canal alfa e descartado). Vazio se as dimensoes nao batem com os texels.
std::string encode_png(uint32_t width, uint32_t height, std::span<const uint32_t> argb, bool alpha = true);

} // namespace bs::native::render
