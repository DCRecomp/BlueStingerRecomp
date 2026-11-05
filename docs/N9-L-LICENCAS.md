# Dependências e licenças: política e inventário (Windows e Linux)

**Atualização (2026-11-04):** gate da v0.8.0 Preview na seção 9 (textos e avisos acrescentados aos pacotes; nenhum
bloqueador; S1, S2, S3 e a troca do CMake da S5 continuam propostas).

**Situação (2026-11-03):** inventário **preliminar**, feito pelas fontes do repositório, pelo pacote Linux `l5zig6` (SHA-256
`70a785ee…eb4474`, árvore `72132c1a`) e pelo `make_builder.ps1` do Windows. **Não é parecer jurídico e não declara
conformidade.** Nenhum pacote pode ser publicado antes de resolver as pendências da seção 7 e de o mantenedor as revisar. Pedidos
de 2026-11-03: "Não publique pacotes antes da revisão das obrigações de licença"; release v0.8.0 só com os critérios técnicos e
as obrigações de distribuição satisfeitos.

## 1. Política (regra permanente do mantenedor, 2026-11-03)

> Nenhuma dependência necessária à compilação, execução ou distribuição poderá exigir licença paga, royalties, assinatura,
> autorização comercial individual ou impor restrições incompatíveis com uma distribuição pública, gratuita e open source.

Como aplicar:
- **Preferidas:** licenças permissivas (MIT, BSD, Apache-2.0, zlib, ISC, MIT-0, domínio público).
- **Aceitas quando as obrigações forem cumpridas:** GPL, LGPL, MPL e outras licenças livres. Avisos, textos de licença,
  oferta de código ou copyleft **não** as tornam proibidas.
- **Exigem substituição:** licença paga, royalties contratuais, licença comercial obrigatória, proibição de redistribuir o que o
  projeto precisa redistribuir, ou condição que impeça o uso público e gratuito.
- **Código aberto não basta:** conferir também patentes (codecs), marcas e direitos sobre ativos (arte, áudio, textos do jogo).
- **Nenhuma troca sem necessidade:** um componente que funciona e já permite a redistribuição gratuita pretendida fica. Toda
  substituição exige análise de impacto, testes dirigidos, revisão do Codex e evidência de equivalência (Original fiel, 16
  filmes, áudio, Vulkan, controles, saves, Builder, mods, Windows e Linux).

Situações usadas na matriz: **compatível** · **ajustes** (compatível, falta texto, aviso ou documentação) · **substituir** ·
**análise** (depende de análise adicional).

## 2. O que é distribuído (e o que não é)

| Entregável | Conteúdo | Distribuído pelo projeto? |
|---|---|---|
| Builder Windows (`dist/BlueStinger-Builder`) | `BlueStinger-Recompiler.exe`, `-Developer.exe`, `bs-builder.exe`, DLLs do runtime do MSVC, `licencas/`, kit de fontes (projeto, SDL3 em fontes, toml++, pl_mpeg, dr_flac, libogg, libvorbis, cabeçalhos Vulkan) | sim, quando publicado |
| Builder Linux (`BlueStinger-Builder-*-linux-x86_64.tar.gz`) | `bs-builder`, kit (o mesmo e mais FreeType, Noto Sans, SDL3 pré-compilado), `toolchain/` (Zig, CMake, Ninja), `licencas/` | sim, quando publicado |
| Jogo (`BlueStinger.exe` ou `BlueStinger`) | compilado **na máquina do usuário** a partir do disco dele | **não** (D7); o Builder leva a pasta `licencas/` para a instalação |
| C++ gerado, dados do disco, filmes convertidos | derivados do disco do usuário | **nunca** |

## 3. Matriz de componentes

Integração: **E** = estática (código ou biblioteca dentro do binário) · **D** = dinâmica (biblioteca do sistema aberta em
execução) · **P** = processo externo · **F** = ferramenta de build · **T** = texto, fonte ou dado.

### 3.1 Código dentro do Builder e do jogo

| Componente | Versão | Finalidade | Licença | Origem | Integ. | Redistr. | Obrigações | Patentes e distribuição | Situação | Alternativa |
|---|---|---|---|---|---|---|---|---|---|---|
| Blue Stinger Recompiled | — | o projeto | MIT | este repositório | E | sim | aviso e texto | marcas: "Blue Stinger", "Dreamcast", "Sofdec" e "CRI" usados só para identificar o jogo e os formatos (uso nominativo; ver seção 5) | compatível; marcas: **análise** | — |
| SDL3 | 3.2.30 | janela, entrada, áudio, controles | zlib | github.com/libsdl-org/SDL, tag `release-3.2.30` | E | sim (fontes no kit Windows; `libSDL3.a` no Linux) | aviso nas fontes; texto | — | compatível | — |
| ↳ HIDAPI (dentro do SDL3) | do SDL 3.2.30 | controles por HID | à escolha: GPL-3.0, BSD ou a original do HIDAPI (`src/hidapi/LICENSE.txt`) | SDL | E | sim | escolher a BSD e incluir `LICENSE-bsd.txt` | — | **ajustes** | — |
| ↳ yuv2rgb (dentro do SDL3) | do SDL 3.2.30 | conversão YUV do SDL | BSD-3-Clause (Adrien Descamps) | SDL `src/video/yuv2rgb/LICENSE` | E | sim | aviso na documentação de quem distribui binário | — | **ajustes** | — |
| ↳ libm (fdlibm, dentro do SDL3) | do SDL 3.2.30 | matemática quando a libc não tem | aviso da Sun ("provided that this notice is preserved") | SDL `src/libm` | E | sim | preservar o aviso | — | **ajustes** | — |
| ↳ outros trechos do SDL3 | do SDL 3.2.30 | cabeçalhos Khronos (Apache-2.0/MIT), Valve (controle Steam), Red Hat (EDID), xcb, Igalia, DirectX (MIT) | permissivas | SDL | E (só os do alvo) | sim | avisos | — | **análise**: levantar quais entram em cada alvo e reunir os avisos | — |
| FreeType | 2.13.3 | texto do menu e das legendas (Linux); interface da Remastered nos dois sistemas (N9.2) | FTL (escolhida) ou GPL-2.0 | github.com/freetype, tag `VER-2-13-3` | E | sim (fontes nos kits Linux e, desde o N9.2, Windows) | texto da FTL e **crédito na documentação** | — | **ajustes** (falta o crédito nos LEIA-ME) | — |
| toml++ | 3.4.0 | configuração | MIT | github.com/marzer/tomlplusplus | E | sim (fontes) | aviso e texto | — | compatível | — |
| pl_mpeg | do repositório (`third_party/pl_mpeg`) | MPEG-1 e MP2: conversão no Builder Windows; filmes no jogo Linux | MIT | github.com/phoboslab/pl_mpeg | E | sim | aviso e texto | MPEG-1 vídeo e MP2: patentes vencidas (README do pl_mpeg) | compatível | — |
| Decodificador ADX (Sofdec) | próprio | áudio dos filmes | MIT (projeto) | este repositório | E | sim | — | formato da CRI; nenhuma patente conhecida em vigor; implementação própria | compatível; **análise** de baixa prioridade | — |
| dr_flac | 0.13.4 | FLAC de mods de áudio | MIT-0 ou domínio público | github.com/mackron/dr_libs | E | sim | nenhuma (MIT-0) | FLAC livre de royalties | compatível | — |
| libogg | 1.3.6 | OGG de mods de áudio | BSD-3-Clause | downloads.xiph.org (SHA-256 conferido) | E | sim | aviso e texto | Vorbis livre de royalties | compatível | — |
| libvorbis | 1.3.7 | Vorbis de mods de áudio (só decodificação) | BSD-3-Clause | downloads.xiph.org | E | sim | aviso e texto | idem | compatível | — |
| Vulkan-Headers | 1.3.282 | cabeçalhos do backend Vulkan | Apache-2.0 (partes MIT) | Khronos | E (só cabeçalhos) | sim (fontes) | texto; conferir se algum usado é MIT | — | compatível; **análise** leve | — |
| Noto Sans Regular e SemiBold | 2.015 | fonte do texto no Linux e da interface da Remastered nos dois sistemas (N9.2), embutida | OFL-1.1, sem nome reservado | notofonts | T (embutida) | sim | `OFL.txt` junto e aviso de copyright | — | compatível | — |

### 3.2 Toolchain do pacote Linux (`toolchain/`, redistribuído sem modificação)

| Componente | Versão | Finalidade | Licença | Origem | Integ. | Redistr. | Obrigações | Patentes e distribuição | Situação | Alternativa |
|---|---|---|---|---|---|---|---|---|---|---|
| Zig (compilador e `lib/std`, `compiler_rt`) | 0.13.0 | compilador portátil do Builder e do jogo | MIT | ziglang.org (SHA-256 conferido) | F; `compiler_rt` E no jogo | sim | texto | — | compatível | — |
| LLVM, Clang, LLD (dentro do binário `zig`) | 18.1.6 | compilação e ligação | Apache-2.0 WITH LLVM-exception | zig-bootstrap | F | sim | texto; `NOTICE` se houver | — | compatível; **análise** dos avisos de terceiros do LLVM | — |
| libc++, libc++abi, libunwind | 18 (do Zig) | C++ estático no `bs-builder` e no jogo | Apache-2.0 WITH LLVM-exception | Zig `lib/` | E | sim | a exceção dispensa o aviso no binário | — | compatível | — |
| ThreadSanitizer (`lib/tsan`) | 18 (do Zig) | sanitizador (não usado pelo Builder nem pelo jogo) | Apache-2.0 WITH LLVM-exception, com possível código de terceiros por diretório | Zig `lib/tsan` | — | sim (3,0 MB) | conferir o `LICENSE.TXT` e os subdiretórios de terceiros na cópia exata do Zig 0.13.0 | — | **análise**, ou retirar (poda) | poda |
| glibc: cabeçalhos e fontes de partida | a do Zig 0.13 (versão a registrar), alvo 2.31 | ligação com a glibc do sistema | LGPL-2.1-or-later; 84 fontes com a exceção de ligação; **13 sem a exceção** (`csu/errno.c`, `stat`/`fstat`/`lstat`/`mknod` de compatibilidade, `start-2.33.S` do aarch64) | Zig `lib/libc/glibc` | E (`crt1.o`, `libc_nonshared`) e D (`libc.so.6`) | sim | texto da LGPL-2.1; fontes disponíveis (estão no próprio pacote); permitir religar | — | **ajustes** e **análise** (as 13 sem exceção) | — |
| Cabeçalhos do kernel Linux | do Zig 0.13 | API do sistema no alvo Linux | GPL-2.0 WITH Linux-syscall-note (e variantes OR BSD, LGPL com a nota) | Zig `lib/libc/include/*-linux-*` | só cabeçalhos | sim (1367 arquivos; 979 depois da poda) | **texto da GPL-2.0 e da Linux-syscall-note** junto da cópia | — | **ajustes** | — |
| Cabeçalhos do macOS, `darwin/` | do Zig 0.13 | alvo macOS (não usado) | APSL-2.0 e outras, por arquivo | Zig | — | sim (556 arquivos) | textos e avisos por arquivo | — | **ajustes** ou retirar (poda, seção 6) | poda |
| mingw-w64, wasi-libc (cloudlibc), musl | do Zig 0.13 | alvos Windows, WASI e musl (não usados) | ZPL-2.1, domínio público, BSD-2, MIT, Apache-2.0 | Zig | — | sim | textos por diretório | — | **ajustes** ou retirar (poda) | poda |
| CMake | 3.30.5 | configuração do build | BSD-3-Clause | cmake.org (SHA-256 conferido) | F | sim | texto | — | compatível | — |
| ↳ embutidos no binário do CMake | da 3.30.5 | rede, arquivos, JSON | curl, libarchive, liblzma, librhash, libuv, nghttp2, kwsys, zlib, zstd (textos no pacote); **jsoncpp (MIT), bzip2 (BSD-like), cppdap (Apache-2.0) e OpenSSL 1.1.1v (licença OpenSSL/SSLeay, com cláusula de anúncio) sem texto no pacote** | binário oficial | F | sim | textos e, pela OpenSSL 1.1.1, o aviso em material de divulgação que cite o recurso | OpenSSL 1.1.1 fora de suporte desde 2023 (o Builder não usa rede do CMake) | **ajustes** | CMake oficial com OpenSSL 3 (Apache-2.0), se a versão nova o trouxer (conferir) |
| Ninja | 1.12.1 | execução do build | Apache-2.0 | github.com/ninja-build | F | sim | texto | — | compatível | — |

### 3.3 Windows: compilador e componentes do sistema

| Componente | Versão | Finalidade | Licença | Origem | Integ. | Redistr. | Obrigações | Patentes e distribuição | Situação | Alternativa |
|---|---|---|---|---|---|---|---|---|---|---|
| MSVC (Visual Studio Build Tools) e Windows SDK | instalado pelo usuário | compila o Builder e, na máquina do usuário, o jogo | proprietária (termos da Microsoft); gratuita para uso individual e, desde 2022, para compilar projetos open source; **não redistribuível** | Microsoft | F | não | aceitar os termos da Microsoft | — | **análise**: é a dependência de compilação mais próxima de violar a política (licença proprietária com condições de uso, download obrigatório da Microsoft). Os termos exatos e o caso de organizações precisam de leitura | **llvm-mingw** (Clang, LLD, mingw-w64, libc++; Apache-2.0 com exceção LLVM, ZPL, domínio público): opção B de `docs/DISTRIBUICAO-COMPILADOR.md` |
| Runtime do MSVC (`vcruntime140*.dll`, `msvcp140*.dll`…) | do VS do mantenedor | runtime do Builder | "Distributable Code" da Microsoft | `VCToolsRedist` | D | **sim** (cópia no Builder) | só com o programa, sem modificar; condições da licença do VS de quem monta | — | **análise** | some com llvm-mingw e libc++ estática |
| Media Foundation (H.264 e AAC) | do Windows | Builder: SFD → MP4; jogo: filmes e mods | parte do Windows | sistema | D | não | — | H.264 e AAC têm pools de patentes; o licenciamento é da Microsoft para o codec do sistema; o projeto não distribui codificador nem os MP4 gerados. Edições N precisam do Media Feature Pack (gratuito) | compatível; **análise** de baixa prioridade | proposta S3 (seção 5) |
| XAudio2 2.9, DirectWrite/Direct2D/WIC, XInput, fibras; DWM, Shell (IShellLinkW, SHGetKnownFolderPath), registro HKCU | do Windows 10+ | áudio, legendas, controles; o instalador do N9.1 (desenho, texto, capturas PNG, barra de título escura, atalhos, "Aplicativos instalados") | parte do Windows | sistema | D | não | — | — | compatível | — |
| Segoe UI | do Windows | fonte das legendas no Windows | Microsoft, fonte do sistema | sistema | D (pelo DirectWrite) | não | não copiar a fonte | — | compatível | — |
| Carregador Vulkan (`vulkan-1.dll`) e driver | do driver | Vulkan | do fabricante | sistema | D | não | — | — | compatível | — |

### 3.4 Linux: componentes do sistema (abertos em execução, não distribuídos)

| Componente | Licença | Integ. | Situação |
|---|---|---|---|
| glibc (`libc.so.6`, `libm`, `libdl`, `libpthread`) | LGPL-2.1+ | D | compatível |
| `libvulkan.so.1`, Mesa (RADV, NVK, llvmpipe), driver NVIDIA | Apache-2.0; MIT; proprietária (NVIDIA, do sistema do usuário) | D | compatível (o driver da NVIDIA é do sistema do usuário, opcional: o RADV e o NVK são livres) |
| libwayland, libdecor, libX11/xcb, PipeWire, libpulse, ALSA, libudev, D-Bus, libusb (pelo SDL) | MIT; LGPL-2.1; AFL/GPL (D-Bus) | D | compatível |
| Portal de arquivos do desktop (xdg-desktop-portal) ou zenity, pelos diálogos do SDL3 no instalador do N9.1 | LGPL-2.1+; LGPL-2.1+ | D (processo externo, só em execução) | compatível |

### 3.5 Ferramentas externas opcionais e mods

| Componente | Finalidade | Licença | Integ. | Redistr. | Patentes e distribuição | Situação | Alternativa |
|---|---|---|---|---|---|---|---|
| `ffmpeg` e `ffprobe` do sistema (Linux) | mods de filme que não são MPEG-1 | LGPL-2.1+ ou GPL, conforme a compilação | P | não | H.264, HEVC e AAC dependem da compilação da distribuição: o Fedora e derivados podem não trazer H.264 (o roteiro do Bazzite registra os decodificadores) | compatível | — |
| Codecs de mods no Windows | idem | do Windows | D | não | **HEVC pode exigir um pacote opcional pago** (extensão da Microsoft Store) e não está garantido em toda instalação do Windows; o `docs/MODS-VIDEO.md` cita H.265 como aceito | **ajustes** (documentação dos mods) | recomendar formatos livres de taxa ao usuário final; proposta S4 |
| Mods de áudio (OGG Vorbis, FLAC, WAV) | dublagens e músicas | (do autor do mod) | E (decodificadores acima) | não | formatos livres de royalties | compatível | — |
| Legendas transcritas e traduzidas (`mods-padrao`, locais) | legendas dos filmes | texto derivado do roteiro do jogo | T | **não** (fora do git) | direitos do texto original | compatível enquanto local; **nunca publicar** | — |
| Ícone a partir da capa do usuário (`icone-local`) | ícone local | arte do jogo | T | **não** | direitos da arte | compatível enquanto local | — |

### 3.6 Só desenvolvimento (nem compilação do usuário nem distribuição)

Python e bibliotecas dos scripts; Capstone (BSD-3, comparação opcional); Flycast (GPL-2.0, **só como referência de
comportamento**, processo externo; nenhum código copiado); Vulkan SDK e camadas de validação (Apache-2.0); RenderDoc (MIT);
llvm-mingw isolado (piloto); WSL; Codex CLI (revisor). Ficam fora da política de distribuição, mas não podem virar dependência
do produto.

## 4. Resultado

**Incompatíveis com a política hoje (exigem substituição):** nenhum componente distribuído pede pagamento, royalties ou
licença comercial. O caso que **pode** violar a política é o **MSVC com o Windows SDK**: o usuário do Windows precisa baixar e
aceitar os termos proprietários da Microsoft para compilar o jogo, e o projeto copia o runtime do MSVC no Builder. A análise dos
termos atuais fica pendente; a solução já estudada é o llvm-mingw (S1).

**Compatíveis que só pedem documentação ou atribuição:** cabeçalhos do kernel (GPL-2.0 com a syscall-note), glibc (LGPL-2.1),
HIDAPI, yuv2rgb e libm do SDL3, FreeType (crédito), componentes do CMake (jsoncpp, bzip2, cppdap, OpenSSL 1.1.1v), macOS,
mingw-w64 e wasi-libc (ou a poda), documentação dos codecs de mods.

**Dependem de análise adicional:** MSVC e o runtime; as 13 fontes da glibc sem a exceção de ligação; os avisos de terceiros do
LLVM e do SDL3 por alvo; o H.264 e o AAC do Media Foundation (risco baixo, o projeto não distribui codec); marcas no nome do
projeto e dos executáveis.

## 5. Propostas (nenhuma iniciada; cada uma espera aprovação)

| | Proposta | Motivo | Impacto | Testes e evidência |
|---|---|---|---|---|
| S1 | **llvm-mingw no Windows** no lugar do MSVC (opção B de `DISTRIBUICAO-COMPILADOR.md`) | tira a única dependência proprietária de compilação e o runtime do MSVC do pacote | grande: build Windows com Clang, ponto flutuante (`-ffp-contract=off`), Builder; ~150–250 MB no pacote Windows | mesmo C++ gerado → mesmos quadros e estado por passo do MSVC (`compara_modos.py` nas rotas do `test_assistido`), `test_saves`, `n5_e001`, N8.0 e N8.7 com o novo exe, C8, revisão do Codex |
| S2 | **Poda do Zig** e textos completos do toolchain | menos arquivos e obrigações (seção 6) | pequeno no código; novo pacote Linux | seção 6 |
| S3 | **Filmes do disco pelo pl_mpeg também no Windows** (como no Linux), sem a conversão para H.264/AAC | tira o H.264 e o AAC do caminho do Original, a conversão com perda, o Media Feature Pack e a diferença entre plataformas | médio: o vídeo do Windows (N5) e o Builder | 16 filmes (`n5_matriz`, fim e sincronia), N8.0 e N8.7 nos filmes, legendas, A3 do Windows, revisão do Codex. **Opcional:** a política não o exige |
| S4 | **Formato de referência dos mods sem taxa**: documentar MPEG-1 (toca sem nada instalado nos dois sistemas) e avisar que HEVC no Windows e H.264 em algumas distribuições Linux dependem de codecs do sistema | o usuário de mods não depende de codec pago | só documentação agora; formatos novos ficam para o N10 | — |
| S5 | CMake com OpenSSL 3, se a versão oficial nova trouxer (conferir com `strings`), ou só os textos | OpenSSL 1.1.1 sem suporte | pequeno; novo pacote | suíte B1–B14 com o pacote novo |

## 6. Plano de redução do Zig (S2)

**Medido em 2026-11-03** numa cópia do Zig do pacote `l5zig6` (`~/bs-linux/zigpoda-teste`, WSL), sem tocar no pacote:

| | Original | Podado | Redução |
|---|---|---|---|
| Arquivos | 15 373 | 4 176 | −73 % |
| Tamanho | 309 MB | 209 MB | −100 MB (−32 %) |
| `tar.gz` só do Zig | 74,4 MB | 60,2 MB | −14,2 MB (−19 %) |
| Pacote inteiro (estimado) | 90,9 MB | ~76,7 MB | −16 % |

O que sai: `lib/libc/include/` de todos os alvos menos `generic-glibc`, `x86_64-linux-gnu`, `x86-linux-any` e
`any-linux-any` (sai o `any-windows-any`, 73 MB, e o `any-macos-any`); `lib/libc/{mingw,wasi,darwin,musl}`; `lib/tsan`;
`lib/docs`; `lib/init`. O binário `zig` (158 MB, com o LLVM) fica igual.

**O que não muda:** os 979 cabeçalhos do kernel e os da glibc continuam no pacote, então os textos da GPL-2.0, da
Linux-syscall-note e da LGPL-2.1 continuam obrigatórios. Saem as obrigações do macOS (APSL), do mingw-w64, do wasi-libc e do
musl. Apagar arquivos não apaga obrigações dos que ficam.

**Prova mínima já feita na cópia:** `zig c++ -target x86_64-linux-gnu.2.31` com libc++, `std::thread` e `std::filesystem`, e
`zig cc` com `libm`, com cache vazio: compilam e rodam. Sem `lib/compiler` também compila (mais 3,9 MB), mas fica dentro por
precaução (`zig ar`, `objcopy`).

**Testes para substituir o Zig do pacote** (depois da aprovação):
1. `toolchain_linux.sh` com a poda por lista fixa e o inventário do que saiu no `VERSAO.txt` (a D-L2 passa a "oficial podado").
2. `SDL3` pré-compilado de novo com o Zig podado, e o mesmo `libSDL3.a` ou a diferença explicada.
3. Suíte `test_builder_linux.py` (B1–B14) com o pacote novo, com cache vazio.
4. **Identidade do jogo:** o exe do pacote podado igual byte a byte ao do `l5zig6` (mesmo C++ gerado e mesmo compilador); se for
   igual, a A3 (N8.0 e N8.7) não se repete. Se diferir, explicar e repetir a A3.
5. Revisão do Codex do script de poda e da lista de licenças.

## 7. Textos e avisos exigidos (lista para o pacote da L8)

| Texto ou aviso | De onde vem | Para onde vai | No pacote v0.8.0 |
|---|---|---|---|
| GPL-2.0 e Linux-syscall-note | `LICENSES/` do kernel, tag v6.3 (cabeçalhos 6.3.8 no Zig 0.13) | `licencas/compilador/` (Linux) | **sim** |
| LGPL-2.1 (glibc) | `COPYING.LIB` da tag `glibc-2.38` | `licencas/compilador/` (Linux) | **sim**, com o `LICENSES` |
| HIDAPI (opção BSD) | SDL `src/hidapi/LICENSE-bsd.txt` e `LICENSE.txt` | `licencas/` dos dois Builders | **sim**; na pasta do jogo instalado, **não** (pendência, abaixo) |
| yuv2rgb (BSD-3) | SDL `src/video/yuv2rgb/LICENSE` | idem | idem |
| fdlibm (Sun) e demais avisos do SDL3 | `SDL3-avisos-terceiros.txt` (`tools/dist/sdl3_avisos.py`: 49 blocos, 140 arquivos, todos os alvos) | idem | idem |
| Crédito da FreeType | FTL | LEIA-ME do Builder e do jogo (Linux), `AVISOS.txt` | **sim** |
| jsoncpp, bzip2, cppdap, OpenSSL 1.1.1v (CMake) | `Utilities/` da tag v3.30.5 do CMake e `LICENSE` da `OpenSSL_1_1_1v` | `licencas/compilador/` (Linux) | **sim**, com os avisos de reconhecimento no `AVISOS.txt` |
| APSL-2.0 (cabeçalhos do macOS); musl, mingw-w64, wasi-libc | `APPLE_LICENSE` do xnu; textos do Zig | `licencas/compilador/` (Linux) | **sim** (a poda da S2 fica para depois) |
| `NOTICE` do LLVM | nenhum arquivo `NOTICE*` no Zig 0.13 (busca de 2026-11-04) | — | não se aplica |
| Procedência | URL e SHA-256 de Zig, CMake, Ninja e dos textos; commit do SDL3 | `AVISOS.txt`, `compilador/PROCEDENCIA.txt`, `VERSAO.txt` | **sim** |

Texto histórico (2026-11-03): os textos não tinham sido baixados; entraram em 2026-11-04 (seção 9).

**Pendência para a v0.8.1 ou a L8:** o Builder copia para a pasta `licencas/` do jogo instalado só as licenças principais
(`extra_licenses()` em `builder/builder_platform_*.cpp`). Os avisos do SDL3 (HIDAPI, yuv2rgb, fdlibm e outros) ficam no pacote do
Builder. O jogo é gerado na máquina do usuário e não é redistribuído pelo projeto, então a obrigação de distribuição é atendida
pelo pacote; o LEIA-ME do jogo aponta onde estão os avisos. A cópia na instalação exige mudar o Builder (C++) e refazer as suítes,
e fica fora desta release (revisão Codex 20261104-103026, achado ALTA aceito em parte).

## 8. O que não basta

- **Não ligar dinamicamente com GPL não elimina obrigações.** Os cabeçalhos do kernel são *distribuídos* dentro do Zig, e a
  GPL-2.0 pede que o texto da licença acompanhe a cópia, mesmo sem nada ser ligado ao jogo. A nota de syscall trata do uso
  dos cabeçalhos por programas, não da redistribuição dos próprios cabeçalhos.
- **Redistribuir o Zig sem modificação não transfere as obrigações ao projeto Zig.** Quem entrega o `.tar.gz` é quem distribui.
- **O texto dentro de `zig/lib/...` pode não bastar** onde a licença pede o aviso "na documentação ou em outros materiais
  fornecidos com a distribuição": a pasta `licencas/` deve reunir tudo.
- **Podar não elimina as obrigações dos arquivos que ficam** (seção 6).
- **Código aberto não é garantia de liberdade de patentes**: os codecs (H.264, HEVC, AAC) ficam fora do que o projeto
  distribui; a escolha de formatos para mods precisa disso documentado.

O pacote que vai para as sessões de validação (o `l5zig6`) não é publicado e não muda; as correções entram no pacote da L8.

## 9. Gate da v0.8.0 Preview (2026-11-04)

O mantenedor autorizou a publicação experimental da v0.8.0 Preview (pacotes Windows e Linux para testes em hardware real),
com a auditoria resolvida antes. Nenhum componente funcional foi trocado; só textos, avisos e documentação entraram.

**Bloqueadores (pagamento, royalties, licença comercial obrigatória, proibição de redistribuir):** nenhum.

| Item | Decisão para a v0.8.0 | Situação |
|---|---|---|
| MSVC e Windows SDK (S1) | Não são distribuídos. O usuário instala o Build Tools e aceita os termos da Microsoft. Para uso individual é gratuito (licença Community, ou os termos do Build Tools para código aberto de 2022). Organizações conferem os termos. Documentado no `LEIA-ME` e no `AVISOS.txt`. S1 (llvm-mingw) continua proposta. | **ressalva documentada**, não bloqueia |
| Runtime do MSVC no Builder | "Distributable Code", redistribuído sem modificação, só com os programas do Builder. | compatível |
| Cabeçalhos do kernel no Zig | `GPL-2.0.txt` e `Linux-syscall-note.txt` da tag v6.3 (cabeçalhos 6.3.8) em `licencas/compilador/`. | resolvido |
| glibc no Zig | `glibc-COPYING.LIB.txt` (LGPL-2.1, tag `glibc-2.38`) e o `glibc-LICENSES.txt`. As partes estáticas no `bs-builder` (crt, `libc_nonshared`): pela seção 6 da LGPL-2.1, as fontes delas estão no pacote, e as do `bs-builder` na tag, com o script e o compilador para religar (`AVISOS.txt`). Isso vale também para as 13 fontes sem a exceção de ligação. | resolvido |
| Cabeçalhos do macOS (405 com APSL) | `APSL-2.0.txt`. Não são usados; a poda (S2) fica para depois. | resolvido |
| `lib/tsan` | 259 arquivos com SPDX Apache-2.0 com exceção LLVM, cobertos pelo texto do LLVM; nenhum com APSL. | compatível |
| CMake: jsoncpp, bzip2, cppdap, OpenSSL 1.1.1v (S5) | Textos da tag v3.30.5 e da `OpenSSL_1_1_1v`, com os dois avisos de reconhecimento da OpenSSL no `AVISOS.txt`. O CMake não muda (a S5 com OpenSSL 3 fica para depois). | resolvido; OpenSSL 1.1.1 sem suporte (o Builder não usa a rede do CMake) |
| SDL3: HIDAPI, yuv2rgb, fdlibm e outros | `SDL3-avisos-terceiros.txt` (49 blocos, 140 arquivos), gerado por `tools/dist/sdl3_avisos.py`, conferido contra as fontes do kit a cada montagem do Windows; no Linux a montagem confere só a versão declarada do pré-compilado (3.2.30), e a conferência contra as fontes de onde ele foi compilado (`deps/sdl3`, commit `f5e5f658`, `release-3.2.30`) foi feita à mão em 2026-11-04 (pendência: registrar o commit no pré-compilado e conferi-lo na montagem); `HIDAPI-LICENSE*.txt` (opção BSD) e `yuv2rgb-BSD-3.txt`. | resolvido |
| FreeType | Crédito da FTL no `LEIA-ME` do Builder Linux, no do jogo e no `AVISOS.txt`. | resolvido |
| Codecs de mods (S4) | Aviso em `docs/MODS-VIDEO.md` e no `AVISOS.txt`: HEVC pode exigir extensão paga no Windows; H.264 depende do `ffmpeg` em Fedora e Bazzite; MPEG-1, OGG, FLAC e WAV sem taxa. | resolvido (documentação) |
| S3 (pl_mpeg no Windows) | Opcional; a política não o exige. | não iniciado |
| Marcas | Uso nominativo e aviso de não afiliação no `AVISOS.txt`. | ressalva documentada |
| Jogo gerado na máquina do usuário | Não é distribuído pelo projeto; recebe as licenças principais na pasta `licencas/`. Os textos completos ficam no pacote do Builder. | compatível |

Procedência dos textos acrescentados: `third_party/licencas/compilador/PROCEDENCIA.txt` (URL e SHA-256). Este registro não é
parecer jurídico: as obrigações conhecidas estão cumpridas e as ressalvas (MSVC, marcas, OpenSSL 1.1.1 sem suporte) estão
documentadas.

## 10. N9.1: instalador (2026-11-04)

O instalador e o desinstalador do N9.1 não trazem nenhum componente de terceiros novo. No Windows eles usam só
componentes do sistema (seção 3.3). No Linux usam o SDL3, o FreeType e a Noto Sans que já estavam no pacote (seção 3.1),
além dos diálogos do desktop em execução (seção 3.4). A arte é do projeto, em geometria, sob MIT: `docs/N9-1-ARTE.md`.
O pacote do Builder ganha só executáveis do projeto (`BlueStinger-Desinstalar`).

## 11. N9.2: interface da Remastered (2026-11-04)

Decisão do mantenedor (2026-11-04): o texto da interface nativa da Remastered usa o FreeType e a Noto Sans nos dois sistemas.
- **Componentes:** nenhum componente novo no projeto. O FreeType 2.13.3 (FTL) e a Noto Sans 2.015 (OFL 1.1), que já iam no
  pacote do Linux, passam a ir também no do Windows.
- **Kit do Windows:** `third_party/freetype` (as fontes, sem os testes e a CI) e `third_party/fonts`.
- **Pacote do Builder:** `licencas/FreeType-LICENSE.txt`, `FreeType-FTL.txt` e `NotoSans-OFL-1.1.txt`.
- **Jogo instalado:** as mesmas licenças entram pelo `extra_licenses` do Windows.
- **Crédito da FTL:** já está nos avisos.
- **Fontes:** no Windows elas entram no executável como vetores C++ gerados na compilação (`native/text/fonts_embed.cmake`),
  sem modificação.
- **Arte:** é a do projeto, em geometria, sob MIT (`docs/N9-1-ARTE.md`, `native/ui/identidade.h`). A tela de título por trás
  da interface é a do próprio jogo do jogador e não é distribuída.
