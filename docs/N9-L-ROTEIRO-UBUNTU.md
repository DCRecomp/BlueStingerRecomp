# N9-L: sessão de validação em Ubuntu 24.04 nativo (L3 e L4 juntas)

Uma instalação e uma sessão para as partes da L3 e da L4 que o WSL não prova (B7): GPU, saída de áudio, controles e o `ffmpeg` do
próprio sistema. O Builder do Linux só vem na L5, então os dados vão prontos do Windows. Nada aqui altera o disco, os SFD ou o
Windows. Devolva os arquivos e as anotações listados no fim.

## 1. Preparar (uma vez)

```
sudo apt install clang lld cmake ninja-build git pkg-config ffmpeg vulkan-tools mesa-vulkan-drivers \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev \
    libdecor-0-dev libpipewire-0.3-dev libpulse-dev libasound2-dev libudev-dev libdbus-1-dev libvulkan-dev
git clone <repositório> ~/bs-repo && cd ~/bs-repo && git checkout <commit da L4>
```

Copiar do Windows (pendrive), sem passar pelo git:

| Do Windows | Para o Ubuntu |
|---|---|
| `generated\` do repositório | `~/bs-repo/generated/` |
| `dist\BlueStinger-Preview\data\disc\`, `disc.manifest`, `disc_meta.bin`, `disc_meta.idx` | `~/bs-data/` (mesma estrutura) |
| `dist\BlueStinger-Preview\data\video\manifest.toml` (os `.mp4` não são usados no Linux) | `~/bs-data/video/` |
| `dist\BlueStinger-Preview\mods\` (legendas) | `~/bs-inst/mods/` |
| `%LOCALAPPDATA%\BlueStingerRecomp\n9-l\L4-provas\sfd_ref_windows.tsv` | `~/sfd_ref_windows.tsv` |
| `dist\BlueStinger-Preview\data\video\E017.mp4` (só para o teste de mod; arquivo do seu disco, não publicar) | `~/E017.mp4` |

## 2. Compilar e testar sem janela

```
tools/dev/linux_build.sh ~/bs-repo HEAD linux-debug
tools/dev/linux_build.sh ~/bs-repo HEAD linux-sdl
cd ~/bs-linux/<sha>/src
BS_DATA_DIR=~/bs-data BS_SFD_REF=~/sfd_ref_windows.tsv ctest --test-dir build/linux-debug    # tudo passa, inclusive sfd_disc_conteudo
ctest --test-dir build/linux-sdl -R audio_sdl
cp build/linux-sdl/app/bluestinger ~/bs-inst/BlueStinger
vulkaninfo --summary | grep -E 'deviceName|driverName'                                         # anotar GPU e driver
```

## 3. Sessão jogável

`cd ~/bs-inst && ./BlueStinger --data ~/bs-data 2>&1 | tee sessao.txt`

| # | Conferir | Critério |
|---|---|---|
| 1 | Abre pelo Vulkan da GPU real (o log diz o dispositivo); tela cheia e janela | A7 |
| 2 | Filme de abertura do NEW GAME: imagem, som e legenda juntos; a legenda some e volta pela opção do menu | A8, L4 |
| 3 | START no meio de um filme: pula e o jogo segue | L4 |
| 4 | NEW GAME até a primeira área; mover, câmera, LOAD e SAVE | A5, A9 |
| 5 | Menu interno (Esc): textos com acento legíveis, setas ◀ ▶, mouse acerta linhas e setas, confirmação de tela | L4, A9 |
| 6 | Teclado; controle Xbox (USB e, se houver, Bluetooth); uma tecla remapeada no `controles.toml` | A9 |
| 7 | Efeitos, falas e música sem cortes; trocar a saída de áudio com o jogo tocando: o som segue | A8 |
| 8 | Recuo para software: `BS_RENDER=software ./BlueStinger --data ~/bs-data` abre e joga | A7 |
| 9 | Mod de filme: `mkdir -p mods/video && cp ~/E017.mp4 mods/video/newgame.MP4`; o NEW GAME toca o E017 (log `(mod)`) | A11 |
| 10 | O mesmo sem o ffmpeg à vista: `PATH=/nao-existe ./BlueStinger --data ~/bs-data` toca o filme original e o log avisa `sem decodificador neste sistema` | A11 |
| 11 | 30 min sem queda (pode ficar para a L6) | A5 |

Ao terminar o item 10, apague `mods/video/newgame.MP4`.

## 4. Devolver

`sessao.txt` de cada execução, a saída do `vulkaninfo --summary`, o resultado dos dois `ctest` e as anotações da tabela (o que
passou, o que falhou e como).
