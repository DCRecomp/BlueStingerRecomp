# RecompLauncher

Pedido do mantenedor em 2026-11-22: adicionar o suporte ao RecompLauncher pelo guia do SDK
(`https://recomplauncher.com/sdk-files/recomplauncher-agent.md`, partes 1 a 3) e o selo no README. Substitui o rascunho de
2026-11-16 do branch `n9-8` (`docs/N9-RECOMPLAUNCHER.md`), que aceitava só o CDI. Nada foi enviado à loja.

O projeto já está no catálogo da loja duas vezes, com o mesmo repositório: `bluestingerrecomp` (autor rodrigo-mendonca) e
`bluestingerrecomp-2` (autor DCRecomp), conferido em `https://recomplauncher.com/pc-catalog-web.json` em 2026-11-22. O selo
aponta para `bluestingerrecomp`.

## O que foi aplicado

1. **`tools/dist/recomplauncher.json`** (níveis 1 e 3 do SDK):
   - `inputs`: o disco do jogador em GDI, CUE, CHD ou CDI, também em `.zip` ou `.7z`. Vale qualquer versão suportada
     (EUA, PAL e Japão); quem decide é o `bs-builder`, pelo executável de boot.
   - `prepare`: `bs-builder.exe --disc {input:disc} --out {install}/jogo --progresso-launcher`, com limite de 1 h. `done`
     aponta para o exe gerado, e `version` "1" só muda quando todos precisarem refazer.
   - Opções:
     - **Use the portable compiler** (só no Windows, marcada): `--compilador-portatil`;
     - **Remastered edition** (marcada): `--edicao remastered`. Sem ela, a Original, que é o padrão do Builder e a única
       fora do disco americano (D-P2). O Builder recusa a Remastered com um disco PAL ou japonês e diz para instalar a
       Original;
     - **Widescreen movies** (desmarcada): `--filmes-widescreen`, ignorada na Original.
   - `start`: `jogo/BlueStinger/BlueStinger.exe`; `saves`: `{documents}/DC-Recomp/Blue Stinger/saves`.
   - Sem `game.dumps`: o guia proíbe inventar os valores, e o projeto não registra o CRC32 do Redump.
   - Sem `system`: o Vulkan tem recuo para o software, então nenhum requisito gráfico é obrigatório.
2. **`bs-builder --progresso-launcher`** (`builder/main_cli.cpp`): o progresso sai como `progress 0.40 <passo>`, com a fração
   do total. Sem a opção, o formato `[progresso] N% passo` de antes não muda.
3. **`make_builder.ps1`**: copia o `recomplauncher.json` para o topo do pacote, ao lado do `bs-builder.exe`. Vale a partir
   do próximo pacote.
4. **README**: o selo "Get it on RecompLauncher" no topo, no formato do guia (claro no tema escuro e escuro no claro).

## Revisão de 2026-11-24 (manifesto v2)

Relato do mantenedor: no launcher, **Jogar abria o desinstalador** e a **instalação fácil não aparecia**. Causa: o zip da
v0.9.7-5 não tinha o `recomplauncher.json` (ele só entra a partir do pacote seguinte) e, num projeto do GitHub, o launcher só
lê o manifesto do zip do release ou do catálogo. Sem ele, o launcher escolheu um dos quatro `.exe` do pacote, o
`BlueStinger-Desinstalar.exe`. Pedido do mantenedor: suporte completo ao launcher e saves na nuvem.

- **Entrada `game`** (o nome que o guia pede para a instalação fácil), com o rótulo citando `.gdi`, `.cue`, `.chd` e `.cdi`
  (o launcher entrega o `.cue` só quando o rótulo o cita).
- **Saves na nuvem e backups:** o passo `prepare` passa `--saves-portatil` (nova opção do `bs-builder`), então os saves ficam
  em `{install}/jogo/BlueStinger/saves`; `saves` lista essa pasta e os `controles.toml`, `legendas.toml` e `audio.toml`
  (o `graficos.toml` fica de fora: depende da GPU de cada PC).
- **Configurações no launcher** (`config` + `settings`): modo de tela, resolução interna, proporção, VSync, antialiasing,
  filtragem, texturas HD; avançadas: iluminação, sombras, água e oclusão ambiente; legendas (ligadas, idioma, tamanho);
  volumes. Cada opção grava a chave própria (o jogo lê as chaves explícitas por cima do preset).
- **Idioma "Automático":** o `legendas.toml` passa a aceitar `idioma = "auto"` (o padrão no launcher), com a mesma regra do
  `instalacao.toml`: o idioma do Windows com pacote, senão pt-BR, senão en-US.
- **`folders`/`actions`:** saves, mods, pasta do jogo e o log da instalação (`{install}/jogo/.bs-build`).
- **`exit_codes`:** `0` é a saída normal. **`system`:** Windows 10.
- **Filmes widescreen** marcados por padrão.
- **Esquema atual:** `prepare.timeout` e `prepare.version` saíram do esquema oficial e foram tirados.
- **Release:** o zip passa a se chamar `BlueStinger-Builder-v<versão>-Windows-x64.zip`, com o conteúdo e o manifesto na raiz.

## Conferência

- O manifesto foi conferido contra o esquema oficial (`https://raw.githubusercontent.com/elripalda/RecompLauncher-SDK/main/schema/recomplauncher.json`)
  com o `jsonschema` do Python: 0 erros. Um arquivo errado de propósito foi recusado.
- A instalação completa com `--progresso-launcher` e o CHD americano está registrada no `STATUS.md`.
- A conferência oficial é a aba *Developer* do launcher ("Add a build"), feita pelo mantenedor.

## Padrões do SDK × projeto

| Padrão | Projeto | Situação |
|---|---|---|
| Nunca distribuir dados do jogo | só o Builder e o kit de fontes | ✅ |
| Formatos de Dreamcast: CHD e GDI | GDI, CUE/BIN, CHD e CDI desde a v0.9.7-3 | ✅ |
| Zip no Windows, sem instalador obrigatório | zip com a pasta do Builder | ✅ |
| `recomplauncher.json` no topo do pacote | a partir do próximo pacote | ✅ |
| Passo `prepare` sem janela, com `progress` e código 0 | `--progresso-launcher` | ✅ |
| Controles pelo SDL | SDL3 | ✅ |
| Sistema e processador no nome do arquivo (`-Windows-x64.zip`) | `BlueStinger-Builder-v0.9.7-5-win-x64.zip` | ⚠️ trocar para `-Windows-x64` no próximo release |
| Release completo para os jogadores | todos os releases são pré-release (beta só a pedido da loja) | ⚠️ decisão do mantenedor |
| Escrever só em `{install}` e `{data}` | saves em Documentos; o compilador portátil em `%LOCALAPPDATA%` | ⚠️ o `bs-builder` não tem a opção de saves portáteis do instalador |
| Logs num lugar conhecido | o jogo escreve no stderr | ⚠️ gravar `logs/*.log` e listar em `folders` |
| Big Picture em tela cheia (`RECOMPLAUNCHER_MODE`) | só no branch `n9-8`, sem commit nem teste | ⚠️ |
| Build para Linux | o N9-L existe; os releases recentes são só Windows | ⚠️ |
| Uma entrada no catálogo | duas entradas com o mesmo repositório | ⚠️ pedir à loja para juntar |
| Director's Cut pelo launcher | só pelo instalador (patch GPL com confirmação e segundo disco) | — fora do manifesto |
