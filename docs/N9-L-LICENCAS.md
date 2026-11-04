# N9-L: auditoria preliminar de licenças do pacote Linux

**Situação (2026-11-03):** auditoria **preliminar**, feita pelo inventário do pacote `l5zig6` (SHA-256 `70a785ee…eb4474`) e
das fontes. **Não é parecer jurídico e não declara conformidade.** Nenhum pacote pode ser publicado antes de as pendências da
seção 4 serem resolvidas e revistas pelo mantenedor (pedido de 2026-11-03: "Não publique pacotes antes da revisão das
obrigações de licença").

## 1. O que o pacote distribui

| Parte | Conteúdo | Forma |
|---|---|---|
| `bs-builder` | o Builder (código do projeto, MIT) com libc++, libc++abi, libunwind e compiler-rt estáticos; liga só `libc`, `libm`, `libdl`, `libpthread` do sistema | binário |
| `kit/` | fontes do projeto (MIT) e de terceiros (FreeType, toml++, pl_mpeg, dr_libs, libogg, libvorbis, cabeçalhos Vulkan), a fonte Noto Sans, o SDL3 3.2.30 **pré-compilado** (`libSDL3.a` e cabeçalhos) | fontes e uma biblioteca estática |
| `toolchain/` | Zig 0.13.0 completo (binário com LLVM/Clang/LLD 18.1.6 e `lib/`: libc de vários alvos, libc++, compiler-rt, tsan, std do Zig), CMake 3.30.5 e Ninja 1.12.1 oficiais, sem modificação | binários e fontes de terceiros, sem modificação |
| `licencas/`, `toolchain/licencas/` | os textos já incluídos (seção 2) | texto |

O jogo **não** é distribuído: o Builder o compila na máquina do usuário. Ele leva consigo a pasta `licencas/` (11 textos) para
a instalação do jogo.

## 2. Componentes, licenças e textos

Legenda: **ok** = licença identificada e texto no pacote; **falta** = obrigação identificada sem o texto ou aviso no pacote;
**rever** = dúvida que pede leitura cuidadosa ou decisão.

| Componente (versão) | Licença | Texto no pacote | Situação |
|---|---|---|---|
| Blue Stinger Recompiled (projeto) | MIT | `BlueStingerRecompiled-MIT.txt` | ok |
| Zig 0.13.0 (compilador e `lib/std`) | MIT | `Zig-MIT.txt`, `zig/LICENSE` | ok |
| LLVM, Clang, LLD 18.1.6 (dentro do binário `zig`) | Apache-2.0 WITH LLVM-exception | `LLVM-Clang-LLD-libcxx-Apache-2.0-LLVM-exception.txt` | ok; rever se o binário do Zig traz componentes LLVM com avisos próprios (`NOTICE`, código de terceiros dentro do LLVM) |
| libc++, libc++abi, libunwind, compiler-rt, tsan (`zig/lib`) | Apache-2.0 WITH LLVM-exception | o mesmo texto e os `LICENSE.TXT` de cada pasta | ok; a exceção LLVM dispensa o aviso no binário que os embute (`bs-builder` e o jogo) |
| glibc (cabeçalhos e fontes de partida em `zig/lib/libc/glibc`) | LGPL-2.1-or-later, partes com outras licenças (`LICENSES`) | `glibc-LICENSES.txt` | rever: o texto da LGPL-2.1 em si não está no pacote (o `LICENSES` da glibc remete a `COPYING.LIB`); confirmar que o que o Zig compila para o alvo glibc (arquivos de partida, `libc_nonshared`) traz a exceção de ligação da glibc |
| musl (`zig/lib/libc/musl`) | MIT | `musl-COPYRIGHT.txt` | ok |
| mingw-w64 (`zig/lib/libc/mingw`, `include/any-windows-any`) | várias (ZPL-2.1, domínio público e outras, conforme `COPYING`) | `mingw-w64-COPYING.txt` | rever: o `COPYING` do mingw-w64 lista licenças por diretório; conferir se todas estão representadas |
| wasi-libc (`zig/lib/libc/wasi`) | Apache-2.0 WITH LLVM-exception, Apache-2.0, MIT; cloudlibc BSD-2-Clause | `wasi-libc-LICENSE.txt` (só o principal) | **falta**: o BSD-2 do cloudlibc e o COPYRIGHT do musl dentro do wasi não estão em `licencas/` (estão só dentro de `zig/lib`) |
| **Cabeçalhos do kernel Linux** (`zig/lib/libc/include/*-linux-any`, 1367 arquivos) | **GPL-2.0 WITH Linux-syscall-note** (SPDX em cada arquivo) | **nenhum**: o LEIA-ME do compilador cita a licença, mas o texto da GPL-2.0 e o da nota de syscall não estão no pacote | **falta** |
| **Cabeçalhos do macOS** (`zig/lib/libc/include/any-macos-any`, 556 arquivos, e `zig/lib/libc/darwin`) | Apple (APSL-2.0 e outras, por arquivo) | **nenhum**, e o LEIA-ME do compilador não os cita | **falta** e **rever** |
| CMake 3.30.5 | BSD-3-Clause; componentes embutidos (curl, libarchive, liblzma, librhash, libuv, nghttp2, kwsys, zlib, zstd) | `CMake-BSD-3.txt` e `CMake-componentes/` | ok; rever se a lista de componentes do binário oficial é completa (o `Copyright.txt` do CMake é a fonte) |
| Ninja 1.12.1 | Apache-2.0 | `Ninja-Apache-2.0.txt` | ok |
| SDL3 3.2.30 (pré-compilado no kit e embutido no jogo) | zlib | `SDL3-zlib.txt` | ok para o SDL |
| HIDAPI (dentro do `libSDL3.a`, `SDL_HIDAPI=ON`) | à escolha: GPL-3.0, BSD-style ou a original do HIDAPI | nenhum | **falta**: escolher a BSD (ou a original) e incluir o texto |
| yuv2rgb (dentro do SDL3) | BSD-3-Clause (Adrien Descamps) | nenhum | **falta**: a BSD-3 pede o aviso na documentação de quem distribui binário (o `libSDL3.a` do kit) |
| libm do SDL3 (fdlibm, Sun) | aviso permissivo da SunPro ("provided that this notice is preserved") | não | rever |
| FreeType 2.13.3 (fontes no kit; embutido no jogo) | FTL (escolhida) ou GPL-2.0 | `FreeType-FTL.txt`, `FreeType-LICENSE.txt` | ok para o texto; **falta** o crédito que a FTL pede na documentação do produto ("Portions of this software are copyright © The FreeType Project"): o LEIA-ME do jogo não o traz |
| Noto Sans 2.015 (embutida no jogo) | OFL-1.1 | `NotoSans-OFL-1.1.txt` (também na pasta do jogo) | ok; rever o aviso de copyright da fonte junto do texto |
| pl_mpeg | MIT | `pl_mpeg-MIT.txt` | ok |
| dr_flac (dr_libs) | MIT-0 ou domínio público | `dr_flac-MIT0.txt` | ok |
| libogg, libvorbis | BSD-3-Clause | `libogg-BSD.txt`, `libvorbis-BSD.txt` | ok |
| toml++ | MIT | `tomlplusplus-MIT.txt` | ok |
| Cabeçalhos Vulkan | Apache-2.0 (e MIT em parte) | `Vulkan-Headers-Apache-2.0.txt` | ok; rever se algum cabeçalho usado é MIT |

## 3. O que não basta

- **Não ligar dinamicamente com GPL não elimina obrigações.** Os 1367 cabeçalhos do kernel são *distribuídos* dentro do Zig, e a
  GPL-2.0 pede que o texto da licença acompanhe a cópia, mesmo sem nada ser ligado ao jogo. A nota de syscall trata do uso
  dos cabeçalhos por programas, não da redistribuição dos próprios cabeçalhos.
- **Redistribuir o Zig sem modificação não transfere as obrigações ao projeto Zig.** Quem entrega o `.tar.gz` é quem distribui.
- **O texto já presente dentro de `zig/lib/...` pode não ser suficiente** onde a licença pede o aviso "na documentação ou em
  outros materiais fornecidos com a distribuição": por isso a pasta `licencas/` deve reunir tudo.

## 4. Pendências antes de qualquer publicação

1. **Cabeçalhos do kernel Linux:** incluir o texto da GPL-2.0 e o da Linux-syscall-note (`LICENSES/preferred/GPL-2.0` e
   `LICENSES/exceptions/Linux-syscall-note` da árvore do kernel de onde o Zig 0.13 os tirou), com a origem e a versão.
2. **Cabeçalhos do macOS e `darwin/`:** levantar as licenças por arquivo (APSL-2.0 e outras) e incluir os textos e avisos, ou
   retirá-los (item 7).
3. **HIDAPI e yuv2rgb do SDL3:** incluir a BSD do HIDAPI (opção declarada) e a BSD-3 do yuv2rgb em `licencas/` e na pasta de
   licenças do jogo; conferir o aviso da libm.
4. **FreeType:** pôr o crédito da FTL no LEIA-ME do jogo e no do Builder.
5. **glibc:** incluir o texto da LGPL-2.1 e confirmar a exceção de ligação dos arquivos que o Zig compila para o alvo glibc.
6. **wasi-libc, mingw-w64, CMake, LLVM:** completar os textos dos subcomponentes (cloudlibc, licenças por diretório do mingw,
   `Copyright.txt` do CMake, `NOTICE` do LLVM se houver no binário).
7. **Decisão do mantenedor: podar o Zig.** O jogo só usa o alvo `x86_64-linux-gnu.2.31`. Tirar do pacote os cabeçalhos e as
   libc de macOS, Windows, WASI e das outras arquiteturas reduz o tamanho (o `zig/lib/libc` tem 13 631 dos 19 519 arquivos do
   pacote) e as obrigações (some o item 2, encolhe o 6), mas muda o compilador da D-L2 de "oficial sem modificação" para "oficial
   podado". Exige nova decisão, nova montagem do pacote e as provas que dependem dele (suíte B1–B14 e A3). Recomendação: podar,
   decidido antes da L8.
8. **Procedência:** o `VERSAO.txt` já registra os SHA-256 dos downloads oficiais do Zig, do CMake e do Ninja e a versão do SDL3;
   acrescentar a origem (URL) de cada um e o commit da fonte do SDL3 pré-compilado.

Nada disso muda o pacote que vai para a sessão em Ubuntu nativo (o `l5zig6`, que não é publicado): as correções entram no
pacote da L8, que será montado e provado de novo.
