# third_party/

Cada dependência entra via CMake FetchContent ou submódulo **quando o marco que
a usa chegar**. Toda dependência precisa ter licença compatível com a do
projeto (ver LEGAL.md).

| Dependência | Licença | Uso | Marco |
|---|---|---|---|
| libchdr | BSD-3 | leitura de CHD | M1 |
| SHA-256 / CRC32 | implementação própria | hashes do extrator | M1 |
| toml++ | MIT | config por jogo | M3 |
| SDL3 | zlib | janela, input, áudio | M4 |
| Vulkan-Headers 1.3.282 | Apache-2.0 | backend Vulkan; só `vulkan.h`, `vulkan_core.h`, `vk_platform.h` e `vk_video/` (`third_party/vulkan`, a mesma versão que o SDL3 3.2.30 traz); o carregador é aberto em tempo de execução, sem volk nem VMA | N7b |
| xxHash | BSD-2 | cache de texturas/pipelines | M5 |
| pl_mpeg | MIT | conversão dos filmes no Builder; no Linux, os filmes do disco tocados pelo jogo (`third_party/pl_mpeg`) | N5, N9-L |
| dr_flac | domínio público ou MIT-0 | FLAC de mods de áudio (`third_party/dr_libs`) | N6 |
| libogg + libvorbis | BSD-3 (Xiph.Org) | OGG/Vorbis de mods de áudio, só a decodificação (`third_party/xiph`) | N6 |
| FreeType 2.13.3 | FTL (FreeType License; o projeto escolhe a FTL da licença dupla FTL/GPLv2) | texto do menu e das legendas no Linux; FetchContent da tag `VER-2-13-3`, sem zlib, PNG, bzip2, Brotli nem HarfBuzz. Só no build do Linux | N9-L |
| Noto Sans 2.015 (Regular e SemiBold) | SIL OFL 1.1 | fonte do texto no Linux, embutida no executável (`third_party/fonts/noto-sans`, com o `OFL.txt`) | N9-L |

**Avisos de distribuição (pacote Linux, L5 do N9-L):** a FTL pede o crédito na documentação do produto: "Portions of this
software are copyright © 2024 The FreeType Project (www.freetype.org). All rights reserved." A OFL pede o texto da licença junto
da fonte: o `OFL.txt` vai no pacote. O nome "Noto" não é usado por nenhuma fonte modificada (a fonte vai como está).

**Ferramenta externa, não distribuída:** no Linux, filmes de mod que não sejam MPEG (MP4) tocam pelo `ffmpeg` e `ffprobe` do
sistema, executados como processos separados, se existirem (N9-L, decisão do mantenedor de 2026-11-03). O jogo não liga a
nenhuma biblioteca do FFmpeg nem a distribui.
