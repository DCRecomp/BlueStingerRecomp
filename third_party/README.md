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

**Pacote Linux do Builder (L5 do N9-L), fora do repositório:** montado por `tools/dist/linux/make_builder_linux.sh` a partir
de um commit. Leva, além do kit de fontes e das licenças acima:

| Peça | Licença | Origem e uso |
|---|---|---|
| SDL3 3.2.30 pré-compilado (`kit/third_party/SDL3-linux`) | zlib | compilado por `tools/dist/linux/sdl3_linux.sh` com o compilador portátil, X11, Wayland, PipeWire, PulseAudio e ALSA carregados em tempo de execução; o usuário não precisa dos cabeçalhos do sistema |
| FreeType 2.13.3 e toml++ 3.4.0 em fontes (`kit/third_party`) | FTL; MIT | compilados na máquina do usuário |
| Zig 0.13.0 (`toolchain/zig`) | MIT; o LLVM/Clang/LLD 18.1.6 embutido e o libc++/libc++abi/libunwind: Apache-2.0 com exceção LLVM; cabeçalhos e fontes de bibliotecas C (glibc LGPL-2.1+, musl MIT, mingw-w64, wasi-libc, cabeçalhos do kernel GPL-2.0 com syscall-note) | compilador portátil (D-L2, escolha do mantenedor de 2026-11-03): alvo x86-64 base com glibc 2.31, libc++ estática. Download oficial conferido pelo SHA-256 publicado |
| CMake 3.30.5 (`toolchain/cmake`, só o executável e os módulos) | BSD-3 (e as licenças das bibliotecas que ele embute, em `licencas/compilador/CMake-componentes`) | binário oficial da Kitware, SHA-256 publicado |
| Ninja 1.12.1 (`toolchain/bin/ninja`) | Apache-2.0 | binário oficial (a release não publica soma; o SHA-256 medido está no `toolchain_linux.sh`) |

Os textos de licença vão em `licencas/` e `licencas/compilador/` do pacote. **Pendência para a publicação (L8):** o tarball do
Zig não traz o texto da GPL-2.0 nem o da Linux-syscall-note dos cabeçalhos do kernel; incluí-los em `licencas/compilador/` antes
de publicar.

**Ferramenta externa, não distribuída:** no Linux, filmes de mod que não sejam MPEG (MP4) tocam pelo `ffmpeg` e `ffprobe` do
sistema, executados como processos separados, se existirem (N9-L, decisão do mantenedor de 2026-11-03). O jogo não liga a
nenhuma biblioteca do FFmpeg nem a distribui.
