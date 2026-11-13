# stb_image (terceiros, domínio público ou MIT)

Decodificador de PNG das texturas HD de mods (N9.8 fase 8.2, `docs/N9-8-AUDITORIA.md` seção 9; decisão D8-2). O jogo o usa
só para os arquivos `texturas\*.png` dos pacotes de `mods\`; as texturas do disco não passam por ele.

- Origem: https://github.com/nothings/stb (`stb_image.h`, cabeçalho único)
- Commit fixo: `f58f558c120e9b32c217290b80bad1a0729fbb2c` (2025-05-26, "fix typo"); versão no cabeçalho `v2.30`.
- `stb_image.h` SHA-256: `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`
- Licença: escolha entre MIT (alternativa A) e domínio público (alternativa B, Unlicense), no fim do cabeçalho, copiadas
  em `LICENSE`. O projeto usa a alternativa B (domínio público): nenhum aviso obrigatório no binário.
- Uso: só o PNG (`STBI_ONLY_PNG`), da memória (`STBI_NO_STDIO`: o arquivo é lido pelo jogo, com o limite de tamanho), com as
  dimensões conferidas por `stbi_info_from_memory` antes da decodificação. Sem alterações locais. A cópia que o SDL3 traz em
  `src/video` tem mudanças do SDL e não é usada.
