# N9-L: validação de L3, L4 e L5 em Bazzite nativo (sessão única)

**Escolha do mantenedor (2026-11-03):** o Bazzite (Fedora Atomic) é o ambiente prioritário da validação nativa, no lugar da
sessão em Ubuntu. O roteiro do Ubuntu (`docs/N9-L-ROTEIRO-UBUNTU.md`) fica preservado como alternativa. **Nada está aprovado
até as evidências desta sessão voltarem ao projeto.** O Bazzite só passa a ambiente validado depois dela. **SteamOS não fica
validado por tabela:** é outro sistema, outro hardware (Steam Deck) e fica na L6.

**Ao vivo ou instalado: o que cada sessão prova.** Segundo a documentação do Bazzite:
- o ambiente ao vivo das ISOs é sempre a imagem base (`bazzite` ou `bazzite-gnome`), mesmo quando a ISO instala outra variante
  (README do `ublue-os/bazzite`, "ISOs use `bazzite` or `bazzite-gnome` as their live environment");
- ele roda com outro kernel, sem todo o suporte de hardware;
- a documentação pede para não jogar nele.

Por isso há dois modos:

| Modo | O que é | O que conta |
|---|---|---|
| **P, prévia ao vivo** (seções 2 a 13, sem instalar nada) | o ambiente do instalador do Bazzite | prévia de A1, A2, A7.1 (RADV), A8, A9, A11 e pastas, registrada como "Bazzite ao vivo (instalador), kernel X". **Não** declara o Bazzite validado. Sem o driver da NVIDIA: o A7.2 só roda se o NVK mostrar a RTX 5080 |
| **V, validação no Bazzite instalado** (seção 15) | `bazzite-nvidia-open` instalado num **SSD externo dedicado** | os mesmos itens, com o driver da NVIDIA (A7.2) e o kernel da imagem. **É o único modo que permite registrar o Bazzite como ambiente validado.** Exige a sua autorização: formata o SSD externo, cria uma entrada de boot na UEFI e pede o registro da chave MOK |

A prévia vale a pena: ela pega cedo o que não depende do kernel nem do driver, como o pacote, o SELinux, o `ffmpeg` sem
H.264, o PipeWire, os controles e o XDG. Se a prévia passar, o modo V repete só o que depende do sistema instalado (seção 15).

**O que esta sessão prova** (o que o WSL não prova, B7 do plano):
- A1 e A2: o pacote do CDI ao jogo, sem pacotes do sistema, e os mesmos dados e C++ do Windows.
- A5: a sessão jogável.
- A7: o Vulkan na GPU real (RADV na GPU integrada; na RTX 5080, o driver da NVIDIA da imagem instalada, só no modo V).
- A8: áudio, filmes e sincronia no dispositivo real (PipeWire).
- A9: teclado, mouse e controle físico.
- A11: mods de filme e o recuo quando não há decodificador.
- As edições Original e Remastered, os saves, as configurações e as pastas XDG.

As baterias já aprovadas (suíte B1–B14 108/0, A3 16/16 e 16/16, decodificação dos 16 SFD) não se repetem, salvo a suíte
opcional do passo 6.

**Regras da sessão:**
- **Nada é instalado no computador.** O Bazzite roda do pendrive no modo ao vivo, sem tocar nos discos internos. Nenhuma
  partição, bootloader, configuração da BIOS (Secure Boot incluído), entrada de boot da UEFI, chave MOK ou arquivo do Windows é
  alterado. O CDI só é lido: o hash é conferido antes e depois.
- **Nenhum pacote ou componente é instalado na imagem do sistema.** Nem `rpm-ostree install`, nem `rpm-ostree rebase`, nem
  `ujust`, nem driver: o sistema imutável fica como veio. O Builder é instalado só na pasta pessoal da sessão pelo
  `instalar.sh` (`~/.local/share/BlueStinger-Builder` e um link em `~/.local/bin`). No modo ao vivo essa pasta some ao
  desligar.
- Tudo o que fica (arquivos, logs, evidências) vai para o disco de troca USB. O trabalho pesado vai para um arquivo ext4 dentro
  dele (`trabalho.img`), que não é partição.
- Os scripts só leem, salvo onde dito. Todo `sudo` é mostrado e perguntado antes; os do roteiro você mesmo digita.
- `privado/` no disco de troca tem conteúdo do jogo e dados seus: **nunca** publicar nem anexar. As evidências não levam dados
  do jogo, saves (só hashes), MAC, número de série nem caminhos pessoais.
- Um item que não puder ser feito fica `nao-feito`, com o motivo. Nenhum critério muda sem decisão do mantenedor.

**Os scripts são os mesmos do Ubuntu**, em `publico/ferramentas/ubuntu/`; o nome da pasta ficou por histórico. Eles reconhecem
o Fedora e o Bazzite:
- o ponto de montagem `/run/media/<usuário>`;
- a pasta pessoal em `/var/home`;
- `rpm` e `rpm-ostree` no lugar do `dpkg`;
- o SELinux;
- o nome de máquina padrão `bazzite`, que também é o `ID` do sistema e não identifica ninguém.

## 1. Requisitos

**Esta máquina:**
- Ryzen 7 9700X, com GPU integrada AMD Radeon (RDNA2);
- RTX 5080 (Blackwell), 31 GB de RAM;
- placa B650 AORUS ELITE AX, áudio Realtek e HDMI;
- controle Xbox (Bluetooth LE; o cabo USB-C é o mais simples).

| Item | Mínimo | Para quê |
|---|---|---|
| Imagem | **Bazzite desktop (KDE) com NVIDIA "open"** (`bazzite-nvidia-open`): é a imagem para placas Turing e mais novas, RTX 5080 incluída. A imagem `-deck` (modo de jogo do Steam) não é a desta sessão | no modo V, o driver da NVIDIA e o Mesa (RADV). No modo P, o ambiente ao vivo é a imagem base, sem o driver da NVIDIA |
| Pendrive A | 16 GB, USB 3 | a ISO do Bazzite tem ~8 GB; a gravação **apaga o pendrive** |
| Disco de troca B | 64 GB livres, USB 3, **exFAT**; de preferência SSD externo | cópia do Windows (1,6 GB), `trabalho.img` de 20 GB, evidências |
| Rede | cabo ou Wi-Fi | só se for preciso baixar o Vulkan SDK (passo 5) |
| Cabos de vídeo | o monitor na **RTX 5080** e, numa segunda entrada dele, na **saída da placa-mãe** | a GPU integrada ativa para o RADV (A7.1) sem mexer na BIOS |
| Controle | Xbox com o cabo USB-C; Bluetooth se quiser testar sem fio | A9 |
| Tempo | ~5 h: A1/A2 15 min; suíte 40 min (opcional); filmes 65 min; mods 25–50 min; jogo 30 min ou mais | |
| Memória | o modo ao vivo usa a RAM como disco do sistema: 31 GB sobram. O trabalho pesado vai para o `trabalho.img` | |

**Antes de começar, por precaução:** se o BitLocker estiver ligado no Windows, tenha a chave de recuperação à mão. A escolha de
boot pelo F12 não deveria pedi-la, mas algumas placas pedem.

## 2. No Windows (preparação, ~40 min)

1. **Baixe a imagem certa.** Em `bazzite.gg`, escolha: desktop, GPU NVIDIA (RTX), KDE. Baixe a ISO `bazzite-nvidia-open` e o
   arquivo de soma (`.sha256` ou `CHECKSUM`) da mesma página. Confira com `Get-FileHash <iso>` contra a soma. Anote o nome
   exato e a data da ISO: a imagem ao vivo pode ser mais antiga que a versão instalável, e o projeto registra a que foi usada.
2. **Grave o pendrive A** com o **Fedora Media Writer** ou o **balenaEtcher** (o Rufus só em modo de imagem DD). Confira a letra
   e o nome do pendrive antes: a gravação apaga tudo nele. Não use Ventoy (exige registrar chave no Secure Boot).
3. Formate o disco B como **exFAT** se ele ainda não for (Explorador > botão direito > Formatar; apaga o disco B). Um disco
   exFAT ou NTFS que já tenha espaço serve sem formatar; o exFAT é o mais seguro no Fedora.
4. **Monte o disco de troca.** Com o disco B ligado (letra `X:`), no PowerShell, na pasta do repositório:

   ```
   powershell -ExecutionPolicy Bypass -File tools\dist\linux\ubuntu\preparar_pendrive.ps1 -Destino X:\
   ```

   O script:
   - para se as ferramentas tiverem mudança sem commit (as do Bazzite precisam estar num commit antes desta etapa);
   - recusa disco que não seja USB e confere o pacote;
   - não sobrescreve uma `X:\BS-TROCA` existente: para; com `-Substituir`, renomeia a anterior (nunca apaga);
   - copia o pacote da L5 e as ferramentas, tiradas do commit, para `X:\BS-TROCA\publico`;
   - copia para `X:\BS-TROCA\privado` o CDI, os dados e o C++ gerado do Windows (A2), o `E017.mp4`, as legendas locais e a
     sessão gravada com o E016 e o E017;
   - grava o `SHA256SUMS-TROCA.txt`.
5. Ligue o monitor nas duas saídas (RTX 5080 e placa-mãe), em entradas diferentes. Deixe o controle e o cabo à mão.

## 3. Iniciar o Bazzite pelo pendrive, sem instalar

1. Com o pendrive A e o disco B ligados, reinicie e aperte **F12** na tela da Gigabyte. É o menu de boot de uma vez: nada muda
   na BIOS. Escolha a entrada **UEFI** do pendrive A.
2. No menu do pendrive, escolha a opção de **iniciar o sistema ao vivo** (o nome exato depende da versão da ISO, como "Start
   Bazzite" ou "Try"). Se houver uma opção de gráficos básicos, use-a só se a tela ficar preta, e anote: conta para o A7.
   **Pare aqui, sem instalar, se:**
   - a ISO só oferecer a instalação;
   - pedir para registrar uma chave (MOK);
   - pedir para desligar o Secure Boot.
   As saídas estão na seção 14.
3. Na área de trabalho do KDE, **nunca** abra "Install to Hard Drive" ou o instalador. Se ele abrir sozinho, feche.
4. **Feche o Steam** se ele abrir sozinho: clique com o botão direito no ícone da bandeja > Sair. O Steam Input toma o controle
   e cria controles virtuais. O `coletar.sh` registra se ele ficou aberto.
5. Abra o terminal (Konsole). O Dolphin monta o disco de troca ao clicar nele. Guarde o caminho numa variável, com o rótulo do
   seu disco no lugar de `<rotulo>` (`ls /run/media/$USER` mostra):

   ```
   ls /run/media/$USER
   T=/run/media/$USER/<rotulo>/BS-TROCA/publico/ferramentas
   bash $T/ubuntu/preparar_sessao.sh
   ```

   Ele confere os arquivos copiados (alguns minutos) e cria e monta o `trabalho.img` de 20 GB. Pergunta antes; o udisks monta
   sem senha. Fim esperado: `Pronto. Pasta de trabalho: /run/media/<usuario>/BS-TRABALHO`.
6. Retrato do sistema **antes de qualquer coisa**. Só lê; `--privilegiado` acrescenta o log do kernel filtrado e pede
   autorização para o `sudo dmesg`:

   ```
   bash $T/ubuntu/coletar.sh inicio --privilegiado
   ```

   O retrato registra:
   - a imagem em uso (`rpm-ostree status`) e os pacotes que importam (`rpm -q`);
   - o SELinux, o Game Mode e os decodificadores do `ffmpeg`;
   - os drivers de GPU, áudio e controle.

## 4. A1 e A2: do pacote ao jogo, sem pacotes do sistema (15 min)

```
bash $T/ubuntu/a1_a2.sh original
```

Igual ao Ubuntu (seção 4 de `N9-L-ROTEIRO-UBUNTU.md`). O script:
- confere o pacote, extrai no `trabalho.img` e roda `instalar.sh --atalho` e `--verificar`;
- roda o `bs-builder` do CDI até o jogo em `BS-TRABALHO/Jogos/Blue Stinger`;
- confere `--verify-data` e o `ldd` (nada de Wine ou Proton);
- confere o Zig do pacote no log do Builder (não o compilador do sistema), a edição e o CDI sem mudança;
- faz o A2 contra os dados e o C++ do Windows.

Esperado: `bs-builder rc=0`, `--verify-data rc=0`, `0 falhas` no A2.

```
bash $T/ubuntu/a1_a2.sh remastered     # o jogo diz "[edicao] perfil remastered" e "[camera] modo livre"
bash $T/ubuntu/a1_a2.sh original       # volta; A1.preserva-arquivos-do-usuario = ok
```

**Pontos próprios do Bazzite:**
- **glibc:** o Bazzite traz uma glibc bem mais nova que a 2.31 do alvo. O `ldd` do jogo e do `bs-builder` tem de resolver tudo
  pelo sistema.
- **SELinux** (`Enforcing` no retrato):
  - Se o jogo ou o Builder não puder ser executado dentro do `BS-TRABALHO` ("Permission denied" com o arquivo executável),
    anote `A1 falha "SELinux: <mensagem>"`. Não mude o SELinux.
  - Alternativa sem mexer em nada: rodar tudo na pasta pessoal da sessão ao vivo, que fica na RAM. Use
    `export BS_TRAB=$HOME/BS-TRABALHO; mkdir -p $BS_TRAB` antes dos scripts e repita o passo.
- **Atalho:** o `instalar.sh --atalho` cria a entrada no menu do KDE. Confira que ela abre o Recompiler.

## 5. Ferramentas (sem instalar nada no sistema)

O Bazzite não tem `apt`, e o `rpm-ostree install` muda a imagem e exige reiniciar, o que o modo ao vivo perde. Por isso esta
sessão não instala pacotes do sistema.

```
command -v vulkaninfo && vulkaninfo --summary | grep -E "deviceName|driverName|driverInfo|apiVersion"
bash $T/ubuntu/coletar.sh sistema depois-ferramentas
```

- Anote os nomes dos dispositivos:
  - a GPU integrada aparece como `AMD Radeon Graphics (RADV …)`;
  - a RTX 5080 aparece no modo V como `NVIDIA GeForce RTX 5080`, com `driverName = NVIDIA proprietary driver`; no modo P, só
    se o NVK a reconhecer;
  - o `llvmpipe` é a CPU e **não** conta para o A7.
- **Se faltar o `vulkaninfo` ou a camada de validação** (`VK_LAYER_KHRONOS_validation`, usada no A7.3), há uma alternativa que
  não toca no sistema: o **Vulkan SDK da LunarG para Linux**. É gratuito; o carregador e as camadas são Apache-2.0.
  1. Baixe o `.tar.xz` em `vulkan.lunarg.com` e confira a soma publicada.
  2. Extraia em `/run/media/$USER/BS-TRABALHO/vulkan-sdk`.
  3. Use só nas execuções em que ele é preciso:
     - `vulkaninfo`: `$SDK/x86_64/bin/vulkaninfo --summary`;
     - validação: `VK_LAYER_PATH=$SDK/x86_64/share/vulkan/explicit_layer.d` na linha do A7.3.

  Sem rede ou sem o SDK, o A7.3 fica `nao-feito`. O jogo não precisa do SDK.

## 6. Suíte do Builder no hardware real (opcional, ~40 min)

```
python3 -I $T/test_builder_linux.py --pacote $T/../BlueStinger-Builder-*-linux-x86_64.tar.gz \
    --cdi "$T/../../privado/Blue Stinger.cdi" --saida /run/media/$USER/BS-TRABALHO/suite \
    --windows-data $T/../../privado/win-data --windows-generated $T/../../privado/win-generated \
    2>&1 | tee $T/../../evidencias/suite.txt
```

Esperado como no WSL: `108 ok, 0 falha(s)`. Anote: `bash $T/ubuntu/anotar.sh suite ok "108/0 no Bazzite"`. Se algum caso
pedir uma ferramenta que o Bazzite não traz (o Python vem no sistema), anote o caso como `nao-feito`, sem instalar nada.

## 7. A7: Vulkan na GPU real

Cada linha é uma execução com `bash $T/ubuntu/jogar.sh <rótulo> [VAR=valor] [-- argumentos]`.
- A prova do driver é o **nome do dispositivo no log**, ligado ao driver pelo `vulkaninfo`. O carregador abre todos os drivers
  instalados, então a lista de bibliotecas não prova nada.
- O script avisa se o Vulkan usado foi o `llvmpipe` ou se houve recuo para o software sem pedido.
- Jogue alguns minutos em cada execução: título, NEW GAME, primeira área, menu (Esc). Feche pelo menu ou pela janela.

| Item | Execução | Conferir |
|---|---|---|
| A7.1 RADV | `jogar.sh a7-radv BS_VULKAN_GPU="AMD Radeon"` | log `[apresentacao] Vulkan direta: AMD Radeon Graphics (RADV …)`; cenários, personagens, fog e transparências corretos; sem tremor nem tela preta |
| A7.2 RTX 5080 | **modo P:** só se o `vulkaninfo` mostrar a RTX 5080 pelo NVK (Mesa sobre o `nouveau`): `jogar.sh a7-nvk BS_VULKAN_GPU=NVIDIA`, conferindo `(NVK …)` no log. Senão, `anotar.sh A7.2 nao-feito "ao vivo sem driver para a RTX 5080"`. **Modo V:** `jogar.sh a7-nvidia BS_VULKAN_GPU=NVIDIA` com o driver da NVIDIA (`driverName = NVIDIA proprietary driver`; `/proc/driver/nvidia/version` no `gpu.txt`) | o mesmo da A7.1, com o nome da RTX 5080 no log |
| A7.3 validação | `jogar.sh a7-validacao BS_VULKAN_GPU="AMD Radeon" VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`, mais `VK_LAYER_PATH=…` se a camada vier do SDK (passo 5) | `grep -c "Validation Error" $T/../../evidencias/sessoes/a7-validacao.txt` = 0; repita com `BS_VULKAN_GPU=NVIDIA` se o A7.2 rodou |
| A7.4 janela e tela cheia | na A7.1: F11; menu > Gráficos: janela, tela cheia, sem bordas, resoluções | troca sem tela preta, imagem centrada, proporção certa; com o KDE em escala diferente de 100 %, o tamanho da janela também |
| A7.5 Wayland e XWayland | `jogar.sh a7-wayland SDL_VIDEO_DRIVER=wayland BS_VULKAN_GPU="AMD Radeon"` e `jogar.sh a7-x11 SDL_VIDEO_DRIVER=x11 BS_VULKAN_GPU="AMD Radeon"` | as duas abrem e jogam; `libwayland-client` numa, `libX11`/`xcb` na outra. O KDE do Bazzite é Wayland: a `x11` passa pelo XWayland |
| A7.6 software pedido | `jogar.sh a7-software -- --render software` | log `[render] renderizador: software`; joga |
| A7.7 sem Vulkan: recuo explícito | `jogar.sh a7-sem-vulkan VK_ICD_FILENAMES=/nao-existe.json` | o log diz `Vulkan indisponivel (…)` e o jogo segue no software: recuo **com aviso**, nunca silencioso |
| A5 estabilidade | `jogar.sh a5-30min BS_VULKAN_GPU="AMD Radeon"` (no modo V, também com `BS_VULKAN_GPU=NVIDIA`): NEW GAME, primeira área, SAVE, sair, abrir de novo, LOAD, 30 min de jogo | sem queda, sem travamento, sem erro de apresentação no log |
| G.1 Game Mode (Feral) | se o retrato mostrar `gamemoderun`: `BS_PREFIXO=gamemoderun bash $T/ubuntu/jogar.sh g1-gamemode BS_VULKAN_GPU="AMD Radeon"`, 5 min | joga igual; `libgamemodeauto` na lista de bibliotecas; `gamemoded -s` em outro terminal diz `active`. Sem o `gamemoderun`: `nao-feito` |
| G.2 Modo de jogo do Steam (gamescope) | não se aplica à imagem desktop desta sessão | `nao-se-aplica`. O jogo como "jogo não Steam" no modo de jogo é da L6 (SteamOS e Deck), não desta sessão |

Depois de cada uma: `bash $T/ubuntu/anotar.sh <item> ok|falha|parcial "o que viu"`.

## 8. A8: áudio, filmes e legendas no dispositivo real

O áudio do Bazzite é PipeWire, com o `pipewire-pulse` no lugar do PulseAudio. Instale as legendas locais no jogo (ficam em
`mods/`):

```
J="/run/media/$USER/BS-TRABALHO/Jogos/Blue Stinger/BlueStinger"
cp -r $T/../../privado/legendas/* "$J/mods/"
```

| Item | Execução | Conferir |
|---|---|---|
| A8.1 rota com filmes | `jogar.sh a8-rota BS_VULKAN_GPU="AMD Radeon" BS_RELOGIO_FIXO=1700000000 BS_INPUT_SCRIPT=$T/roteiros/passo_newgame_area1.txt` | NEWGAME e depois E001: imagem e som juntos, falas no tempo da boca, legenda no tempo da fala, nada cortado; a passagem do filme para o jogo sem tela preta longa nem som sobrando |
| A8.2 START no filme | `jogar.sh a8-start BS_VULKAN_GPU="AMD Radeon"`: NEW GAME e START no meio do NEWGAME | o filme para (`[video] parado pelo jogo`), **nenhum som do filme continua**, o jogo segue |
| A8.3 legendas e menu | na A8.2: menu (Esc) > Legendas: pt-BR, en, nenhuma | a legenda troca e some; textos com acento legíveis |
| A8.4 E016 e E017 | `mkdir -p /run/media/$USER/BS-TRABALHO/saves-sessao && cp -r $T/../../privado/gravacoes/sessao-20261021-145006/saves_inicio/. /run/media/$USER/BS-TRABALHO/saves-sessao/` e `jogar.sh a8-sessao BS_VULKAN_GPU="AMD Radeon" BS_RELOGIO_FIXO=1700000000 BS_INPUT_SCRIPT=$T/../../privado/gravacoes/sessao-20261021-145006/entrada.txt -- --saves-pasta /run/media/$USER/BS-TRABALHO/saves-sessao --tempo nativo` | os dois filmes no meio do jogo, com som e imagem juntos; se a reprodução sair do caminho gravado, anote (não é critério desta sessão) |
| A8.5 os 16 filmes | `BS_VULKAN_GPU="AMD Radeon" bash $T/ubuntu/filmes.sh todos` (65 min; `--perguntar` pergunta filme a filme; `filmes.sh um E134.SFD` repete um) | `16/16` no `evidencias/filmes/resumo.tsv`; **veja e ouça** pelo menos E017, E134, E185A, E200B e ENDING2 e anote cada um (`anotar.sh A8.filme.E017 ok "…"`) |
| A8.6 troca de saída | durante a A5 ou a A8.1, troque a saída de som no ícone de volume do KDE (alto-falante, fone, HDMI da RTX) | o som segue na nova saída, sem queda do jogo; no máximo um corte curto |
| A8.7 PipeWire e Pulse | `jogar.sh a8-pipewire SDL_AUDIO_DRIVER=pipewire` e `jogar.sh a8-pulse SDL_AUDIO_DRIVER=pulseaudio` (2 min cada, com música e efeitos) | as duas tocam; `libpipewire` numa, `libpulse` (pelo `pipewire-pulse`) na outra |
| A8.8 sessão sem cortes | na A5 de 30 min | efeitos, falas e música sem estalos nem cortes |

A comparação automática de imagem e PCM do WSL (L4) **não** substitui estes itens: aqui o que conta é o que você vê e ouve.
A cobertura automática (`filmes.sh todos`) mede o fim de cada filme; a amostra vista e ouvida é o que aprova o audiovisual.

## 9. A11: mods de filme

O retrato do passo 3 lista o `ffmpeg` do Bazzite e os decodificadores dele (`sistema.txt`). O mod de teste (`E017.mp4`) é
H.264 com AAC.

```
BS_VULKAN_GPU="AMD Radeon" bash $T/ubuntu/filmes.sh mods
```

O script decide pelo que o sistema tem:
- **Sempre:** mod MP4 com o `ffmpeg` fora do `PATH`: o jogo toca o original e avisa (`A11.mod-sem-decodificador`). Também
  testa o mod vazio e o mod truncado.
- **`ffmpeg` com H.264:** o `E017.mp4` toca pelo `ffmpeg` no lugar do NEWGAME (`A11.mod-mp4-ffmpeg`), e o nome em minúsculas é
  aceito.
- **`ffmpeg` sem H.264** (possível em Fedora e derivados, porque o H.264 tem patentes): o jogo tem de recusar o mod com aviso e
  tocar o original (`A11.mod-ffmpeg-sem-codec`). O `A11.mod-mp4-ffmpeg` fica `nao-feito` com o motivo. É o comportamento
  esperado, não falha do jogo.
- **Sem `ffmpeg`:** os casos com `ffmpeg` ficam `nao-feito`. **Não instale** o `ffmpeg` no sistema (`rpm-ostree` muda a
  imagem).

O script nunca toca num mod seu: se já houver `mods/video/NEWGAME.*`, ele para. As cópias que ele cria são apagadas no fim.
Mods de dublagem e música não vão nesta sessão (não há mod de teste); ficam com os testes automáticos da L4 e `nao-feito` aqui.

## 10. A9: controles

Com o Steam fechado (passo 3.4) e a execução `jogar.sh a9-controles BS_VULKAN_GPU="AMD Radeon"`:

| Item | Conferir |
|---|---|
| A9.1 teclado | menus, movimento, ações, START/pausa pelas teclas do `controles.toml` |
| A9.2 mouse | o menu interno: linhas e setas ◀ ▶ acertadas pelo clique; o cursor some no jogo e volta no menu |
| A9.3 controle por cabo | log `[controles] conectado: <nome> (GUID …)`. Anote o nome, o GUID e o módulo do kernel (`entrada.txt`: `xpad`, `xone` ou `hid_xpadneo`). Se o GUID termina em `68xx`, o SDL usa o HIDAPI; senão, o evdev. Menus, movimento, câmera, ações, START e pausa |
| A9.4 reconexão | tire o cabo no meio do jogo e ponha de novo: `desconectado` e `conectado` no log; o controle volta sem reiniciar o jogo |
| A9.5 Bluetooth (se houver) | pareie pelo ícone de Bluetooth do KDE; o mesmo da A9.3 (o Bazzite pode usar o `xpadneo` no sem fio: anote o nome e o GUID); desligue e ligue o controle |
| A9.6 remapeamento | no `controles.toml` ao lado do jogo, troque uma tecla, abra de novo: a nova tecla vale. Desfaça depois |
| A9.7 câmera por edição | Original: câmera original. `a1_a2.sh remastered` e abra de novo: o direcional direito controla a câmera livre. Volte com `a1_a2.sh original` |
| A9.8 Steam aberto (informativo) | opcional, no fim: abra o Steam, rode `jogar.sh a9-steam-aberto` e anote se o controle aparece em dobro ou some | só registra o comportamento; não é critério |

## 11. Pastas do usuário e modo portátil

No Bazzite a pasta pessoal fica em `/var/home/<usuário>`; `~` aponta para ela.

| Item | Execução | Conferir |
|---|---|---|
| X.1 XDG dos saves | depois de um SAVE: `ls ~/.local/share/DC-Recomp/"Blue Stinger"/saves` | o cartão está ali |
| X.2 configuração sem permissão na pasta do jogo | `chmod a-w "$J"`, `jogar.sh x2-config`, feche, `ls ~/.config/BlueStingerRecomp`, depois `chmod u+w "$J"` | `controles.toml` criado em `~/.config/BlueStingerRecomp` |
| X.3 portátil | `jogar.sh x3-portatil -- --saves-portatil` e um SAVE | o cartão vai para `"$J/saves"` |
| X.4 preservação | `a1_a2.sh original` de novo | `A1.preserva-arquivos-do-usuario` = ok |

**No modo ao vivo a pasta pessoal some ao desligar.** Os saves XDG desta sessão são de teste; as evidências guardam só os
hashes.

## 12. Fechar a sessão

```
bash $T/ubuntu/coletar.sh final --privilegiado
```

Ele refaz o retrato, junta hashes, `VERSAO.txt`, edição, logs, pastas do usuário (só nomes e hashes) e a matriz. Procura dados
pessoais e arquivos de mídia e recusa o pacote se achar. Grava `BS-TROCA/evidencias-<data>.tar.gz`. Desligue pelo menu do KDE,
tire o pendrive A, ligue o Windows e mande ao projeto **só** o `evidencias-<data>.tar.gz`.

## 13. Matriz de critérios desta sessão

| Critério do plano | Itens | Fonte |
|---|---|---|
| A1 instalação limpa (Bazzite; SteamOS fica na L6) | A1, A1 edição Remastered, A1.preserva-arquivos-do-usuario | `a1_a2.sh` |
| A2 dados | A2 | `a1_a2.sh` (e B3/B11 da suíte, opcional) |
| A5 execução real | A5, A8.1, A8.2 | `jogar.sh` |
| A7 Vulkan | A7.1–A7.7 | `jogar.sh`, `vulkaninfo` |
| Game Mode | G.1; G.2 `nao-se-aplica` | `jogar.sh` |
| A8 áudio e filmes | A8.1–A8.8, A8.filme.* (amostra vista e ouvida) | `jogar.sh`, `filmes.sh todos` |
| A9 controles (Deck fica na L6) | A9.1–A9.7 (A9.8 informativo) | `jogar.sh` |
| A11 mods | A11.* de `filmes.sh mods`; legendas na A8.3; dublagem e música `nao-feito` | `filmes.sh mods` |
| Pastas e portátil (L5) | X.1–X.4 | `jogar.sh`, `a1_a2.sh` |
| A3, A4, A13 | não se repetem aqui | `STATUS.md` |

Um critério só passa a aprovado no `STATUS.md` depois que o projeto receber e conferir as evidências.
- **Modo P:** os resultados ficam registrados como "Bazzite ao vivo (instalador)", com o kernel e o `os-release` do retrato.
  Eles não fazem do Bazzite um ambiente validado.
- **Modo V:** o Bazzite entra como "ambiente validado", com a imagem e a versão registradas pelo `rpm-ostree status`. Isso não
  vale para outras imagens do Bazzite nem para o SteamOS.

## 14. Se o modo ao vivo não servir

Nenhuma das saídas abaixo é feita sem autorização do mantenedor. Todas mudam algo que esta sessão promete não mudar.

| Situação | Saída | O que muda |
|---|---|---|
| A ISO não oferece o modo ao vivo | (a) ir direto ao modo V (seção 15) | o que a seção 15 lista |
| | (b) Sessão em **Ubuntu** ao vivo (`N9-L-ROTEIRO-UBUNTU.md`) | nada; mas o Bazzite fica sem validação |
| A RTX 5080 sem driver no modo P | (c) deixar o A7.2 `nao-feito` na prévia (padrão) e fazê-lo no modo V | nada agora |
| Falta `ffmpeg` ou H.264 e se quer o A11 com `ffmpeg` | (d) `ffmpeg` portátil no `BS-TRABALHO`, posto no `PATH` só para o `filmes.sh` | nada no sistema; é um binário de terceiros (GPL), só para o teste |
| O SELinux impede executar do `BS-TRABALHO` | (e) a pasta pessoal na RAM (seção 4) | nada |

## 15. Modo V: Bazzite instalado num SSD externo (só com autorização)

Esta seção **não é executada sem autorização explícita do mantenedor**. Ela muda três coisas que o resto do roteiro promete
não mudar:
- formata o SSD externo dedicado;
- grava uma entrada de boot na NVRAM da placa;
- registra a chave MOK.

O Windows, os discos internos e as partições deles continuam fora.

**Requisitos a mais:**
- um SSD externo USB 3 de 128 GB ou mais, dedicado ao Bazzite (disco C, diferente do disco de troca B); ele será apagado;
- a senha de registro da chave do Universal Blue, que a documentação do Bazzite mostra no primeiro boot das imagens NVIDIA;
- tempo: ~1 h para instalar e atualizar, mais a sessão.

**Instalação** (feita por você, com os discos conferidos na tela):
1. Boot pelo pendrive A (F12). No instalador, escolha **só o SSD externo C** como destino. Confira modelo e tamanho: nenhum
   disco interno pode estar marcado. Se a placa permitir desligar os discos internos sem mexer em partições, é a forma mais
   segura. Anote o que foi feito.
2. O instalador cria a partição EFI no próprio SSD C e uma entrada de boot na NVRAM. Depois, o boot do Windows continua pelo
   gerenciador do Windows, e o do Bazzite pelo F12, escolhendo o SSD C. Não mude a ordem de boot.
3. No primeiro boot, registre a chave MOK, como a tela pedir. Sem ela, o Secure Boot recusa o módulo da NVIDIA e o A7.2 fica
   `nao-feito`. Desligar o Secure Boot é a outra saída, mas muda a BIOS e pode pedir a chave do BitLocker: decida antes.
4. Não instale pacotes (`rpm-ostree install`), não troque de imagem e não rode `ujust`. Uma atualização da própria imagem
   (`rpm-ostree upgrade`, ou a automática) é permitida antes da sessão, para validar a versão atual. Registre a versão final
   no retrato.

**A sessão no modo V:** as seções 3.4 a 12, iguais, com o disco de troca B montado em `/run/media/$USER/<rotulo>`.
- Se a prévia (modo P) já passou, repita só o que depende do sistema instalado:
  - `coletar.sh inicio`;
  - A1/A2 (glibc e SELinux da imagem);
  - A7 inteiro (com o A7.2 no driver da NVIDIA);
  - A5;
  - A8.1, A8.5 (a amostra vista e ouvida), A8.6 e A8.7;
  - A9.3 a A9.5 (os módulos do kernel da imagem);
  - G.1;
  - `coletar.sh final`.
- Sem prévia, faça tudo.

**Depois da sessão:** o SSD C pode ficar como está ou ser apagado no Windows. Isso não afeta o Windows.
