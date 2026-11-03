# N9-L: sessão de validação em Ubuntu 24.04 nativo (L3, L4 e L5 juntas)

Uma instalação e uma sessão para o que o WSL não prova (B7): GPU, saída de áudio, controles, o `ffmpeg` do próprio sistema e o
fluxo completo do pacote num Linux limpo, do CDI ao jogo (A1 e A2 da L5). Nada aqui altera o disco, os SFD ou o Windows. Devolva
os arquivos e as anotações listados no fim.

## 1. Levar do Windows (pendrive, sem passar pelo git)

| Do Windows | Para o Ubuntu | Para quê |
|---|---|---|
| `BlueStinger-Builder-<versão>-linux-x86_64.tar.gz` e o `.sha256` (pacote da L5, montado no WSL; não publicar) | `~/` | A1, A2 |
| A sua imagem `Blue Stinger.cdi` | `~/` | A1, A2 |
| `dist\BlueStinger-Preview\data\` (só para comparar os dados, A2) | `~/bs-win-data/` | A2 |
| `generated\` do repositório (só para comparar o C++ gerado) | `~/bs-win-generated/` | A2 |
| `dist\BlueStinger-Preview\data\video\E017.mp4` (teste de mod; arquivo do seu disco, não publicar) | `~/E017.mp4` | A11 |

Os testes sem janela da L3 e da L4 (filmes bit a bit iguais aos do Windows, sincronia, texto) já foram provados no WSL e não precisam
de hardware; aqui fica só o que depende da máquina real.

## 2. Instalar sem nada do sistema (A1)

Num Ubuntu 24.04 recém-instalado, **sem** instalar pacotes de desenvolvimento:

```
cd ~ && sha256sum -c BlueStinger-Builder-*-linux-x86_64.tar.gz.sha256
tar -xzf BlueStinger-Builder-*-linux-x86_64.tar.gz
cd BlueStinger-Builder-*-linux-x86_64 && ./instalar.sh --atalho          # ~/.local/share/BlueStinger-Builder
./instalar.sh --verificar
time ~/.local/bin/bs-builder --disc ~/"Blue Stinger.cdi" --out ~/Jogos/BlueStinger 2>&1 | tee ~/builder.txt
~/Jogos/BlueStinger/BlueStinger/BlueStinger --verify-data
```

Anotar: tempo total, a última linha (`instalado: ...`) e o `VERSAO.txt` do jogo. Se algo falhar, guardar `~/builder.txt` e
`~/Jogos/BlueStinger/.bs-build/builder.log`.

## 3. Testes automáticos no Ubuntu real (opcional, mas recomendado)

Exige `git` e `python3` (já vêm no Ubuntu). Usa o mesmo pacote e o CDI:

```
git clone <repositório> ~/bs-repo && cd ~/bs-repo && git checkout <commit da L5>
python3 -I tools/dist/linux/test_builder_linux.py --pacote ~/BlueStinger-Builder-*-linux-x86_64.tar.gz \
    --cdi ~/"Blue Stinger.cdi" --windows-data ~/bs-win-data --windows-generated ~/bs-win-generated 2>&1 | tee ~/suite.txt
```

O resultado esperado é o mesmo do WSL, registrado no `STATUS.md`. A suíte usa uma HOME própria dentro da pasta de saída e não toca na
sua.

## 4. Sessão jogável (L3 e L4, A5 a A11)

Para os itens 9 e 10 (mod de filme), instale o `ffmpeg` (`sudo apt install ffmpeg`) só depois do passo 2: o A1 é sem pacotes.

`cd ~/Jogos/BlueStinger/BlueStinger && ./BlueStinger 2>&1 | tee ~/sessao.txt`

| # | Conferir | Critério |
|---|---|---|
| 1 | Abre pelo Vulkan da GPU real (o log diz o dispositivo); tela cheia e janela | A7 |
| 2 | Filme de abertura do NEW GAME: imagem, som e legenda juntos; a legenda some e volta pela opção do menu | A8, L4 |
| 3 | START no meio de um filme: pula e o jogo segue | L4 |
| 4 | NEW GAME até a primeira área; mover, câmera, LOAD e SAVE | A5, A9 |
| 5 | Menu interno (Esc): textos com acento legíveis, setas ◀ ▶, mouse acerta linhas e setas, confirmação de tela | L4, A9 |
| 6 | Teclado; controle Xbox (USB e, se houver, Bluetooth); uma tecla remapeada no `controles.toml` | A9 |
| 7 | Efeitos, falas e música sem cortes; trocar a saída de áudio com o jogo tocando: o som segue | A8 |
| 8 | Recuo para software: `BS_RENDER=software ./BlueStinger` abre e joga | A7 |
| 9 | Mod de filme: `mkdir -p mods/video && cp ~/E017.mp4 mods/video/newgame.MP4`; o NEW GAME toca o E017 (log `(mod)`) | A11 |
| 10 | O mesmo sem o ffmpeg à vista: `PATH=/nao-existe ./BlueStinger` toca o filme original e o log avisa `sem decodificador neste sistema` | A11 |
| 11 | Remastered: `~/.local/bin/bs-builder --disc ~/"Blue Stinger.cdi" --out ~/Jogos/BlueStinger --edicao remastered`; o jogo abre com a câmera livre | L5, A9 |
| 12 | 30 min sem queda (pode ficar para a L6) | A5 |

Ao terminar o item 10, apague `mods/video/newgame.MP4`.

## 5. Devolver

`~/builder.txt`, `~/suite.txt` (se rodou o passo 3), o `sessao.txt` de cada execução, o `VERSAO.txt` do jogo, a saída do
`vulkaninfo --summary` (se estiver instalado; senão, o que o log do jogo diz do dispositivo) e as anotações da tabela (o que passou,
o que falhou e como).
