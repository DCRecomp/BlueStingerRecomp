# Blue Stinger Recompiled

<a href="https://recomplauncher.com/game/bluestingerrecomp"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

Também disponível no [RecompLauncher](https://recomplauncher.com/game/bluestingerrecomp), que instala e atualiza o jogo
direto deste repositório.

*Also available on [RecompLauncher](https://recomplauncher.com/game/bluestingerrecomp), which installs and updates the
game straight from this repository.*

Recompilação estática (AOT) de **Blue Stinger** (Sega Dreamcast, 1999) para um executável nativo de **Windows x64**, em
C++20. O código do jogo é traduzido instrução por instrução para C++, e as partes do console (arquivos, tempo, controles,
saves, filmes, áudio e vídeo) foram trocadas, uma por vez, por versões nativas de PC, sempre comparadas com o jogo
original rodando no emulador Flycast. Não usa BIOS nem emulação do processador.

*A static (AOT) recompilation of **Blue Stinger** (Sega Dreamcast, 1999) into a native **Windows x64** executable, in
C++20. The game code is translated instruction by instruction into C++, and the console parts (files, timing, controls,
saves, movies, audio and video) were replaced, one at a time, by native PC versions, always checked against the original
game running in the Flycast emulator. No BIOS and no CPU emulation.*

> **Este repositório não contém o jogo** nem nenhum dado dele. Você precisa da sua própria cópia do disco (GDI, CUE/BIN,
> CHD ou CDI). Ver [LEGAL.md](LEGAL.md).
>
> ***This repository does not contain the game** or any of its data. You need your own copy of the disc (GDI, CUE/BIN,
> CHD or CDI). See [LEGAL.md](LEGAL.md).*

## Status

Atualizado em 24/11/2026. O jogo já roda no Windows a partir do seu disco, com controles (teclado e controle), saves,
filmes, áudio e vídeo nativos, renderização Vulkan, câmera livre e legendas em português e inglês. A versão atual é uma
**pré-release**: [v0.9.7-5](https://github.com/DCRecomp/BlueStingerRecomp/releases/tag/v0.9.7-5). Os 16 filmes widescreen
estão publicados e já são baixados por ela.

*Updated on 2026-11-24. The game already runs on Windows from your disc, with native controls (keyboard and gamepad),
saves, movies, audio and video, Vulkan rendering, a free camera and subtitles in Portuguese and English. The current
version is a **pre-release**: [v0.9.7-5](https://github.com/DCRecomp/BlueStingerRecomp/releases/tag/v0.9.7-5). All 16
widescreen movies are published and it already downloads them.*

Já no código, para a próxima versão: texturas HD e antialiasing na Remastered, legendas em espanhol, francês, russo,
japonês e chinês, e o suporte ao RecompLauncher.

*Already in the code, for the next version: HD textures and antialiasing in the Remastered edition, subtitles in Spanish,
French, Russian, Japanese and Chinese, and RecompLauncher support.*

Rumo à versão 1.0: as etapas N0 a N6 estão concluídas, N7 e N8 estão em 95% e o N9 tem 6 de 8 partes com o trabalho
técnico concluído. A campanha completa ainda não começou. Isso dá **cerca de 88%**: as 11 etapas da 1.0 (N0 a N9 e a
campanha) têm o mesmo peso, e cada parte com trabalho técnico concluído conta como inteira.

*Towards version 1.0: stages N0 to N6 are done, N7 and N8 are at 95%, and N9 has 6 of its 8 parts technically done. The
full playthrough has not started yet. That is **about 88%**: the 11 stages of 1.0 (N0 to N9 and the playthrough) have equal
weight, and each technically finished part counts as complete.*

## Planejamento / Roadmap

O caminho foi dividido em duas fases: o que entra na **versão 1.0** e o que vem **depois dela**. Em 22/11/2026 o escopo
da 1.0 foi reduzido: controles avançados, acessibilidade, recursos extras e otimização saíram do N9 e passaram para o
N10, e os mods passaram para o N11.

*The plan is split into two phases: what goes into **version 1.0** and what comes **after it**. On 2026-11-22 the 1.0
scope was reduced: advanced controls, accessibility, extra features and optimization moved from N9 to N10, and mods moved
to N11.*

✅ concluído / done · 🟡 em andamento ou aguardando conferência / in progress or awaiting review · ⬜ não iniciado / not started

### Versão 1.0 / Version 1.0

| Etapa / Stage | O que é / What it is | Status |
|---|---|---|
| N0–N6 | Base, arquivos, tempo, controles, saves, filmes e áudio nativos / Foundation, native files, timing, controls, saves, movies and audio | ✅ |
| N7 | Renderização Vulkan / Vulkan rendering | 🟡 95%: falta uma comparação de neblina com o original / one fog comparison against the original is missing |
| N8 | Remoção da simulação do console / Console simulation removed | 🟡 95%: falta a campanha completa e uma falha de áudio intermitente / full playthrough and an intermittent audio fault pending |
| N9 | Remasterização: edições **Original** e **Remastered** do mesmo jogo / Remaster: **Original** and **Remastered** editions of the same game | 🟡 ver abaixo / see below |
| Campanha / Playthrough | Zerar o jogo do início ao fim, gravado e reproduzível, nas duas edições / Finish the game start to end, recorded and reproducible, in both editions | ⬜ obrigatória para a 1.0 / required for 1.0 |

| N9 | Parte / Part | Status |
|---|---|---|
| N9.0 | Duas edições no mesmo executável / Two editions in the same executable | ✅ |
| N9.1 | Instalador gráfico / Graphical installer | 🟡 pronto, falta conferência visual / done, visual review pending |
| N9.2 | Interface da Remastered / Remastered interface | 🟡 pronto, falta conferência visual / done, visual review pending |
| N9.3 | Widescreen 16:9 e HUD, incluindo os filmes widescreen / 16:9 widescreen and HUD, including widescreen movies | 🟡 pronto, falta conferência visual; filmes: 16 de 16 aprovados e publicados / done, visual review pending; movies: 16 of 16 approved and published |
| N9.4 | Iluminação, sombras e oclusão ambiente / Lighting, shadows and ambient occlusion | 🟡 pronto, falta conferência visual / done, visual review pending |
| N9.5 | Água remasterizada / Remastered water | 🟡 só o cais; faltam outras áreas / docks only; other areas pending |
| N9.6 | Texturas HD, antialiasing e pós-processamento / HD textures, antialiasing and post-processing | 🟡 integrado ao código, falta conferência visual / merged into the code, visual review pending |
| N9.7 | Estabilização final / Final stabilization | ⬜ |

O suporte a Linux (N9-L) segue em paralelo: os passos L0 a L5 já foram publicados.

*Linux support (N9-L) runs in parallel: steps L0 to L5 are already published.*

### Depois da 1.0 / After 1.0

| Etapa / Stage | O que é / What it is | Status |
|---|---|---|
| N10.0 | Controles e experiência de PC: remapeamento no menu, ícones por controle, vários monitores / Controls and PC experience: in-menu remapping, per-gamepad icons, multiple monitors | ⬜ |
| N10.1 | Cinemáticas, áudio e acessibilidade: filmes e músicas substituíveis, opções de legenda e de HUD / Cutscenes, audio and accessibility: replaceable movies and music, subtitle and HUD options | ⬜ |
| N10.2 | Recursos extras: mapa, estatísticas, modo foto, conquistas locais / Extra features: map, statistics, photo mode, local achievements | ⬜ |
| N10.3 | Desempenho e compatibilidade: NVIDIA, AMD e Intel, até 4K, Linux / Performance and compatibility: NVIDIA, AMD and Intel, up to 4K, Linux | ⬜ |
| N11 | Mods e sistemas do jogo em C++ editável / Mods and game systems as editable C++ | ⬜ |

Os números antigos continuam nos documentos e nos commits anteriores. Equivalência: N9.5 → N9.3, N9.6 → N9.4, N9.7 →
N9.5, N9.8 → N9.6, N9.12 → N9.7, N9.4 → N10.0, N9.9 → N10.1, N9.10 → N10.2, N9.11 → N10.3 e N10 → N11. O antigo N9.3
(saves de PC) foi cancelado. Detalhes em [docs/N9-PLANO.md](docs/N9-PLANO.md).

*The old numbers remain in earlier documents and commits. Mapping: N9.5 → N9.3, N9.6 → N9.4, N9.7 → N9.5, N9.8 → N9.6,
N9.12 → N9.7, N9.4 → N10.0, N9.9 → N10.1, N9.10 → N10.2, N9.11 → N10.3 and N10 → N11. The old N9.3 (PC saves) was
cancelled. Details in [docs/N9-PLANO.md](docs/N9-PLANO.md).*

## Bugs reportados / Reported bugs

Problemas e pedidos enviados pelas [issues](https://github.com/DCRecomp/BlueStingerRecomp/issues) e o estado de cada um.
Se o seu problema não estiver aqui, abra uma issue.

*Problems and requests sent through [issues](https://github.com/DCRecomp/BlueStingerRecomp/issues) and the status of
each one. If your problem is not listed, please open an issue.*

✅ resolvido / fixed · ❌ em aberto / open

| Issue | Problema / Problem | Status | Versão / Version |
|---|---|---|---|
| [#1](https://github.com/DCRecomp/BlueStingerRecomp/issues/1) | CDIs de outras fontes eram recusados / CDIs from other sources were rejected | ✅ | v0.9.7-3 |
| [#1](https://github.com/DCRecomp/BlueStingerRecomp/issues/1) | O instalador fecha depois de escolher a imagem / The installer closes after choosing the image | ❌ não reproduzido nos nossos testes; quem abriu a issue já instala, falta o retorno de outro usuário / not reproduced in our tests; the reporter can install now, waiting for another user's feedback | — |
| [#2](https://github.com/DCRecomp/BlueStingerRecomp/issues/2) | Imagens CHD não eram aceitas / CHD images were not accepted | ✅ confirmado por quem relatou / confirmed by the reporter | v0.9.7-3 |
| [#3](https://github.com/DCRecomp/BlueStingerRecomp/issues/3) | Imagens GDI não eram aceitas / GDI images were not accepted | ✅ | v0.9.7-3 |
| [#3](https://github.com/DCRecomp/BlueStingerRecomp/issues/3) | O instalador fechava após o seletor de arquivos, por causa de uma extensão do Explorer / The installer closed after the file picker, because of an Explorer extension | ✅ | v0.9.7-4 |
| [#3](https://github.com/DCRecomp/BlueStingerRecomp/issues/3) | A instalação exigia o Visual Studio / Installing required Visual Studio | ✅ | v0.9.7-5 |
| [#4](https://github.com/DCRecomp/BlueStingerRecomp/issues/4), [#5](https://github.com/DCRecomp/BlueStingerRecomp/issues/5) | O Director's Cut não era suportado / The Director's Cut was not supported | ✅ | v0.9.7-4 |
| [#4](https://github.com/DCRecomp/BlueStingerRecomp/issues/4) | Versão PAL em 480p perde a sincronia e trava na primeira cena da Janine no QG / PAL version at 480p loses sync and freezes in the first Janine scene at HQ | ❌ ainda não testado / not tested yet | — |
| testes internos / internal tests | Falhas intermitentes no áudio em tempo real / Intermittent real-time audio dropouts | ❌ | — |

## Versões suportadas / Supported versions

| Produto / Product | Versão / Version | Região / Region | Edições / Editions |
|---|---|---|---|
| T13001N | V1.000 (1999-07-05) | NTSC-U (EUA / USA) | Original e Remastered / Original and Remastered |
| T13001D 18 | V1.000 | PAL alemã / German PAL | Original, 50 Hz |
| T13001D-05 | V1.000 | PAL inglesa / English PAL | Original, 50 Hz |
| T13001D 09 | V1.000 | PAL francesa / French PAL | Original, 50 Hz |
| HDR-0003 | V1.006 | NTSC-J (Japão / Japan) | Original |
| Director's Cut | EN e FR / EN and FR | japonesa com o patch de [espouaisso](https://github.com/espouaisso/Blue-Stinger-Directors-Cut-Patcher) / Japanese with the patch by [espouaisso](https://github.com/espouaisso/Blue-Stinger-Directors-Cut-Patcher) | Original |

## Como usar / How to use

Baixe a [última versão](https://github.com/DCRecomp/BlueStingerRecomp/releases), abra o `BlueStinger-Recompiler.exe` e
escolha o seu disco. O instalador compila o jogo na sua máquina e nada do disco sai dela. Imagens aceitas: **GDI**,
**CUE/BIN** (como as do Redump), **CHD** e **CDI**, também dentro de **.zip** ou **.7z**.

*Download the [latest release](https://github.com/DCRecomp/BlueStingerRecomp/releases), open `BlueStinger-Recompiler.exe`
and pick your disc. The installer builds the game on your machine and nothing from the disc leaves it. Accepted images:
**GDI**, **CUE/BIN** (like Redump's), **CHD** and **CDI**, also inside **.zip** or **.7z**.*

Para compilar, o instalador usa o Visual Studio (ou os Build Tools), se estiver instalado. Se não estiver, ele oferece um
**compilador portátil**: baixa o Zig, o CMake e o Ninja oficiais (cerca de 125 MB, cada arquivo conferido pelo SHA-256) e
os guarda só no seu perfil, em `%LOCALAPPDATA%\BlueStingerRecomp\compilador`, sem instalar nada no sistema.

*To build, the installer uses Visual Studio (or the Build Tools) when it is installed. Otherwise it offers a **portable
compiler**: it downloads the official Zig, CMake and Ninja (about 125 MB, each file checked by SHA-256) and keeps them only
in your profile, in `%LOCALAPPDATA%\BlueStingerRecomp\compilador`, without installing anything on the system.*

Pela linha de comando, o Builder faz o mesmo:

*From the command line, the Builder does the same:*

```
bs-builder --disc <seu_disco.cdi> --out <pasta> --kit . --sdl3 build/msvc-release/_deps/sdl3-src
bs-builder ... --compilador-portatil
```

## Para desenvolvedores / For developers

Requisitos: Visual Studio 2022 ou mais novo (ou os Build Tools) com CMake e Ninja, ou Clang. O jogo também compila com o
Zig no alvo `x86_64-windows-gnu`. Abra um *Developer PowerShell* e rode:

*Requirements: Visual Studio 2022 or newer (or the Build Tools) with CMake and Ninja, or Clang. The game also builds with
Zig for the `x86_64-windows-gnu` target. Open a *Developer PowerShell* and run:*

```
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

O teste `real_disc` usa a sua imagem quando `BS_DISC_IMAGE` aponta para o `.cdi`, `.gdi`, `.cue` ou `.chd`. Sem essa
variável, ele é ignorado.

*The `real_disc` test uses your image when `BS_DISC_IMAGE` points to the `.cdi`, `.gdi`, `.cue` or `.chd`. Without that
variable it is skipped.*

Ferramentas de linha de comando:

*Command-line tools:*

```
bs-extract --info "ISO/Blue Stinger.cdi"            # mostra faixas e hash sem gravar nada / shows tracks and hash, writes nothing
bs-extract "ISO/Blue Stinger.cdi" game-data         # extrai e valida o hash / extracts and checks the hash
bs-recomp  config/bluestinger_us.toml               # gera generated/*.cpp / generates generated/*.cpp
bs-disasm  game-data/1ST_READ.BIN 0x8C010000 0x8C010000 64   # disassembly: base, início, quantidade / base, start, count
```

O executável `1ST_READ.BIN` precisa ter o SHA-256 de uma versão suportada (`tools/extractor/known_versions.h`). Com
`--force`, a extração acontece mesmo assim, e o resultado fica marcado como não verificado.

*The `1ST_READ.BIN` executable must match the SHA-256 of a supported version (`tools/extractor/known_versions.h`). With
`--force` it is extracted anyway, and the result is marked as unverified.*

Detalhes e evidências de cada verificação estão no [STATUS.md](STATUS.md). A arquitetura está no
[ARCHITECTURE.md](ARCHITECTURE.md), e os planos de cada etapa, em [docs/](docs/).

*Details and evidence for every check are in [STATUS.md](STATUS.md). The architecture is in
[ARCHITECTURE.md](ARCHITECTURE.md), and each stage's plans are in [docs/](docs/).*

As pastas locais `ISO/`, `game-data/`, `generated/`, `analysis/`, `saves/` e `build/` são ignoradas pelo git.

*The local folders `ISO/`, `game-data/`, `generated/`, `analysis/`, `saves/` and `build/` are ignored by git.*
