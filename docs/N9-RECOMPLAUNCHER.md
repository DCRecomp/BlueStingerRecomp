# RecompLauncher / RecompStore: padrões e aplicação no projeto

Pedido do mantenedor em 2026-11-16: conferir os padrões de <https://recompstore.com/how> e aplicá-los ao projeto.

Fontes, lidas em 2026-11-16 e guardadas em `tmp/recompstore/` (fora do git):

- a página *How it works*;
- o guia do SDK (`https://recomplauncher.com/sdk-files/guide.md`);
- o esquema do `recomplauncher.json` (`https://recomplauncher.com/schema/recomplauncher.json`);
- o exemplo `fifawc14.json`.

Nada foi enviado à loja.

## Resumo

- **Como a loja funciona:**
  - o launcher (Windows, Linux e Mac) instala o recomp direto do **último release completo** no GitHub;
  - escolhe o arquivo pelo sistema e pelo processador escritos no nome;
  - confere o SHA-256 que o GitHub guarda;
  - lê um `recomplauncher.json` no topo do pacote.
- **Nenhum dado do jogo:** o jogador traz a própria cópia, como no nosso modelo.
- **Encaixe no projeto:** o Builder corresponde ao **nível 3** do SDK (`prepare`, um passo de preparação sem janela). O launcher entrega o CDI do jogador ao `bs-builder`, que gera o jogo nativo dentro da instalação. O Play então abre o `BlueStinger.exe` gerado. O jogo não precisa mudar.

## Conferência: padrão × projeto

| Padrão (SDK, *Recomp Standards*) | Projeto hoje | Situação |
|---|---|---|
| Nunca distribuir dados do jogo | Releases só com o Builder, sem dados (LEGAL.md) | ✅ |
| Um arquivo por sistema, com sistema e processador no nome (`-Windows-x64.zip`) | `BlueStinger-Builder-v0.9.7-1-win-x64.zip` | ⚠️ a partir do próximo release: `BlueStinger-Builder-vX-Windows-x64.zip` |
| Zip no Windows; uma pasta no topo é aceita | Zip com a pasta do Builder | ✅ |
| Extras (`.sha256`, `.sig`, `debug`, `pdb`) fora do jogo | `SHA256SUMS-<v>.txt` não tem sistema no nome e não é escolhido | ✅ |
| `recomplauncher.json` no topo do pacote | não existia | ✅ aplicado: `tools/dist/recomplauncher.json`, copiado pelo `make_builder.ps1` |
| Release completo (não pré-release) para os jogadores | todos os releases são pré-release | ❓ decisão R1 |
| Tag com versão e notas | sim | ✅ |
| Formatos de Dreamcast aceitos: CHD e GDI | só CDI | ❓ decisão R2 |
| Conferir a cópia e dizer o que está errado | o extrator confere o hash do CDI | ✅ |
| `--game <caminho>` para launchers e atalhos | `bs-builder --disc <cdi>`; o jogo instalado não precisa do disco | ✅ (equivalente) |
| Portátil quando possível | `--portatil` no instalador; saves em Documentos | ✅ |
| Configurações em arquivo legível (TOML/INI/JSON) | `graficos.toml`, `controles.toml`, `instalacao.toml` | ✅ |
| Logs em lugar conhecido | o Builder grava log; o jogo grava em stderr | ⚠️ ver R5 |
| Controles pelo SDL, remapeáveis | SDL3, `controles.toml` | ✅ |
| Sem administrador nem instalador obrigatório | sem administrador; o Builder compila com o Build Tools do Visual Studio | ⚠️ o compilador é pré-requisito (no Linux vem portátil) |
| Não baixar nada na primeira execução | não baixa | ✅ |
| Build para Linux e Mac | Linux existe (N9-L, v0.8.0); releases recentes só para Windows | ❓ decisão R3 |
| Modo Big Picture: abrir em tela cheia | não lia `RECOMPLAUNCHER_MODE` | ✅ aplicado (`app/main_sdl.cpp`) |
| Passo `prepare`: linhas `progress <0..1> <texto>`, sem janela, código 0 quando dá certo | `bs-builder` imprimia `[progresso] N% passo` | ✅ aplicado: `--progresso-launcher` |
| "How to play" no topo do README e selo "Get it on RecompLauncher" | o README começa pela descrição técnica | ❓ decisão R4 |
| Conquistas (Recomp Achievements) | não | fora do escopo (N10, se quiser) |

## O que foi aplicado (2026-11-16; compilar e testar depois dos filmes, com a GPU livre)

O commit deste documento leva só o `recomplauncher.json`, já conferido contra o esquema. Os itens 2, 3 e 4 ficam na worktree, fora do
commit, até a compilação e os testes previstos abaixo.

1. **`tools/dist/recomplauncher.json`**: níveis 1 e 3.
   - `inputs`: o CDI do jogador (`.cdi`, EUA, T13001N V1.000).
   - `prepare`: `bs-builder.exe --disc {input:disc} --out {install}/jogo --edicao remastered --progresso-launcher`, com tempo limite de 1 h.
     - `done` aponta para o exe gerado, então quem já preparou não prepara de novo.
     - `version` "1": mudar só quando todos precisarem refazer.
     - Opção marcável **Original edition**: acrescenta `--edicao original` (vale o último `--edicao`).
   - `start`: `jogo/BlueStinger/BlueStinger.exe`.
   - `saves`: `{documents}/DC-Recomp/Blue Stinger/saves`.
   - Conferido com um validador mínimo contra o esquema baixado (`tmp/recompstore/valida_manifesto.py`):
     - o nosso arquivo e o exemplo oficial passam;
     - um arquivo errado de propósito é recusado.
   - A conferência oficial é a aba *Developer* do launcher, que é do mantenedor.
2. **`bs-builder --progresso-launcher`** (`builder/main_cli.cpp`): imprime o progresso como `progress 0.40 <passo>`. O formato antigo continua o padrão.
3. **`RECOMPLAUNCHER_MODE=bigpicture`** (`app/main_sdl.cpp`): o jogo abre em tela cheia. Sem a variável, nada muda; F11 continua alternando.
4. **`make_builder.ps1`**: copia o `recomplauncher.json` para o topo do pacote.

Testes previstos (nível 1):

- build dos três presets e unitários;
- `bs-builder --progresso-launcher` com o CDI, conferindo as linhas `progress` e o código 0;
- o exe com `RECOMPLAUNCHER_MODE=bigpicture` (janela; avisar antes de abrir);
- `test_builder.ps1`.

## Decisões do mantenedor

- **R1, release completo:** o launcher só instala o último release *completo*; pré-releases viram betas, abertos pela loja a pedido.
  - Opções:
    - (a) publicar o próximo como release completo;
    - (b) continuar como pré-release e pedir a abertura como beta.
  - Recomendação: (b) até o fim do N9 e (a) no primeiro release estável.
- **R2, GDI e CHD:** os formatos padrão de Dreamcast segundo o SDK; o Redump é GDI. Hoje só o CDI é aceito.
  - GDI: leitura das faixas, sem dependência nova.
  - CHD: precisa do libchdr (BSD, compatível com a regra de dependências; entra no inventário de licenças).
  - Recomendação: GDI primeiro, CHD em seguida, como etapa própria com testes. Antes disso, conferir o hash do conteúdo extraído; hoje o hash conferido é o do CDI.
- **R3, Linux no release:** o N9-L já faz o pacote Linux. O SDK pede `-Linux-x64.AppImage` ou `.tar.gz` com o mesmo padrão de nome.
- **R4, README:** um "How to play" curto no topo (qual cópia usar e onde colocá-la). O selo da loja vem do kit de imprensa dela e só deve entrar se você quiser listar o projeto.
- **R5, log do jogo:** gravar um `.log` numa pasta `logs`, com os últimos N, para o relatório de travamento do launcher.
- **R6, inscrição na loja:** formulário em `recomplauncher.com/submit?platform=pc` com o repositório; a conta de desenvolvedor é opcional. É um ato externo, que fica com o mantenedor.

## Nível 2 (configurações no launcher), para depois

Mapear `graficos.toml` (resolução interna, proporção, AA, efeitos) em `config` e `settings`. Antes, é preciso confirmar o caminho do arquivo em cada modo (portátil ou pasta do usuário) e quais chaves o jogo relê a cada partida. Fica para depois do N9.8 (a D2 mexe no AA).
