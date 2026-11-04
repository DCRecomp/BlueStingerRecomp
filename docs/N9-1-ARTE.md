# N9.1: arte e identidade visual do instalador

Decisão 12.9 (2026-11-02): artes próprias, com origem e direitos documentados. Decisão 12.3: o instalador e a interface do
jogo (N9.2) compartilham esta identidade, não o motor.

## Peças

| Peça | Onde | Origem | Direitos |
|---|---|---|---|
| Emblema (o ferrão estilizado: corpo âmbar com faixas, quatro asas verde-água) | `builder/instalador/gui/arte.cpp`, `emblema` | desenhado em geometria (triângulos) pelo projeto, em 2026-11-04 | autoria do projeto, MIT (`LICENSE`) |
| Logotipo (emblema + "BLUE STINGER" + "Recompiled") | `arte.cpp`, `logotipo` | emblema + texto na fonte do sistema | MIT. "Blue Stinger" é usado só para identificar o jogo (marca do titular, `AVISOS.txt`) |
| Ilustração da edição Original (tela 4:3, sol e serra em blocos, linhas de varredura) | `arte.cpp`, `ilustracao_original` | geometria do projeto | MIT |
| Ilustração da edição Remastered (tela 16:9, sol com brilho, serras em camadas, reflexo) | `arte.cpp`, `ilustracao_remastered` | geometria do projeto | MIT |
| Fundo da faixa lateral, símbolos de visto, erro e alerta | `arte.cpp` | geometria do projeto | MIT |
| Ícone do instalador e do desinstalador (`.ico` de 16 a 256 px) | gerado na compilação por `bs-instalador-icone` (`builder/instalador/gui/icone_win.cpp`) a partir do emblema | derivado do emblema | MIT |
| Ícone dos atalhos no Linux (PNG 256 px na pasta do usuário) | gerado pelo instalador na integração (`janela_sdl.cpp`, `icone_atalho`) | derivado do emblema | MIT |
| Cores, tamanhos, tempos das animações | `builder/instalador/gui/tema.h` | projeto | MIT |

Nenhuma imagem binária fica no repositório: tudo é desenhado pelo código, nítido em qualquer escala. Nada vem do jogo (capa,
logotipo, personagens, texturas, telas). A arte do jogo continua só local, a partir dos arquivos do próprio jogador (o ícone
de `icone-local/`, 2026-10-23), e não entra no instalador.

## Fontes

| Sistema | Fonte | Distribuição |
|---|---|---|
| Windows | Segoe UI, pelo DirectWrite | do sistema; não vai em nenhum pacote |
| Linux | Noto Sans Regular e SemiBold 2.015, pelo FreeType | embutida no executável (já estava no pacote desde a L4); SIL OFL 1.1, texto em `third_party/fonts/noto-sans` |

## Identidade

- Fundo azul-noite (`#0B111C`), faixa lateral com dégradé e dois triângulos discretos.
- Âmbar (`#F4A637`): ação principal, edição Original, foco.
- Verde-água (`#3CC6B8`): edição Remastered, sucesso.
- Coral (`#F06A5B`): erro e ações destrutivas (desinstalar).
- Animações curtas (0,12 a 0,18 s): entrada da tela, realce do botão sob o mouse, marcação do cartão, brilho da barra de
  progresso. Desligadas quando o Windows pede menos animação (`SPI_GETCLIENTAREAANIMATION`) ou com `BS_MENOS_ANIMACAO=1`.
