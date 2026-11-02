# Monitor de Campanha (gate de campanha completa, D-N8-4)

O projeto exige uma campanha completa reproduzível. Em 2026-11-01 o mantenedor transferiu esse requisito do N8.8 para o **gate
pós-N9** (`docs/N8-PLANO.md` seção 9.1, `docs/N9-PLANO.md` seção 14): uma única gravação valida as duas edições. Ele continua
obrigatório e **não aprovado**; o monitor fica pronto e testado até lá, e nenhuma campanha precisa ser jogada agora.

O monitor grava a campanha enquanto você joga normalmente, em quantas sessões quiser, e depois **prova pela reprodução** que
ela vai do início aos créditos. Gravar não aprova nada: só a reprodução da cadeia completa aprova.

## Como usar

1. Abra `tools\dist\Jogar-e-Gravar-Campanha.cmd`.
2. Clique em **Jogar e gravar (continuar)**. A primeira vez cria a campanha (NEW GAME, cartão vazio); as seguintes continuam
   a campanha aberta. A janela mostra `● GRAVANDO`, a sessão e o passo da última entrada.
3. Jogue normalmente: teclado ou controle, nada de comandos. **Salve no jogo** antes de parar. F5 marca um checkpoint
   (opcional).
4. Feche a janela do jogo para terminar a sessão. Para continuar depois, abra de novo e clique em **Jogar e gravar**: o
   cartão da campanha já está lá; use LOAD no jogo.
5. Depois dos créditos, deixe o filme terminar (ou pule com START) e feche o jogo. Clique em **Verificar (reproduzir tudo)**.

O cartão da campanha é próprio: seus saves normais não são usados nem alterados, e o que você salva na campanha não aparece
no jogo normal. A instalação usada (Preview ou a do Builder) não é alterada.

## O que conta como progresso

Cada sessão registra três coisas separadas:

| Informação | Significado |
|---|---|
| **Gravado até o passo** | tudo o que foi jogado e gravado; uma queda não perde o que já foi gravado |
| **Último save no passo** | o último SAVE concluído; é daí que a sessão seguinte continua (LOAD) |
| **Passos depois do último save** | jogados e conferidos na reprodução, mas **não continuam**: a sessão seguinte volta ao save |

Uma sessão sem nenhum SAVE não avança a campanha. Por isso: salve antes de fechar.

## Queda ou travamento

Se o jogo cair, travar ou for encerrado, a sessão fica como **interrompida**, com o que foi gravado até ali. Ao abrir de novo,
o monitor recupera a sessão pelos arquivos e continua do cartão deixado. Se o próprio monitor cair com o jogo aberto, nada se
perde: a sessão é fechada pelos arquivos na próxima abertura. Uma sessão interrompida é conferida até o último passo gravado e
aparece assim no relatório (`REPRODUZIDA_ATE_A_QUEDA`).

## Vereditos

`Verificar` grava `relatorio.md` e `relatorio.json` na pasta da campanha.

| Veredito | Significado |
|---|---|
| `CAMPANHA REPRODUZIDA ATE OS CREDITOS` | **aprovada**: todas as sessões reproduzidas, cadeia íntegra, créditos concluídos na reprodução |
| `REPRODUZIDA, MAS INCOMPLETA (sem os creditos)` | o que existe confere, mas a campanha não chegou ao fim |
| `REPROVADA` | divergência, cadeia quebrada ou arquivos alterados; o relatório mostra a primeira divergência (sessão e passo) |
| `TESTE REPRODUZIDO` | campanha de teste; nunca vale para o D-N8-4 |

O que `Verificar` confere em cada sessão, reproduzindo headless com o programa fixado:
- o estado lógico de **todos** os passos e o VBlank de cada um;
- cada LOAD e SAVE, com o passo e o resultado;
- os saves de fim (sem a data do envelope `.bsv`, que é a hora real de gravação).

E na campanha:
- a primeira sessão começa de um cartão vazio;
- cada sessão começa dos saves de fim **reproduzidos** da anterior, não dos gravados;
- nenhum arquivo mudou depois da gravação (selos SHA-256 no manifesto);
- programa e dados são os fixados na criação;
- um filme dos créditos (`ENDING` ou `ENDING2`) **começou e terminou** dentro da reprodução. Gravação cortada no meio do
  filme não conta.

O relatório traz também a cobertura: filmes vistos, com o passo, e arquivos do disco lidos.

## Uma gravação, duas edições

A gravação não depende dos recursos gráficos:

- a entrada gravada é a **efetiva**, depois do mapa de controles e da movimentação relativa à câmera;
- a reprodução é headless, sem janela e no renderizador em software, mesmo que a sessão tenha sido jogada em Vulkan;
- a câmera é sempre explícita (`nova --camera original|livre`, padrão `original`): o `[camera]` do `controles.toml` não entra
  na gravação. Numa campanha Livre, gravação e reprodução comparam o estado sem o objeto câmera do SDK, que muda por definição
  e não é estado do jogo. Numa campanha Original a comparação continua com o estado completo.

`reproduzir-em` reproduz a cadeia em **outro programa** (outra edição, versão, câmera ou renderizador):

```
python tools/dev/campanha.py reproduzir-em <id> --instalacao <pasta> [--rotulo r] [--camera original|livre] [--render software|nativo]
```

Compara com o programa fixado, passo a passo: estado lógico sem o objeto câmera, VBlanks, LOAD/SAVE e saves; a cadeia segue
pelos saves que o programa novo produziu. Só vale para uma instalação feita do mesmo disco. O relatório fica em
`outros\<rotulo>\relatorio.json`, com `REPRODUZIDA EM OUTRO PROGRAMA` ou `DIVERGENTE`. Esse comando **nunca** aprova nem
reprova a campanha: o veredito continua sendo o de `Verificar`.

## Onde ficam os dados

Tudo fica fora do repositório, em `%LOCALAPPDATA%\BlueStingerRecomp\campanhas\<id>`:

```
campanha.json      manifesto: programa fixado (identidade e SHA-256), dados, sessões, selos
programa\          cópia do exe, DLLs e mods da instalação (os dados ficam no lugar, fixados pela identidade)
sessao_001\        entrada.txt, estado.tsv, saves.tsv, saves_inicio\, saves\, jogo.txt, checkpoint_<n>.bmp
                   reproducao\ (a última verificação)
relatorio.md/.json
```

Não versione nem publique essa pasta: ela tem saves e dados do jogo.

**Mudou a instalação no meio da campanha?** A campanha continua com o programa copiado na criação. Se os **dados** do jogo
mudarem, ela recusa continuar: reinstale os mesmos dados ou comece outra.

## Linha de comando

```
python tools/dev/campanha.py                  janela
python tools/dev/campanha.py nova [--instalacao <pasta>] [--camera original|livre]
python tools/dev/campanha.py jogar [<id>]
python tools/dev/campanha.py status [<id>]
python tools/dev/campanha.py verificar [<id>] [--sessao N]
python tools/dev/campanha.py reproduzir-em <id> --instalacao <pasta>
python tools/dev/campanha.py lista
```

Sem `--instalacao`, usa `dist\BlueStinger-Preview`. `--sessao N` reproduz só uma sessão e dá veredito parcial.

## Testes

`python tools/dev/test_campanha.py [--casos C1,C7]` roda com o jogo de verdade, em campanhas de teste: nova campanha e
continuidade com SAVE/LOAD reais, reprodução repetida, corrupção, divergência, queda do jogo, queda do monitor, segunda
instância, detecção dos créditos e, nos casos C8 e C9, a mesma gravação em gameplay real em outro programa e com a câmera Livre
movida. O jogador é simulado por `BS_GRAVAR_BASE` e pelo controle virtual, no mesmo caminho da
gravação real. Campanha de teste é marcada como tal e não aprova o D-N8-4; uma campanha real recusa roteiros de teste e não
pode começar de um save.
