# N9-L: validação de L3, L4 e L5 em Ubuntu 24.04 nativo (sessão única)

**O que esta sessão prova:** o que o WSL não prova (B7 do plano): o pacote num Linux limpo, do CDI ao jogo (A1, A2), o
Vulkan na GPU real (A7), o áudio e os filmes no dispositivo real (A8), os controles físicos (A9), os mods de filme (A11) e a
sessão jogável (A5). **Nada está aprovado até as evidências desta sessão voltarem ao projeto.** As baterias já aprovadas no
WSL e no Windows (suíte B1–B14, A3 16/16 e 16/16, decodificação dos 16 SFD) não se repetem aqui, salvo a suíte opcional do
passo 6, que roda no hardware real.

**Regras da sessão:**
- O Ubuntu roda do pendrive ("Experimentar o Ubuntu"), **sem instalar** e sem tocar nos discos internos. Nenhuma partição,
  bootloader, configuração da BIOS ou arquivo do Windows é alterado. O CDI só é lido (o hash é conferido antes e depois).
- Tudo o que fica (arquivos, logs, evidências) vai para o disco de troca USB; o trabalho pesado vai para um arquivo ext4 dentro
  dele (`trabalho.img`), que não é partição.
- Os scripts são somente leitura, salvo onde dito. Todo `sudo` é mostrado e perguntado antes; os do roteiro você digita.
- `privado/` no disco de troca tem conteúdo do jogo e dados seus: **nunca** publicar nem anexar. As evidências não levam dados
  do jogo, saves (só hashes), MAC, número de série nem caminhos pessoais.
- Um item que não puder ser feito fica `nao-feito`, com o motivo. Nenhum critério muda sem decisão do mantenedor.

## 1. Requisitos

**Esta máquina:** Ryzen 7 9700X (GPU integrada AMD Radeon, RDNA2), RTX 5080, 31 GB de RAM, placa B650 AORUS ELITE AX,
áudio Realtek e HDMI, controle Xbox (Bluetooth LE; o cabo USB-C é o mais simples).

| Item | Mínimo | Para quê |
|---|---|---|
| Pendrive A | 8 GB, USB 3 | Ubuntu 24.04 (a gravação **apaga o pendrive**) |
| Disco de troca B | 64 GB livres, USB 3, **exFAT** (SSD externo de preferência; pendrive serve, mais lento) | 1,6 GB de cópia do Windows, `trabalho.img` de 20 GB, evidências |
| Rede | cabo ou Wi-Fi | `apt` das ferramentas (passo 5) |
| Cabo de vídeo | um monitor (ou uma segunda entrada dele) na **saída de vídeo da placa-mãe** | o RADV na GPU integrada (A7) sem depender do driver da RTX 5080 no Live |
| Controle | Xbox com o cabo USB-C; Bluetooth se quiser testar a reconexão sem fio | A9 |
| Tempo | ~5 h (A1/A2 15 min; suíte 40 min, opcional; filmes 65 min; mods 50 min nas duas rodadas, o NEWGAME original de 7,5 min toca inteiro em dois casos; jogo 30 min ou mais) | |
| Memória | o Live usa a RAM como disco do sistema: 31 GB sobram; o trabalho pesado vai para o `trabalho.img` | |

**Antes de começar, por precaução:** se o BitLocker estiver ligado no Windows, tenha a chave de recuperação à mão (a escolha de
boot pelo F12 não deveria pedi-la, mas algumas placas pedem).

## 2. No Windows (preparação, ~30 min)

1. Baixe a ISO **Ubuntu 24.04 Desktop**, a versão pontual mais recente (24.04.3 ou posterior: o kernel mais novo é o que
   reconhece a RTX 5080), de `ubuntu.com/download/desktop`, e o `SHA256SUMS` da mesma página. Confira:
   `Get-FileHash ubuntu-24.04.*-desktop-amd64.iso` contra o `SHA256SUMS`.
2. Grave a ISO no pendrive A com o **Rufus** (modo imagem ISO, GPT, UEFI) ou o **balenaEtcher**. Confira a letra e o nome do
   pendrive antes: a gravação apaga tudo nele. Não use Ventoy (exige registrar chave no Secure Boot).
3. Formate o disco B como **exFAT** se ele ainda não for (Explorador > botão direito > Formatar; apaga o disco B). Um disco
   exFAT ou NTFS que já tenha espaço serve sem formatar.
4. Com o disco B ligado (letra `X:`), no PowerShell, na pasta do repositório:

   ```
   powershell -ExecutionPolicy Bypass -File tools\dist\linux\ubuntu\preparar_pendrive.ps1 -Destino X:\
   ```

   Ele recusa disco que não seja USB e confere o pacote. Se já houver uma `X:\BS-TROCA`, ele para; com `-Substituir`, a
   anterior é renomeada (nunca apagada). Copia o pacote da L5 e as ferramentas (tiradas do commit) para
   `X:\BS-TROCA\publico`. Copia o CDI, os dados e o C++ gerado do Windows (A2), o `E017.mp4`, as legendas locais e a sessão
   gravada com o E016 e o E017 para `X:\BS-TROCA\privado`. Grava o `SHA256SUMS-TROCA.txt`. Fim esperado:
   `Pronto: X:\BS-TROCA (1.610 MB, 2166 arquivos conferidos)`.
5. Ligue um monitor na saída de vídeo da placa-mãe (pode ser o mesmo monitor numa segunda entrada). Deixe o controle e o
   cabo à mão.

## 3. Iniciar o Ubuntu pelo pendrive

1. Com o pendrive A e o disco B ligados, reinicie e aperte **F12** na tela da Gigabyte (menu de boot de uma vez; nada muda na
   BIOS). Escolha a entrada **UEFI** do pendrive A.
2. No GRUB do Ubuntu: **Try or Install Ubuntu**. Se a tela ficar preta, desligue, volte e use **Ubuntu (safe graphics)**: anote
   isso, conta para o A7. O Secure Boot pode continuar ligado.
3. No assistente: idioma, teclado, rede; escolha **Experimentar o Ubuntu** (nunca "Instalar"). Feche o instalador se abrir.
4. Abra o Terminal (Ctrl+Alt+T). Localize o disco de troca e guarde o caminho numa variável (o rótulo do seu disco no lugar de
   `<rotulo>`; `ls /media/$USER` mostra):

   ```
   ls /media/$USER
   T=/media/$USER/<rotulo>/BS-TROCA/publico/ferramentas
   bash $T/ubuntu/preparar_sessao.sh
   ```

   Ele confere os 2166 arquivos copiados (alguns minutos) e cria e monta o `trabalho.img` de 20 GB (pergunta antes; o udisks
   monta sem senha). Fim esperado: `Pronto. Pasta de trabalho: /media/<usuario>/BS-TRABALHO`.
5. Retrato do sistema **antes de instalar qualquer coisa** (somente leitura; `--privilegiado` acrescenta o log do kernel
   filtrado e pede autorização para o `sudo dmesg`):

   ```
   bash $T/ubuntu/coletar.sh inicio --privilegiado
   ```

## 4. A1 e A2: do pacote ao jogo, sem pacotes do sistema (15 min)

Ainda **sem** `apt install` nenhum:

```
bash $T/ubuntu/a1_a2.sh original
```

O script:
- confere o SHA-256 do pacote;
- extrai o pacote no `trabalho.img`, roda `instalar.sh --atalho` e `--verificar` (destino padrão
  `~/.local/share/BlueStinger-Builder`);
- roda o `bs-builder` do CDI até o jogo em `BS-TRABALHO/Jogos/Blue Stinger` (caminho com espaço);
- confere `--version`, `--verify-data`, `ldd` (nada de Wine ou Proton), o Zig do pacote no log do Builder, a edição que o
  jogo lê e o CDI sem mudança;
- faz o A2: os dados, o C++ gerado e os 16 filmes do manifesto têm de ser iguais aos do Windows.

Ele anota sozinho `A1` e `A2` em `evidencias/resultados.tsv`. Esperado: `bs-builder rc=0`, `--verify-data rc=0`,
`0 falhas` no A2.

Depois, a troca de edição (a mesma pasta; saves, mods e configurações têm de ficar iguais):

```
bash $T/ubuntu/a1_a2.sh remastered     # o jogo diz "[edicao] perfil remastered" e "[camera] modo livre"
bash $T/ubuntu/a1_a2.sh original       # volta; A1.preserva-arquivos-do-usuario = ok
```

## 5. Ferramentas (agora pode instalar)

```
sudo add-apt-repository -y universe
sudo apt update
sudo apt install -y vulkan-tools vulkan-validationlayers mesa-utils binutils
bash $T/ubuntu/coletar.sh sistema depois-ferramentas
vulkaninfo --summary | grep -E "deviceName|driverName|driverInfo|apiVersion"
```

Anote os nomes dos dispositivos: a GPU integrada aparece como `AMD Radeon Graphics (RADV …)`. A RTX 5080 só aparece se
o Live tiver um driver Vulkan para ela: o NVK (Mesa, sobre o nouveau) ou o da NVIDIA. O `llvmpipe` é a CPU e **não** conta
para o A7.

## 6. Suíte do Builder no hardware real (opcional, ~40 min)

```
python3 -I $T/test_builder_linux.py --pacote $T/../BlueStinger-Builder-*-linux-x86_64.tar.gz \
    --cdi "$T/../../privado/Blue Stinger.cdi" --saida /media/$USER/BS-TRABALHO/suite \
    --windows-data $T/../../privado/win-data --windows-generated $T/../../privado/win-generated \
    2>&1 | tee $T/../../evidencias/suite.txt
```

Esperado como no WSL: `108 ok, 0 falha(s)`. Ela usa uma HOME própria dentro de `--saida` e não toca na sua. Anote:
`bash $T/ubuntu/anotar.sh suite ok "108/0 no hardware"` (ou `falha` com os nomes).

## 7. A7: Vulkan na GPU real

Cada linha é uma execução com `bash $T/ubuntu/jogar.sh <rótulo> [VAR=valor] [-- argumentos]`. O script grava o log, o código
de saída, o dispositivo Vulkan que o jogo escolheu e as bibliotecas que o processo carregou (os backends de áudio e vídeo do
SDL aparecem ali; os `libvulkan_*` não provam nada, porque o carregador do Vulkan abre todos os drivers instalados: **a prova
do driver é o nome do dispositivo no log**, ligado ao driver pelo `vulkaninfo`). Também avisa se o Vulkan usado foi o
`llvmpipe` ou se houve recuo para o software sem pedido. Jogue alguns minutos em cada uma: título, NEW
GAME, primeira área, menu (Esc). Feche pelo menu ou pela janela.

| Item | Execução | Conferir |
|---|---|---|
| A7.1 RADV | `jogar.sh a7-radv BS_VULKAN_GPU="AMD Radeon"` | log `[apresentacao] Vulkan direta: AMD Radeon Graphics (RADV …)` (o `vulkaninfo` mostra `driverName = radv` para esse nome); cenários, personagens, fog e transparências corretos; sem tremor nem tela preta |
| A7.2 segundo driver | se o `vulkaninfo` mostrar a RTX 5080: `jogar.sh a7-nvidia BS_VULKAN_GPU=NVIDIA` | o mesmo, com o nome da RTX 5080 no log (`(NVK …)` no NVK; o `driverName` do `vulkaninfo` diz `nvk` ou `NVIDIA`). Se a RTX 5080 não aparecer no Vulkan do Live: `anotar.sh A7.2 nao-feito "<motivo>"` (veja a nota abaixo) |
| A7.3 validação | `jogar.sh a7-validacao BS_VULKAN_GPU="AMD Radeon" VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` | `grep -c "Validation Error" $T/../../evidencias/sessoes/a7-validacao.txt` = 0 |
| A7.4 janela e tela cheia | na execução A7.1: F11; menu > Gráficos: janela, tela cheia, sem bordas, resoluções | troca sem tela preta, imagem centrada, proporção certa |
| A7.5 Wayland e X11 | `jogar.sh a7-wayland SDL_VIDEO_DRIVER=wayland BS_VULKAN_GPU="AMD Radeon"` e `jogar.sh a7-x11 SDL_VIDEO_DRIVER=x11 BS_VULKAN_GPU="AMD Radeon"` | as duas abrem e jogam; `libwayland-client` numa, `libX11`/`xcb` na outra |
| A7.6 software pedido | `jogar.sh a7-software -- --render software` | log `[render] renderizador: software`; joga |
| A7.7 sem Vulkan: recuo explícito | `jogar.sh a7-sem-vulkan VK_ICD_FILENAMES=/nao-existe.json` | o log diz `Vulkan indisponivel (…)` e o jogo segue no software: recuo **com aviso**, nunca silencioso |
| A5 estabilidade | `jogar.sh a5-30min BS_VULKAN_GPU="AMD Radeon"`: NEW GAME, primeira área, SAVE, sair, abrir de novo, LOAD, 30 min de jogo | sem queda, sem travamento, sem erro de apresentação no log |

Depois de cada uma: `bash $T/ubuntu/anotar.sh <item> ok|falha|parcial "o que viu"`.

**Nota sobre a RTX 5080 no Live:** o driver da NVIDIA não vem no Live, e instalá-lo numa sessão sem instalação costuma exigir
trocar o módulo de vídeo em uso (e às vezes reiniciar, o que apaga o Live). Se o NVK não a mostrar, o A7.2 fica `nao-feito`
nesta sessão, com o motivo, e o segundo driver vai para a L6 (o RADV do Steam Deck é outro hardware) ou para uma sessão com o
Ubuntu instalado, que precisa de autorização sua. Não instale o driver da NVIDIA no Live sem decidir isso antes.

## 8. A8: áudio, filmes e legendas no dispositivo real

Instale as legendas locais no jogo (ficam em `mods/`; o jogo procura por nome sem diferença de maiúsculas):

```
J="/media/$USER/BS-TRABALHO/Jogos/Blue Stinger/BlueStinger"
cp -r $T/../../privado/legendas/* "$J/mods/"
```

| Item | Execução | Conferir |
|---|---|---|
| A8.1 rota com filmes | `jogar.sh a8-rota BS_VULKAN_GPU="AMD Radeon" BS_RELOGIO_FIXO=1700000000 BS_INPUT_SCRIPT=$T/roteiros/passo_newgame_area1.txt` (o roteiro joga sozinho) | NEWGAME e depois E001: imagem e som juntos, falas no tempo da boca, legenda no tempo da fala, nada cortado; a transição do filme para o jogo sem tela preta longa nem som sobrando |
| A8.2 START no filme | `jogar.sh a8-start BS_VULKAN_GPU="AMD Radeon"`: NEW GAME e START no meio do NEWGAME | o filme para (log `[video] parado pelo jogo`), **nenhum som do filme continua**, o jogo segue |
| A8.3 legendas e menu | na A8.2: menu (Esc) > Legendas: pt-BR, en, nenhuma | a legenda troca e some; textos com acento legíveis no menu |
| A8.4 E016 e E017 | `mkdir -p /media/$USER/BS-TRABALHO/saves-sessao && cp -r $T/../../privado/gravacoes/sessao-20261021-145006/saves_inicio/. /media/$USER/BS-TRABALHO/saves-sessao/` e `jogar.sh a8-sessao BS_VULKAN_GPU="AMD Radeon" BS_RELOGIO_FIXO=1700000000 BS_INPUT_SCRIPT=$T/../../privado/gravacoes/sessao-20261021-145006/entrada.txt -- --saves-pasta /media/$USER/BS-TRABALHO/saves-sessao --tempo nativo` | os dois filmes no meio do jogo, com som e imagem juntos; se a reprodução sair do caminho gravado, anote (não é critério desta sessão) |
| A8.5 os 16 filmes | `BS_VULKAN_GPU="AMD Radeon" bash $T/ubuntu/filmes.sh todos` (65 min: cada filme tem ~150 s de rota antes; pode assistir; `--perguntar` pergunta filme a filme; `filmes.sh um E134.SFD` repete um) | `16/16`: cada filme toca no lugar do NEWGAME pelo decodificador nativo e termina no tempo do manifesto (`evidencias/filmes/resumo.tsv`); veja e ouça pelo menos E017, E134, E185A, E200B e ENDING2 |
| A8.6 troca de saída | durante a A5 ou a A8.1, troque a saída de som nas Configurações do Ubuntu (alto-falante, fone, HDMI) | o som segue na nova saída, sem queda do jogo; no máximo um corte curto |
| A8.7 PipeWire e Pulse | `jogar.sh a8-pipewire SDL_AUDIO_DRIVER=pipewire` e `jogar.sh a8-pulse SDL_AUDIO_DRIVER=pulseaudio` (2 min cada, com música e efeitos) | as duas tocam; `libpipewire` numa, `libpulse` na outra |
| A8.8 sessão sem cortes | na A5 de 30 min | efeitos, falas e música sem estalos nem cortes |

A comparação automática de imagem e PCM do WSL (L4) **não** substitui estes itens: aqui o que conta é o que você vê e ouve.

## 9. A11: mods de filme

Antes de instalar o `ffmpeg`:

```
BS_VULKAN_GPU="AMD Radeon" bash $T/ubuntu/filmes.sh mods      # mod MP4 sem ffmpeg: toca o original e avisa; mod vazio; mod truncado
sudo apt install -y ffmpeg
BS_VULKAN_GPU="AMD Radeon" bash $T/ubuntu/filmes.sh mods      # agora também: o E017.mp4 toca pelo ffmpeg; newgame.mp4 em minúsculas
```

O script nunca toca num mod seu: se já houver `mods/video/NEWGAME.*`, ele para. As cópias que ele cria são apagadas no fim.
Mods de dublagem e música não vão nesta sessão (não há mod de teste); ficam com os testes automáticos da L4 e `nao-feito` aqui.

## 10. A9: controles

Com a execução `jogar.sh a9-controles BS_VULKAN_GPU="AMD Radeon"`:

| Item | Conferir |
|---|---|
| A9.1 teclado | menus, movimento, ações, START/pausa pelas teclas do `controles.toml` |
| A9.2 mouse | o menu interno: linhas e setas ◀ ▶ acertadas pelo clique; o cursor some no jogo e volta no menu |
| A9.3 controle por cabo | log `[controles] conectado: <nome> (GUID …)`. Anote o nome e o GUID: se o GUID termina em `68xx`, o SDL usa o HIDAPI; senão, o evdev do kernel. Menus, movimento, câmera, ações, START e pausa |
| A9.4 reconexão | tire o cabo no meio do jogo e ponha de novo: `desconectado` e `conectado` no log, o controle volta a funcionar sem reiniciar |
| A9.5 Bluetooth (se houver) | pareie nas Configurações > Bluetooth; o mesmo da A9.3; desligue e ligue o controle |
| A9.6 remapeamento | no `controles.toml` ao lado do jogo, troque uma tecla, abra de novo: a nova tecla vale. Desfaça depois |
| A9.7 câmera por edição | Original: câmera original. `a1_a2.sh remastered` e abra de novo: o direcional direito controla a câmera livre. Volte com `a1_a2.sh original` |

## 11. Pastas do usuário e modo portátil

| Item | Execução | Conferir |
|---|---|---|
| X.1 XDG dos saves | depois de um SAVE: `ls ~/.local/share/DC-Recomp/"Blue Stinger"/saves` | o cartão está ali (o jogo não grava saves em outro lugar) |
| X.2 configuração sem permissão na pasta do jogo | `chmod a-w "$J"`, `jogar.sh x2-config`, feche, `ls ~/.config/BlueStingerRecomp`, depois `chmod u+w "$J"` | `controles.toml` criado em `~/.config/BlueStingerRecomp` (o log vai para o terminal, porque o `jogar.sh` o captura) |
| X.3 portátil | `jogar.sh x3-portatil -- --saves-portatil` e um SAVE | o cartão vai para `"$J/saves"` |
| X.4 preservação | `a1_a2.sh original` de novo | `A1.preserva-arquivos-do-usuario` = ok |

## 12. Fechar a sessão

```
bash $T/ubuntu/coletar.sh final --privilegiado
```

Ele refaz o retrato do sistema, junta os hashes do Builder e do jogo, o `VERSAO.txt`, a edição, os logs e as pastas do
usuário (só nomes e hashes) e a matriz (`matriz.txt`, o último resultado de cada item). Procura dados pessoais e arquivos de
mídia, avisa ou recusa, e grava `BS-TROCA/evidencias-<data>.tar.gz`. Desligue pelo menu do Ubuntu, tire o pendrive A, ligue
o Windows e mande ao projeto **só** o `evidencias-<data>.tar.gz` (o `privado/` fica com você; apague-o quando quiser).

## 13. Matriz de critérios desta sessão

| Critério do plano | Itens | Fonte |
|---|---|---|
| A1 instalação limpa (Ubuntu; SteamOS fica na L6) | A1, A1 edição Remastered, A1.preserva-arquivos-do-usuario | `a1_a2.sh` |
| A2 dados | A2 | `a1_a2.sh` (e B3/B11 da suíte, opcional) |
| A5 execução real | A5, A8.1, A8.2 | `jogar.sh` |
| A7 Vulkan | A7.1–A7.7 | `jogar.sh`, `vulkaninfo` |
| A8 áudio e filmes | A8.1–A8.8 | `jogar.sh`, `filmes.sh todos` |
| A9 controles (Deck fica na L6) | A9.1–A9.7 | `jogar.sh` |
| A11 mods | A11.* de `filmes.sh mods`; legendas na A8.3; dublagem e música `nao-feito` | `filmes.sh mods` |
| Pastas e portátil (L5) | X.1–X.4 | `jogar.sh`, `a1_a2.sh` |
| A3, A4, A13 | não se repetem aqui (A3 16/16 e 16/16 no WSL; A4 e A13 são de outras etapas) | `STATUS.md` |

Um critério só passa a aprovado no `STATUS.md` depois que o projeto receber e conferir as evidências.
