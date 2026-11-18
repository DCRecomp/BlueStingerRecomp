# Aspectos legais

## O que este projeto é

Um conjunto de ferramentas (extrator, recompilador e runtime) que transforma uma
cópia **fornecida pelo próprio usuário** do disco de Blue Stinger em um
executável nativo. Blue Stinger é © Climax Graphics / Sega / Activision. Este
projeto não tem afiliação com nenhum detentor de direitos.

A licença MIT (`LICENSE`) cobre só o código-fonte deste repositório. This license covers only the source code of this
repository. It grants no rights to Blue Stinger or to any data, code or assets derived from it.

## O que nunca entra no repositório

- Imagens de disco (CDI, GDI, CHD, ISO, BIN/CUE).
- BIOS ou flash do Dreamcast. Também não são necessárias, porque o boot e as
  syscalls são HLE.
- Arquivos extraídos do disco (`game-data/`): executável, modelos, texturas,
  áudio, vídeo.
- Código gerado a partir do executável do jogo (`generated/`, `analysis/`).
- Saves, traces e capturas que contenham dados do jogo.
- Transcrições e traduções das falas e dos filmes (pacotes de legendas): são texto derivado do jogo e ficam fora do
  repositório. As ferramentas que as geram são do projeto (`mods-padrao/README.md`, `docs/LEGENDAS-PENDENCIAS.md`).
  **Decisão do mantenedor (2026-11-12):** a partir da v0.9.7-1, os pacotes `legendas-en` (en-US) e `legendas-pt-br`
  (pt-BR) vão no ZIP do Builder publicado nas releases. Eles têm só o texto das legendas, sem áudio, vídeo nem dados do
  disco; o jogo continua vindo do CDI de cada usuário. O texto continua fora do git.

## Mod opcional: filmes widescreen (v0.9.7-2)

**Decisão do mantenedor (2026-11-17/18):** os filmes widescreen feitos por fãs (o centro 4:3 ampliado com Real-ESRGAN e as
laterais 16:9 geradas com Wan2.1-VACE) são um **mod**, publicado à parte no Internet Archive
(`bluestinger-recompiled-widescreen-movies`), nunca neste repositório nem no ZIP do Builder.

- O instalador da Remastered oferece baixá-los (opção marcada por padrão, que o jogador pode desmarcar; `--filmes-widescreen`
  no `bs-builder`). Cada arquivo é conferido pelo SHA-256 da lista do item e vai para `mods\video`.
- Sem o mod, ou se o download falhar, o jogo toca os filmes convertidos do disco do próprio jogador.
- O jogo, o áudio e todo o resto continuam vindo da cópia de cada usuário.
- Pedidos de remoção seguem o processo do Internet Archive; o projeto não hospeda os filmes.

O `.gitignore` bloqueia esses caminhos e extensões. `config/*.toml` contém só
metadados (ID de produto, hashes, tamanhos e endereços) e nenhum byte do jogo.

## Licença do projeto

**MIT** (ver [LICENSE](LICENSE)). Ela cobre só o código deste repositório e
não concede nenhum direito sobre o Blue Stinger ou sobre dados derivados dele.

## Uso de projetos de referência

| Projeto | Licença | Como é usado aqui |
|---|---|---|
| Flycast | GPL-2.0 | Leitura como referência de comportamento e **oráculo externo** em testes diferenciais (processo separado). **Nenhum código copiado ou linkado.** |
| N64ModernRuntime | GPL-3.0 | Só ideias de arquitetura. Nenhum código. |
| N64Recomp | MIT | Ideias de arquitetura. Se algum trecho for adaptado, manter o aviso de copyright MIT. |
| XenonRecomp / XenosRecomp | MIT | Idem. |
| Initial D AS3 SH-4 Recomp | sem licença visível no README (todos os direitos reservados até prova em contrário) | Só a estrutura do repositório e as lições publicadas. Nenhum código. |
| KallistiOS | KOS License (BSD-like) | Documentação de hardware. |
| gditools3 / loaders Ghidra | não confirmada | Documentação de formato. Nenhum código até confirmar a licença. |

Regra: qualquer código de terceiros só entra via `third_party/`, com a licença
registrada em `third_party/README.md`.
