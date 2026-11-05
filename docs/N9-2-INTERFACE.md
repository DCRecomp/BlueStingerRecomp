# N9.2: interface moderna do jogo (edição Remastered)

Autorizado pelo mantenedor em 2026-11-04 (`docs/N9-PLANO.md`, N9.2). A Original não muda: ela continua com o menu do jogo
original e o menu interno de configurações do N7c (matriz da seção 4 do plano).

## 1. Decisões

| Decisão | Origem |
|---|---|
| O instalador e o jogo compartilham a identidade visual, não o motor | 12.3, 2026-11-02 |
| O menu nativo aciona o jogo por **pontos de entrada nativos**, não por entrada sintética | mantenedor, 2026-11-04 |
| Sem VMU na Remastered: Continuar e Carregar vão direto à escolha do **perfil**, sem soquete nem slot | mantenedor, 2026-11-04 |
| Texto da interface: **FreeType + Noto Sans nos dois sistemas** | mantenedor, 2026-11-04 |
| A tela de título original continua, com o logotipo e a personagem animados. O menu antigo é **trocado**, não coberto: ele deixa de ser desenhado e o nativo fica no lugar dele | mantenedor, 2026-11-04 |

## 2. Arquitetura

```
 native/ui/identidade.h          a identidade em dados (cores, emblema), a mesma do instalador (builder/instalador/gui)
 native/text/                    Canvas + motor de texto (FreeType + Noto Sans embutida), agora nos dois sistemas
 app/remaster/interface.{h,cpp}  MODELO: telas, navegação, perfis, confirmações, textos pt-BR/en-US (sem janela nem jogo)
 app/remaster/desenho.{h,cpp}    DESENHO: a camada ARGB da saída, composta pela apresentação do jogo (Vulkan ou SDL)
 app/remaster/entrada_titulo.*   PONTOS DE ENTRADA: na thread do jogo (docs/N9-2-PONTOS-DE-ENTRADA.md)
 app/settings_menu.*             as configurações: o MESMO modelo e a mesma persistência do menu interno do N7c
 app/main_sdl.cpp (InGameMenu)   a ligação: entrada (teclado, controle, mouse), fases, reinício, sair
 app/game_host.*                 GameHost::restart (o "Menu principal") e GameOptions::onSessionStart
```

- A interface não depende do Win32: o modelo e o desenho são C++ portátil sobre `native/text`, e só a ligação usa o SDL3.
- A lógica do jogo não muda. Os pontos de entrada levam a máquina de estados do título ao estado que a confirmação
  original produziria, e o jogo segue pelo próprio código.
- A política fica no perfil de edição: `EditionProfile::native_menus` (Remastered).

## 3. Telas

- **Menu inicial**, sobre a tela de título original animada:
  - Novo jogo;
  - Continuar, o perfil válido gravado mais recente;
  - Carregar;
  - Configurações;
  - Extras;
  - Sair.

  Quando o título chega ao "PRESS START", o ponto de entrada abre o menu do título (estado 6, que não cai na
  demonstração). As três rotinas do menu antigo deixam de desenhar: "PRESS START", as opções e a barra de destaque. As
  opções nativas ficam no lugar delas, entre o logotipo e o copyright.
- **Pausa** (Esc ou View durante o jogo), com o jogo parado entre VBlanks:
  - Continuar;
  - Salvar (desativado até o N9.3, "nos pontos de gravação do jogo");
  - Carregar;
  - Configurações;
  - Menu principal;
  - Sair do jogo.
- **Perfis:** os arquivos `BLUESTIN.000` a `.005` do cartão nativo, com o número, a data da gravação (do envelope) e se a
  imagem é válida. Nada de VMU, soquete nem bloco.
- **Confirmação:** Carregar, Menu principal e Sair, a partir da pausa, perdem o progresso, então pedem confirmação.
- **Configurações:** as páginas do menu interno (gráficos, áudio, legendas, controles, câmera conforme a edição), no estilo
  novo. No menu inicial, o último item é "Voltar".
- **Extras:** sobre o projeto e a versão.
- Entrada e saída com fade de 0,25 s, sem corte seco. Teclado (setas, Enter, Esc), controle (D-pad, analógico com
  repetição, A, B, Start, View) e mouse (passar por cima seleciona, clique escolhe, botão direito volta, roda).

## 4. Ações

| Ação | Como |
|---|---|
| Novo jogo | ponto de entrada NEW GAME; a interface sai em fade e o fade original do título aparece |
| Continuar / Carregar (no menu inicial) | ponto de entrada LOAD: a rotina de load do próprio jogo com o arquivo do perfil, sem o painel; "Carregando o perfil N..." até o título terminar |
| Carregar (na pausa) | reinício do jogo e, no título, o mesmo LOAD |
| Menu principal | `GameHost::restart` (o jogo original não volta ao título sozinho); a abertura do jogo aparece de novo e o menu inicial volta no "PRESS START" |
| Sair | fecha a janela |

## 5. Gravação e reprodução

- Cada ação aplicada pelo ponto de entrada é um evento com o passo de jogo.
- Com `BS_GRAVAR`, os eventos vão para `<gravação>.acoes`.
- `BS_REMASTER_ACOES=<arquivo>` reproduz eventos no mesmo passo, e `BS_REMASTER_PEDIR=<ação>[:<arquivo>]` faz o pedido do
  menu sem janela.
- Com um roteiro (`BS_INPUT_SCRIPT`, modo espectador) ou uma gravação-base (`BS_GRAVAR_BASE`), a interface nativa fica
  desligada e o título original segue o roteiro. As sessões gravadas pelo menu original continuam valendo.

## 6. Provas

Ver `STATUS.md` (N9.2) e `docs/N9-2-PONTOS-DE-ENTRADA.md`, seção 4.
