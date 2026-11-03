# pl_mpeg (terceiros, MIT)

Decodificador MPEG-1 de referência do Builder (N5, decisão D1, `docs/N5-PLANO.md`), que converte os filmes do disco do usuário.
No Linux (N9-L, L4) o próprio jogo o usa para tocar os SFD do disco, sem conversão (`native/video/sfd_movie.cpp`). O jogo do
Windows não o inclui.

- Origem: https://github.com/phoboslab/pl_mpeg
- Commit fixo: `c871f2be022ece7ef4f64230b4fb8e1fb9eb6023` (2025-12-30, "Fix corrupt slice check; close #64")
- `pl_mpeg.h` SHA-256: `3a8cb30c83c2a1147719c30fe0c8b93da2987aa43140077c575b39aaa75fc2c9`
- Licença: MIT. O projeto a declara no próprio cabeçalho (`SPDX-License-Identifier: MIT`) e no
  README, sem arquivo de licença separado; o `LICENSE` daqui é o texto padrão da MIT com a autoria do
  cabeçalho. Sem alterações locais no `pl_mpeg.h`.
- Ajustes na unidade de implementação (`native/video/pl_mpeg_impl.c`), sem tocar no cabeçalho: o fim do stream em memória (N5) e,
  desde o N9-L, um alocador com folga zerada de 8 KiB no fim de cada bloco. A conferência `max_address` de
  `plm_video_process_macroblock` não conta a linha a mais da interpolação vertical de meio pixel, e um fluxo corrompido lia até uma
  linha além do último plano (achado pelo AddressSanitizer). Fluxos válidos não mudam: a decodificação dos 16 SFD do disco é
  idêntica antes e depois.
