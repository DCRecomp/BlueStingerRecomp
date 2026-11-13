# N9.1: instalador moderno e identidade visual

Segunda subetapa do N9 (`docs/N9-PLANO.md`, seção 5). Autorizada pelo mantenedor em 2026-11-04, com os testes do Linux e do
Bazzite sob a responsabilidade dele (eles não bloqueiam o N9.1 e nenhum critério Linux é marcado como aprovado sem os
resultados dele). O resultado das provas fica no `STATUS.md`.

## 1. Decisões

| Decisão | Origem |
|---|---|
| Windows: Win32 + Direct2D (DirectWrite para o texto, WIC para as capturas) | 12.2, 2026-11-02 |
| O instalador e a interface do jogo (N9.2) compartilham a identidade visual, não o motor | 12.3, 2026-11-02 |
| Artes próprias, com origem e direitos documentados | 12.9, 2026-11-02 |
| A edição Original preserva só o menu original | 12.12, 2026-11-02 (não muda nada aqui: o instalador não é o jogo) |
| **Linux: janela SDL3, texto pelo FreeType + Noto Sans** (os dois já estão no pacote), diálogos pelo portal do desktop | mantenedor, 2026-11-04 |
| **Integração por usuário, sem administrador:** Windows com a entrada em "Aplicativos instalados" (`HKCU\...\Uninstall`) e atalhos no Menu Iniciar (Área de Trabalho opcional); Linux com `.desktop` em `~/.local/share/applications`; lista das instalações na pasta do usuário | mantenedor, 2026-11-04 |

## 2. Arquitetura

O mecanismo continua o `bs_builder_core` (`builder/builder_core.h`): o contrato da linha de comando, do Developer e do
`bs-builder` não muda. O instalador é uma camada de cima, em três partes, sem `#ifdef` no código comum:

```
 builder/instalador/                      portátil (Windows e Linux), sem janela
   instalacoes.{h,cpp}   lista das instalações do usuário, detecção, verificação (SHA-256 do pacote), desinstalação, espaço
   fases.{h,cpp}         etapas do Builder -> as três fases da tela (extração, preparação, instalação), progresso monotônico
   integracao.h          atalhos e registro do desinstalador: integracao.cpp (comum) + integracao_win.cpp | integracao_posix.cpp

 builder/instalador/gui/                  portátil: a identidade visual e as telas
   pintor.h              interface de desenho em DIP (retângulos, cantos, triângulos, degradê, texto, recorte)
   tema.h                cores, medidas, tempos das animações (a identidade, reaproveitável pelo N9.2)
   arte.{h,cpp}          logotipo, emblema e ilustrações das edições em geometria (vetorial: nítido em qualquer escala)
   ui.{h,cpp}            botão, cartão de opção, caixa de marcação, campo de caminho, barra de progresso, foco, medida do texto
   telas.{h,cpp}         as 7 telas e as de manutenção (verificar, desinstalar): escolhas, validação, navegação, a thread do
                         Builder, teclado, mouse, roda
   hospedeiro.{h,cpp}    a linha de comando comum e o modo sem janela (--captura, --teclas), o ícone dos atalhos do Linux

 hospedeiros (o que muda por sistema)
   Windows: pintor_d2d.cpp + janela_win.cpp + main_win.cpp + icone_win.cpp (o .ico na compilação)
            (BlueStinger-Recompiler.exe, BlueStinger-Desinstalar.exe)
   Linux:   pintor_sw.cpp  + janela_sdl.cpp   (BlueStinger-Recompiler, BlueStinger-Desinstalar)
```

- O `pintor_sw` desenha num buffer ARGB pelo `native/text` (FreeType + Noto Sans embutida, o mesmo motor das legendas do
  Linux) e entrega ao SDL3 como textura. O `pintor_d2d` desenha no `ID2D1HwndRenderTarget` e, nas capturas de teste, num
  bitmap WIC.
- As telas medem o texto pelo pintor e registram todo texto que não cabe na caixa. É a prova de "sem cortes" em
  100/125/150/200 % (capturas `--captura`).
- O Recompiler continua aceitando `--disc --out --kit --sdl3 --portatil --edicao --idioma --auto --sem-filmes`, que o
  `test_builder` usa. A janela é que muda.
- Textos pelo catálogo de hoje (`builder/ui`, pt-BR e en-US), com o teste de catálogo completo (`ui_catalog_complete`).

## 3. Telas

1. **Boas-vindas:** idioma, instalações encontradas (com Atualizar, Verificar e Desinstalar) ou Nova instalação.
2. **Edição:** Original ou Remastered, com ilustração e descrição. Na atualização, a edição atual vem marcada.
3. **CDI:** escolha e validação do arquivo antes de seguir (tamanho, formato e a faixa de dados do jogo).
4. **Componentes e idiomas:** legendas (idioma inicial ou desativadas), filmes do disco e modo portátil dos saves. As
   dublagens e os pacotes HD aparecem como "nenhum pacote local encontrado" até existirem (N9.9 e N9.8); a tela já lê
   `mods\` da instalação.
5. **Pasta:** destino, espaço livre e avisos (instalação existente, pasta alheia, troca do local dos saves).
6. **Progresso:** extração, preparação e instalação com a etapa real do Builder; Cancelar; log técnico opcional.
7. **Conclusão:** atalhos (Menu Iniciar, Área de Trabalho), "Jogar agora", "Abrir pasta". Em erro: a mensagem do tipo do
   erro, o caminho do `builder.log` e "Tentar de novo".

Manutenção: **Verificar** (o SHA-256 dos arquivos do pacote e o `--verify-data` dos dados) e **Desinstalar** (só os arquivos
da lista `.bs-package`; saves, preferências, mods do jogador e o que ele pôs na pasta ficam; a pasta só sai se esvaziar).

## 4. Dados pessoais

- Saves: Documentos (padrão) ou `saves/` na própria pasta (modo portátil). Nunca entram na lista do pacote.
- Preferências e log: ao lado do jogo, se a pasta aceitar escrita, senão na pasta do usuário. Fora da lista do pacote.
- Atualização: a troca segura de pastas do Builder (`install_swap`) copia para a versão nova tudo o que não é do pacote.
- Desinstalação: remove os arquivos da lista, os dois arquivos da instalação que ficam fora dela (`instalacao.toml`, escrito
  pelo instalador, e `bluestinger.log`, o log do jogo ao lado do executável) e, se o jogador marcar, o trabalho do Builder
  (`.bs-build`, o cache de compilação). Os arquivos de fora da lista ficam e a tela diz quantos e onde. Um caminho da lista
  que sairia da pasta é ignorado; com o jogo aberto, nada muda.

## 5. Aceitação (`docs/N9-PLANO.md`, N9.1) e como cada item é provado

| Critério | Prova |
|---|---|
| Instalação limpa, atualização e desinstalação nas duas edições, dados pessoais intactos | suíte `tools/dist/test_instalador.ps1`: hash dos saves e das preferências antes e depois |
| Cancelamento em cada fase deixa a instalação anterior utilizável | a mesma suíte: cancelamento em cada fase e o jogo anterior responde e confere os dados |
| Progresso monotônico com as fases reais | unitário das fases e o registro de progresso da suíte |
| 100/125/150/200 % sem cortes | capturas de todas as telas nos dois idiomas e nas quatro escalas, com o relatório de texto cortado vazio |
| Navegação completa só pelo teclado | roteiro de teclas do começo ao fim, sem mouse (`--teclas`) |
| pt-BR e en-US sem chave faltando | `ui_catalog_complete` |

O Linux tem as mesmas telas e as mesmas capturas no WSL. A validação dele em hardware real é do mantenedor.

## 6. Arte e direitos

Toda a arte do instalador é geometria desenhada pelo código (`gui/arte.cpp`) e o ícone gerado dela
(`bs-instalador-icone`, `gui/icone_win.cpp`, na compilação). Autoria do projeto, licença MIT, sem nada do jogo. Origem e direitos de cada peça:
`docs/N9-1-ARTE.md`. Fontes: Segoe UI no Windows (do sistema, não distribuída) e Noto Sans no Linux (OFL 1.1, já no pacote).

## 7. Fora do N9.1

- A interface do jogo (N9.2), os saves de PC (N9.3, depois cancelado), o preset gráfico Remastered (N9.5–N9.8) e os pacotes de dublagem e HD
  (N9.8 e N9.9).
- Detecção de atualização pela rede (N9.10).

## 8. v0.9.7-1: "Adicionar à Steam" (pedido do mantenedor, 2026-11-12)

- **A opção:** uma caixa "Adicionar à Steam" na tela das opções, junto dos atalhos, **desmarcada por padrão**; no `--auto`,
  só com `--integrar --steam` (os testes).
- **O que faz** (`builder/instalador/steam`): o jogo entra como atalho de jogo não-Steam em cada conta da Steam desta máquina
  (`<Steam>\userdata\<conta>\config\shortcuts.vdf`, a pasta `0` e as que não são números ficam de fora), com o nome da edição
  ("Blue Stinger" ou "Blue Stinger Remastered"), o executável, a pasta e o ícone do jogo, e o `appid` que a Steam calcula
  (CRC-32 do executável e do nome, com o bit alto).
- **O formato:** o binário de chave e valor da própria Steam, lido e gravado pelo projeto (sem dependência nova). Um arquivo
  que não se lê no formato esperado nunca é reescrito (o aviso diz qual). Cada gravação guarda a versão anterior em
  `shortcuts.vdf.anterior-bluestinger` e troca o arquivo de uma vez. As entradas de outros programas ficam byte a byte.
- **Sem duplicar:** a entrada do mesmo executável é atualizada (a reinstalação na mesma pasta), preservando o que a Steam
  guardou nela (tags, tempo de jogo).
- **A desinstalação** tira as entradas cujo executável está na pasta da instalação.
- **A Steam:** Windows, `HKCU\Software\Valve\Steam\SteamPath`; Linux, `~/.steam/steam`, `$XDG_DATA_HOME/Steam`
  (`~/.local/share/Steam`) e o Flatpak. Sem a Steam, a instalação termina com o aviso "Steam não encontrada". Com a Steam
  aberta, o jogo aparece depois de reiniciá-la (o aviso diz isso).
- **Testes:** `bs_instalador_tests` (`instalador_steam_formato_e_entradas`, `instalador_steam_desviada`), com a Steam
  desviada por `BS_INSTALADOR_TESTE_RAIZ` para `<raiz>\steam`: os testes nunca tocam na Steam real.
