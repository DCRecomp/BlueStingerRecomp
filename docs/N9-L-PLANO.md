# N9-L: suporte nativo a Linux

**Situação (2026-11-03):** plano aprovado em 2026-11-02 com D-L1 a D-L5 como recomendados (a D-L3 ajustada em 2026-11-03, abaixo).
L0 e L1 feitas. L2 feita: A3 16/16 e 16/16 no Linux (WSL), com a ressalva dos filmes do `STATUS.md`, aceita pelo mantenedor em
2026-11-03. L3 implementada e provada no WSL (áudio SDL3, raster paralelo, Windows sem regressão); falta a porta de saída em Ubuntu
nativo. L4 implementada e provada no WSL e no Windows (seção 6.1); a parte que depende de GPU, áudio e controles reais vai junto
com a da L3 na mesma sessão em Ubuntu nativo. L5 implementada e provada no WSL e no Windows (seção 6.2), com o compilador portátil
da D-L2 (Zig 0.13, escolha do mantenedor em 2026-11-03); A1 e A2 em Ubuntu nativo vão na mesma sessão da L3 e da L4
(`docs/N9-L-ROTEIRO-UBUNTU.md`). L6 em diante não iniciadas. Detalhes e evidências no `STATUS.md`.

N9-L é um marco intermediário provisório, sem alterar a numeração N0–N10. Vem depois do N9.0 e antes do N9.1 e do N9.2, que ficam
bloqueados até a portabilidade ser validada. O alvo inicial é Linux x86-64, Ubuntu e SteamOS, sem Wine nem Proton.

Continuam valendo: AOT mantido, N10 separado e não autorizado, campanha completa no gate pós-N9, ressalvas do N8, bloqueio do
N9.5–N9.8 com o N7 aberto, fidelidade da Original e recursos substituíveis para mods, dublagem, músicas e vídeos.

## 1. Auditoria de portabilidade

**Método:** leitura do código e `g++ 13.3 -std=c++20 -fsyntax-only` no Ubuntu 24.04 do WSL, sobre todo o código do jogo, sem mudar
nenhum arquivo. Também uma compilação real de uma unidade do código gerado.

| Medida | Resultado |
|---|---|
| Arquivos do jogo verificados (`app`, `native`, `runtime`, `transitional`, `tools/extractor`) | 151 |
| Passam sem mudança | 134 |
| Dependência real de Windows ou MSVC | 13 |
| Inconclusivos, porque o `known_versions.h` particular não compila | 4 |
| Código gerado | Compila com `g++ -O2`: 16 s e 530 MB por unidade, 12 unidades |

A prova é só de sintaxe e de uma unidade gerada. Nada foi ligado nem executado no Linux ainda.

Os 13 arquivos:

| Arquivo | Dependência | Peso |
|---|---|---|
| `runtime/cpu/sh4_threads.cpp` | fibras do Win32 | alto |
| `native/video/mf_movie.cpp` | Media Foundation | alto |
| `native/video/subtitles.cpp` | DirectWrite, Direct2D, WIC | médio |
| `app/menu_overlay.cpp` | DirectWrite, Direct2D, fonte Segoe UI | médio |
| `native/audio/audio_output.cpp` | XAudio2, COM | médio |
| `native/platform/saves_store.cpp` | `ReplaceFileW`, escrita atômica | médio |
| `app/main_sdl.cpp` | argumentos, caminhos, pastas, caixas de mensagem, 101 usos de `wchar_t` | médio |
| `native/platform/input_system.cpp`, `runtime/input/input.cpp` | XInput de reserva | baixo: o SDL3 já é a entrada principal |
| `native/platform/data_files.cpp`, `native/video/movie_catalog.cpp`, `native/localization/mod_packages.cpp` | `_fseeki64` e literais `wchar_t` | baixo |
| `runtime/debug/cri_diag.cpp` | `fopen_s`, só no build de validação | baixo |

Três pontos passam na sintaxe, mas não funcionam no Linux:

| Ponto | Problema |
|---|---|
| `transitional/cri_idle.cpp` | O rastreamento de escrita só existe no Windows, e o `--tempo experimental` é o **padrão na janela**. Sem ele, o jogo recusa abrir nesse modo. O `docs/DECISAO-VULKAN-LINUX.md` tratava isso como opcional, e não é |
| `native/render/soft/soft_raster.cpp` | `std::execution::par` sem TBB pode rodar em sequência, em silêncio. Reduz o desempenho, mas não muda o resultado: a ordenação dos fragmentos é total |
| Caminhos | `\` em literais e nomes com maiúsculas diferentes funcionam no Windows e falham num sistema de arquivos que diferencia maiúsculas. Afeta `mods`, `data\video`, pacotes de legendas |

Fora do jogo:

| Parte | Situação |
|---|---|
| Builder | Núcleo quase portável (9 pontos em `builder_core.cpp`). `toolchain.cpp` e `child_process.cpp` precisam de versão Linux. As janelas Win32 não se aproveitam |
| Conversão de filmes | O codificador é o Media Foundation (`MfMovieWriter`). Demux e ADX já são portáveis |
| CMake | `d2d1`, `dwrite`, `xaudio2`, `xinput`, `mfplat`, `/bigobj` e o teste `xmm_probe.asm` (MASM) sem condição de plataforma |
| Testes | 31 suítes em PowerShell e 56 scripts Python, dos quais 22 assumem `.exe` ou MSVC |

## 2. Bloqueadores

| # | Bloqueador | Por que bloqueia | Caminho |
|---|---|---|---|
| B1 | Troca de contexto | O modelo de threads do jogo depende das fibras | Troca própria em assembly GAS (seção 3) |
| B2 | Rastreamento de escrita | Tempo padrão da janela | `mprotect` com `SIGSEGV` |
| B3 | Filmes | Sem Media Foundation não há nem conversão nem reprodução | D-L3 |
| B4 | Compilador no SteamOS | O jogo é compilado na máquina do usuário, e o SteamOS não traz compilador e tem sistema somente leitura | D-L2 |
| B5 | Texto | Menu e legendas dependem do DirectWrite e da Segoe UI | FreeType com fonte livre no pacote |
| B6 | Fidelidade entre compiladores | A Original precisa continuar idêntica, e GCC ou Clang otimizam de outra forma | Opções fixas e as 16 referências no Linux (seção 5) |
| B7 | **Ambiente de validação** | WSL2 não é Linux nativo: kernel próprio, GPU por `/dev/dxg`, sem controle físico. Só serve para compilar e para testes sem janela. Daqui não consigo testar Ubuntu real nem SteamOS | D-L1, depende do mantenedor |
| B8 | RADV e controles do Deck | O Vulkan nunca rodou no driver do Steam Deck nem sob gamescope | Validação na L6 |

## 3. Arquitetura proposta

O código compartilhado não ganha `#ifdef`. Cada dependência fica num `.cpp` de backend, atrás de interface, seguindo a regra de
`docs/DECISAO-VULKAN-LINUX.md`.

| Peça | Windows, sem mudança | Linux |
|---|---|---|
| Contexto das threads do jogo | Fibras do Win32 | `sh4_context_switch` em GAS: registradores preservados, MXCSR e palavra de controle x87, pilha de 8 MB com página de guarda |
| Memória observada | `MEM_WRITE_WATCH` | `mprotect` + `SIGSEGV` com pilha alternativa, por página. Uma interface com a mesma semântica |
| Saída de áudio | `IAudioOutput` com XAudio2 | `IAudioOutput` com `SDL_AudioStream` |
| Decodificador de filme | `IMovieDecoder` com Media Foundation | `IMovieDecoder` escolhido pelo conteúdo: SFD e MPEG-PS com ADX pelo `pl_mpeg`; outros formatos (MP4 de mod) pelo `ffmpeg` do sistema como processo separado (D-L3) |
| Texto | `ISubtitleRasterizer` com DirectWrite | `ISubtitleRasterizer` e o menu sobre `native/text` (FreeType 2.13.3 e Noto Sans embutida) |
| Plataforma | `Win32` em `app/platform_win.cpp` | `app/platform_posix.cpp`: argumentos UTF-8, pasta do executável, XDG, caixas de mensagem pelo SDL |
| Escrita atômica | `ReplaceFileW` | `rename` + `fsync` do arquivo e da pasta, com a mesma semântica de `.bak` |
| Pastas | Documentos e `%LOCALAPPDATA%` | Saves em `$XDG_DATA_HOME/DC-Recomp/Blue Stinger/saves`, preferências em `$XDG_CONFIG_HOME`. Modo portátil igual |
| Janela, entrada, Vulkan | SDL3 e `vk_device` | Os mesmos, com X11, Wayland e o `dlopen` do carregador Vulkan |
| Build | MSVC com CMake e Ninja | Compilador da D-L2, CMake e Ninja, com presets próprios |
| Builder | Janelas Win32, `bs-builder` | Só `bs-builder` e um `instalar.sh`. A janela Linux fica fora desta etapa |

**Opções de compilação obrigatórias no Linux:** `-O2 -fno-strict-aliasing -fwrapv -ffp-contract=off`, sem `-march=native` e sem
`-ffast-math`. Sem elas, o compilador pode assumir o que o código do SH-4 não garante.

**Saves:** o mesmo `.vms`. Um save do Windows tem de abrir no Linux e o contrário.

**Mods:** `mods/video`, `mods/audio` e pacotes de legendas funcionam igual, com busca sem diferenciar maiúsculas.

## 4. Decisões para o mantenedor

| # | Decisão | Recomendação | Motivo |
|---|---|---|---|
| D-L1 | Onde validar | Uma máquina Ubuntu 24.04 nativa com GPU real e um Steam Deck, operados por você com um roteiro meu | Sem isso, os critérios A5 e A6 não têm como ser cumpridos. Máquina virtual com GPU dedicada serve para Ubuntu, mas não substitui o Deck |
| D-L2 | Compilador no Linux | Clang e LLD portáteis dentro do pacote, com um sysroot de glibc antiga e o SDL3 pré-compilado. **Implementação escolhida em 2026-11-03 (L5):** Zig 0.13.0, que é o Clang/LLD 18.1.6 com o alvo de glibc 2.31 e o libc++ estático embutidos, mais CMake 3.30.5 e Ninja 1.12.1 oficiais | Funciona no SteamOS sem tocar no sistema, e todos compilam com o mesmo compilador, o que torna a prova de fidelidade válida para todos. O pacote fica maior; tamanho a medir na L5 |
| D-L3 | Filmes no Linux | Tocar os SFD do disco em tempo real com `pl_mpeg` e o ADX que já temos, sem reconverter. `.mp4` de mods por `libav` do sistema carregada em tempo de execução, se existir; sem ela, toca o filme do disco e registra aviso. **Ajuste aprovado em 2026-11-03:** o MP4 de mod vai pelo **`ffmpeg` e `ffprobe` do sistema como processos separados**, não pela libav por `dlopen` (o jogo teria de ler estruturas da libav, cuja disposição muda entre as versões 59, 60 e 61; sem os cabeçalhos LGPL de cada uma, isso quebraria em silêncio). O resto da decisão fica | Sem codec patenteado no pacote, e a imagem é a original, sem perda. Os mods continuam substituíveis no Linux, mas MP4 depende de um programa do sistema. Alternativa: exigir um formato aberto nos mods, com `dav1d` |
| D-L4 | Ferramentas no WSL | Autorizar `cmake`, `ninja`, `clang` e bibliotecas de desenvolvimento do X11, Wayland, ALSA e PipeWire no Ubuntu do WSL | A regra do projeto proíbe instalar sem autorização. Sem isso, a L1 não anda |
| D-L5 | Forma do pacote | `.tar.gz` com o Builder, o kit e `instalar.sh`. Sem AppImage ou Flatpak nesta versão | É o mais simples, e o jogo é gerado na máquina do usuário, como no Windows |

Como no Windows, nenhum pacote traz o jogo compilado: o usuário gera o executável a partir do próprio CDI.

## 5. Critérios de aceitação

Todos são obrigatórios para a publicação. Nenhum se cumpre só por compilar.

| # | Critério | Prova |
|---|---|---|
| A1 | Instalação limpa | Pacote num Ubuntu 24.04 limpo e num SteamOS: do CDI ao jogo aberto, sem Wine, sem Proton, sem instalar pacotes do sistema |
| A2 | Dados | `--verify-data` rc=0; dados gerados no Linux iguais aos do Windows por SHA-256 |
| A3 | **Fidelidade** | As 16 referências do N8.0 (179 exceções, nenhuma nova) e as 16 do N8.7 no executável Linux: estado por passo, VBlanks, LOAD/SAVE, saves e quadros do modo fiel. Divergência é defeito a corrigir |
| A4 | Portão de campanha | C8 do `test_campanha` reproduzindo no Linux a sessão gravada no Windows, nas duas edições |
| A5 | Execução real em Ubuntu | Boot, NEW GAME até a primeira área, LOAD, SAVE, filme, legenda, menu, sessão de 30 min sem queda, em GPU real |
| A6 | Execução real em SteamOS | O mesmo do A5 no Deck, com os controles embutidos, em modo jogo e pelo atalho |
| A7 | Vulkan | RADV e pelo menos mais um driver, sem erro de validação; recuo para software funcionando |
| A8 | Áudio | Efeitos, falas, música e filme em PipeWire, sem cortes na sessão; troca de dispositivo sem queda |
| A9 | Controles | Teclado, controle Xbox e Deck; remapeamento por `controles.toml`; câmera Livre na Remastered |
| A10 | Saves | Atomicidade com falha simulada; `.vms` do Windows abre no Linux e o contrário |
| A11 | Mods | Vídeo, dublagem, música e legendas substituídos no Linux, com nomes em maiúsculas diferentes |
| A12 | Desempenho | Sem queda de quadros sustentada no Deck, com o número registrado |
| A13 | Windows sem regressão | `ctest`, referências 16/16 e 16/16, C8 do `test_campanha`, `test_preview`, `test_gameplay`, `test_builder`, `test_projeto`, `test_saves`, `test_edicao`, e comparação função a função mostrando que só mudou o que a portabilidade exige |
| A14 | Pacotes | Windows e Linux separados, com instruções; `varre_publicacao` limpo; nenhuma mídia proprietária, dado pessoal ou arquivo particular; `SHA256SUMS.txt` conferido |
| A15 | Revisão | Codex em cada subetapa relevante, com achados registrados no `STATUS.md` |

## 6. Plano incremental

Cada subetapa fecha com testes e, quando houver mudança no núcleo, com as referências do Windows. O Windows continua sendo o
programa de referência durante todo o N9-L.

| Etapa | Entrega | Porta de saída |
|---|---|---|
| **L0** Base | Presets e CMake por plataforma, opções da seção 3, `known_versions.h` contornado na compilação limpa | Windows idêntico; `ctest` |
| **L1** Núcleo sem janela | B1, B2, escrita atômica, plataforma POSIX. Executável `--headless` no WSL, com dados preparados no Windows | Boot até o gameplay por roteiro |
| **L2** **Prova de fidelidade** | Referências rodando no Linux | **A3.** Se falhar sem causa corrigível, paro aqui e apresento o problema, antes de investir no resto |
| **L3** Janela | SDL3, Vulkan, `IAudioOutput` com SDL, entrada | Sessão jogável em Ubuntu nativo; A7, A8, A9 parciais |
| **L4** Filmes e texto | B3, B5, caminhos e mods | Filmes, legendas e menu; A11 |
| **L5** Builder | `bs-builder` Linux, compilador da D-L2, dados, testes portados para Python | A1 e A2 em Ubuntu |
| **L6** SteamOS | Ajustes de RADV, gamescope, controles, atalho | A5, A6, A10, A12 |
| **L7** Regressão Windows | Bateria do A13, revisão Codex final | A13, A15 |
| **L8** Publicação | v0.8.0 com os dois pacotes, Preview/pré-lançamento, limitações e hashes | A14, depois da conferência de todos os critérios |

Paralelismo do raster por pool de threads próprio (sem TBB) entra na L3, com prova de que o quadro não muda.

### 6.1 L4: filmes e texto (resultado, 2026-11-03)

- **SFD do disco:** os 16 são "Sofdec Stream" + MPEG-1 Program Stream com vídeo MPEG-1 320x240 a 30 fps e áudio ADX estéreo a
  44,1 kHz, PTS de vídeo e áudio em 0. No Linux, a imagem (RGB24) e o áudio (PCM) dos 16 são idênticos bit a bit aos do Windows;
  quadros, amostras e fins batem com o manifesto do Builder. Contra o `ffmpeg` (decodificador independente): mesmo número de quadros
  e PSNR mínimo de 45,15 dB (o MPEG-1 não exige IDCT exata).
- **Fim do filme:** pelo manifesto, como no Windows. Sem ele, 3 filmes acabariam um passo (16,7 ms) antes: **o Builder do Linux (L5)
  tem de gravar os fins `fim_video_100ns`/`fim_audio_100ns` como o do Windows** (arredondados).
- **Texto:** FreeType e Noto Sans, com a geometria do Windows (tamanhos, margens, alinhamento, quebra, contorno, fundo, painel e áreas
  do mouse do menu). Medido contra o Windows: mesmo número de linhas, base e centro com até 1 px de diferença, largura 5 % a 11 %
  maior (a fonte). Sem kerning (HarfBuzz seria uma dependência nova) e sem CJK.
- **Mods (A11):** nomes sem diferenciar maiúsculas; MP4 pelo `ffmpeg` com os tempos do arquivo; sem decodificador, o filme original
  com aviso. Diferença conhecida: num MP4 cuja edit list corta o fim, o `ffmpeg` respeita o corte e o Media Foundation não.
- **Pendente em hardware real (sessão única com a L3, roteiro em `docs/N9-L-ROTEIRO-UBUNTU.md`):** filme com som e legenda em GPU e
  saída de áudio reais, sincronia de ouvido, menu com teclado, controle e mouse, mod MP4 com o `ffmpeg` do próprio Ubuntu.

### 6.2 L5: Builder e distribuição Linux (resultado, 2026-11-03)

- **Builder:** um fluxo e uma linha de comando para os dois sistemas. O que depende do sistema fica em
  `builder_platform_*`, `child_process_*` e `toolchain_*`. No Windows nada mudou: mensagens de uso com os mesmos bytes, linha do
  CMake, stamp, `VERSAO.txt`, limites de caminho e cache iguais, e a regressão completa aprovada. No Linux os processos sobem
  sem shell (`execve`), a árvore de processos é morta no fim, no prazo e no cancelamento, e a trava é por `flock`. O cache fica no
  XDG e o modo portátil continua sendo o `instalacao.toml`. Kit, compilador ou cache com `;`, `\`, `"` ou `$` são recusados, com o
  motivo: o CMake não aceita esses caracteres em caminhos.
- **Compilador (D-L2):** Zig 0.13.0 (Clang 18.1.6), alvo glibc 2.31, CPU x86-64 base, libc++ estática, mais CMake 3.30.5 e Ninja
  1.12.1. SHA-256 de cada download fixados no script. SDL3 3.2.30 pré-compilado com os backends por `dlopen` (sem D-Bus por
  enquanto). Opção `--compilador-sistema` para clang, cmake e ninja do sistema.
- **Filmes:** manifesto gravado direto dos SFD, com os fins arredondados como no Windows; os 16 iguais ao manifesto do Windows.
- **Pacote (D-L5):** `.tar.gz` reprodutível de 87 MB (405 MB extraído), com `instalar.sh` (sem root, troca atômica, recuperação de
  interrupção, arquivos do usuário preservados, `--verificar`), licenças, `VERSAO.txt` e `SHA256SUMS.txt`. Não traz jogo
  compilado, mídia nem arquivo particular.
- **Provas (WSL):**
  - Suíte B1–B14 com 108 ok e 0 falha, na árvore do commit.
  - A2: dados e C++ gerado iguais aos do Windows por SHA-256.
  - A3 com o executável que o Builder do pacote compilou com o Zig: N8.0 16/16 (só as 179 exceções) e N8.7 16/16, sem exceção.
  - Duas revisões do Codex (5 e 3 achados, todos tratados; detalhes no `STATUS.md`).
- **Pendente:**
  - A1 em Ubuntu nativo (roteiro em `docs/N9-L-ROTEIRO-UBUNTU.md`, sessão única com a L3 e a L4) e no SteamOS (L6).
  - **Revisão das licenças e avisos do Zig antes de publicar qualquer pacote**: cabeçalhos do kernel Linux (GPL-2.0 com a exceção
    de syscall) e bibliotecas C dos outros alvos. Uma alternativa é podar o Zig, que reduz o tamanho e as obrigações, mas exige
    nova decisão.
  - Projeto C++ editável (Developer) no Linux.

### 6.3 Sessão em Ubuntu nativo de L3, L4 e L5: preparação (2026-11-03)

Autorizada pelo mantenedor em 2026-11-03: preparar a validação conjunta, sem iniciar L6, L7 e L8 e sem publicar.

- **Roteiro:** `docs/N9-L-ROTEIRO-UBUNTU.md`, numa sessão única com o Ubuntu 24.04 rodando do pendrive, sem instalação e sem
  tocar nos discos internos. Os arquivos ficam num disco de troca USB, e o trabalho pesado num ext4 dentro de um arquivo, que
  não é partição.
- **Scripts em `tools/dist/linux/ubuntu/`:**
  - `preparar_pendrive.ps1` (Windows): o disco de troca, com as ferramentas tiradas do commit e a parte privada separada;
  - `preparar_sessao.sh`;
  - `a1_a2.sh`: A1, A2, troca de edição e preservação dos arquivos do usuário;
  - `jogar.sh`: log e bibliotecas carregadas, com aviso de `llvmpipe` e de recuo sem pedido;
  - `filmes.sh`: os 16 SFD pelo decodificador nativo no lugar do NEWGAME, e os mods do A11;
  - `anotar.sh`;
  - `coletar.sh`: somente leitura, `sudo` só com autorização, filtro de privacidade e pacote de evidências.
- **Licenças:** auditoria preliminar em `docs/N9-L-LICENCAS.md`. As pendências bloqueiam a publicação e não mudam o pacote da
  sessão.
- **Nada aprovado:** A1, A2 (no hardware), A5, A7, A8, A9 e A11 só passam a aprovados com as evidências da sessão.

## 7. Publicação da v0.8.0

Autorizada antecipadamente, condicionada a todos os critérios acima:

- Mesma versão e mesma GitHub Release para os dois sistemas, marcada como pré-lançamento.
- `BlueStinger-Builder-0.8.0-windows-x64.zip`, `BlueStinger-Builder-0.8.0-linux-x86_64.tar.gz` e `SHA256SUMS.txt`.
- Notas com as limitações conhecidas e as ressalvas do N8, inclusive H9 e D-N8-4, e sem declarar a campanha completa aprovada.
- Não depende do N9.1, do N9.2 nem da remasterização.

**Parada obrigatória:** qualquer bloqueador, falha não resolvida ou necessidade de mudar um critério interrompe a publicação até
decisão do mantenedor. Uma versão Linux que só compila não é publicada.

## 8. Riscos

| Risco | Efeito | Resposta |
|---|---|---|
| O estado diverge entre MSVC e Clang | Original deixa de ser idêntica no Linux | L2 detecta cedo; sanitizadores ajudam a achar comportamento indefinido, que também é defeito no Windows |
| Falhas de página do B2 pesam no desempenho | Queda de quadros no Deck | Medir na L1; alternativa `userfaultfd` |
| Tamanho do pacote com compilador | Download grande | Medido na L5: 87 MB o `.tar.gz`, 405 MB instalado (o Zig sozinho, 341 MB, por levar as bibliotecas C de todos os alvos). Pacote com compilador opcional ou Zig podado só depois de nova decisão |
| H9 aparecer no Linux | A falha intermitente segue sem causa conhecida | Registrar à parte; não vira critério novo sem decisão |
| Diferença visual do texto | FreeType não desenha igual ao DirectWrite | Aceitável: menu e legendas não entram nas referências. A fonte muda no Linux |
