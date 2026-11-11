#pragma once
// N9.6 fase 2b (docs/N9-6-AUDITORIA.md secao 9): o catalogo de materiais da Remastered, um so para as tres frentes que
// identificam texturas (N9.6 materiais e iluminacao, N9.7 agua, N9.8 texturas HD). A chave e o TextureId do contrato do
// N7 (texture_replacement.h): dimensoes + FNV-1a dos texels ARGB8888 decodificados da textura ORIGINAL, nunca o endereco
// na VRAM, a cor do desenho ou a lista; a substituicao HD de uma textura continua com o material dela.
//
// Cada linha e medida (o inventario do bs-ir-tela texturas numa IR guardada, com a cobertura e a prova); sem curinga e sem
// heuristica. So a Remastered consulta o catalogo; na Original, no software e com o catalogo vazio, nada muda. O catalogo
// guarda so identidades (hashes), nenhuma arte.
#include "render/texture_replacement.h"

#include <cstdint>
#include <span>

namespace bs::native::render {

// o que o material pede ao renderizador (bits; 0 = nenhum)
enum MaterialFlag : uint32_t {
    // translucida que funciona como superficie (alfa cheio na maior parte): recebe o AO do opaco de baixo onde esta a ate
    // 1 % de 1/w dele (um decalque); mais longe, nada (o AO nao tem a geometria dela)
    kMaterialAoSurface = 1u << 0,
};

struct MaterialEntry {
    TextureId   id;
    uint32_t    flags;
    const char* what;  // onde foi medido e o que e
};

std::span<const MaterialEntry> material_table();

// os bits do material da textura (0 = fora do catalogo)
uint32_t material_flags(const TextureId& id);

} // namespace bs::native::render
