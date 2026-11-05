# N9.0: perfis de edição

Primeira subetapa do N9 (`docs/N9-PLANO.md` seção 5). Um executável e um núcleo, com duas edições. O resultado das provas fica
no `STATUS.md`.

## Contrato

| Regra | Como funciona |
|---|---|
| Onde fica | `instalacao.toml`, ao lado do `BlueStinger.exe`: `[edicao] perfil = "original"` ou `"remastered"` |
| Sem o arquivo ou sem `[edicao]` | Original. Vale para toda instalação de antes do N9 |
| Valor inválido | Original, com aviso no log. O jogo abre |
| Quem escreve | Só o instalador: `--edicao` no `bs-builder` e no `BlueStinger-Recompiler`. A tela de escolha é do N9.1 |
| Atualização sem escolher a edição | Mantém a edição, o local dos saves e o idioma da instalação (`install_config_for`). Vale também para `--auto` sem `--portatil` |
| `instalacao.toml` com erro de sintaxe | Para o jogo e para o instalador valem os padrões, com aviso no log. Se o instalador precisa escrever, grava esses padrões com a escolha nova |
| Leitura | Uma vez, ao abrir (`editionProfile()` em `app/main_sdl.cpp`). O jogo não tem `--edicao`, e a opção é recusada |
| Save | Não guarda nem altera o perfil |

Código: `native/platform/edition.h` e `edition.cpp`.

## O que a edição muda hoje

Só a câmera. Na Remastered:

- a câmera é Livre e o movimento é relativo a ela, sem opção de desligar;
- `--camera original` é ignorado e registrado no log;
- `Modo` e `Movimentação` não aparecem no menu;
- "Restaurar padrões" mantém a câmera Livre.

Estes são os pontos de edição no código:

| Ponto | Arquivo |
|---|---|
| Linha de comando | `main`, em `app/main_sdl.cpp` |
| Sem janela | `gameOptions`, em `app/main_sdl.cpp` |
| Janela | `mainWindow`, em `app/main_sdl.cpp` |
| Gravação das preferências | `InGameMenu::persist`, em `app/main_sdl.cpp` |
| Menu | `SettingsMenu::entries` e `commit`, em `app/settings_menu.cpp` |

## Preferências entre edições

As duas edições usam os mesmos arquivos de preferências. O perfil limita o valor usado, sem reescrever o arquivo: quem tem
`modo = "original"` no `controles.toml` joga com a câmera Livre na Remastered, e o valor continua lá. Se o jogador muda a
sensibilidade, só ela é gravada (`for_file`). Isso fecha a decisão 1 da seção 12 do plano.

## Sem mudança nesta etapa

- **Legendas:** o modo automático já escolhe pt-BR quando o pacote está instalado. Sem o pacote, usa en-US. Nada depende da edição.
- **Gráficos:** o preset Remastered ainda não existe. Ele nasce com os recursos gráficos do N9.5 ao N9.8.
- **Saves, menus de jogo e widescreen:** ficam no N9.2, N9.3 e N9.5. (N9.3 cancelado em 2026-11-05: os saves continuam os do jogo original, `docs/N9-PLANO.md` seção 15)

## Provas

- Unitários `edition_*` em `tests/unit/test_edition.cpp`.
- `tools/dist/test_edicao.ps1`: casos E0 a E8, numa cópia da Preview por hard links. O E8 usa o menu real, com o controle virtual. A pasta `remastered` fica pronta para o portão.
- Portão de campanha (`docs/N9-PLANO.md` seção 14.2): `python tools/dev/test_campanha.py --casos C8 --outra <pasta>`, com a
  edição anterior e com a Remastered.
- `test_builder`: Recompiler com `--edicao remastered`; Recompiler sem escolhas e Developer preservando edição, saves, legendas e o save do jogador.
- Referências do N8.0 e do N8.7 no executável novo, que é a definição de "Original idêntica".
