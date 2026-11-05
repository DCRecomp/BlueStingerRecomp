# N9.2: pontos de entrada nativos da interface Remastered

Decisão do mantenedor (2026-11-04): na Remastered, o menu nativo aciona a lógica do jogo por **pontos de entrada
nativos**. Ele não usa entrada sintética sobre o menu original escondido. A Original não muda. Este documento registra o
que foi achado no binário (`1ST_READ.BIN`, T13001N V1.000) e como cada ponto foi provado.

Método:
- **Estático:** literais e `bsr` no binário, `bs-disasm` e `analysis/functions.tsv`.
- **Dinâmico:** gameplay assistido headless (`tools/dev/assistido.py`), com a RAM inteira gravada em passos escolhidos do
  roteiro do título (`dump`), comparada byte a byte.

## 1. Módulo do título

- Tabela do módulo em `0x8C13B35C`, com cinco rotinas: `8C078448`, `8C078514` (atualização), `8C07862E`, `8C0786E0` e
  `8C078760`. A tabela é referenciada por `8C0784CC`. A atualização é chamada por ponteiro, então `mods::replace` vale
  também no build do produto, que não tem os slots de mods.
- `8C078514` chama `8C078AC0` (`bsr` em `8C07851E`) e a máquina de estados `8C078FC0` (`bsr` em `8C07852A`).
- **Estrutura do título** em `0x8C24D2CC` (o `r14` de `8C078FC0`):

  | Campo | Endereço | Uso |
  |---|---|---|
  | +0x51C | `8C24D7E8` | estado (`u16`), 17 casos (tabela em `8C07903C`, `braf` a partir de `8C07901A`) |
  | +0x51E | `8C24D7EA` | cursor do menu (`u16`): 0 NEW GAME, 1 LOAD GAME, 2 OPTIONS |
  | +0x520 | `8C24D7EC` | contador de fade (`u16`) |

  Prova na RAM: entre o passo 985 (NEW GAME), o 998 (LOAD GAME) e o 1008 (NEW GAME), só o cursor muda (0, 1, 0). Depois do
  START no passo 1011, o estado vai de 6 para 7.

- **Estado 6, menu à espera** (`8C079166`). Lê as bordas dos botões em `(*r10)+16`:
  - bit `0x2` (voltar): efeito `8C02A6B8(60, 0, 0)` e estado 5;
  - bits `0xC` (confirmar): efeito `8C02A6B8(90, 0, 0)` e depois o cursor decide:
    - **0, NEW GAME:** `8C07B2A0(0)` e, em `8C079430`: estado = 7, `u8 [8C24D2AA]` = 1, `u16 [8C24D29E]` = 8,
      `u16 [8C24D2A0]` = 0x00FF, `f32 [8C24D298]` = -1,0;
    - **1, LOAD GAME:** estado = 8 e +0x520 = 0;
    - **2, OPTIONS:** estado = 13, com +0x520, +0x522 e +0x524 zerados.

  A RAM confirma as escritas do NEW GAME: `8C24D2AA` vai de 0 a 1, `8C24D29E` de 4 a 8 e `8C24D2A0` de 0 a 255.
- **LOAD GAME (estados 8 a 12):**
  - 8: fade até +0x520 = 0xC0, depois estado 9;
  - 9: `8C03BAE0(0)` e o painel de saves `8C01D200(1, 8)`; acompanha a situação do painel, um `u16` em `8C1F3B40`:
    - 0: estado 10;
    - 7: estado 12 se `u8 [8C24F94B]` = 4, senão 11. Depois `8C064700()` é comparado com `[8C24FDE8]` para escolher
      entre 11 e 12.

## 2. Painel de saves e load

- **Painel** `8C01D200(modo, 8)`:
  - modo 2 no boot (título, conferência) e 1 no LOAD do título;
  - se a situação `u16 [8C1F3B40]` for 0 ou 7, inicia em `8C01D260`;
  - o modo `[8C1F3BA6]` = 2 vai para `8C01D570`, senão para `8C01D3A0` (LOAD);
  - fase interna em `u16 [8C1F3B42]`, código da mensagem em `u8 [8C1F3BAC]`, soquete em `u16 [8C1F3BA4]`;
  - a situação mostrada sai do código da mensagem: 1, 2 e 3 dão 4; 20, 21 e 29 dão 5; 30 e 39 dão 6; o fim
  (`8C01D520`) grava 7 se a fase for 1.
- **Telas** (quadros do gameplay assistido, `n9-2/ram2/mosaico.png`): LOAD, Socket 1 e 2, a lista dos blocos 1 a 6
  (`BLUESTIN.000` a `.005`, com nome, tempo de jogo e dinheiro) e "Do you want to Load this data? Press the A button to
  confirm".
- **Rastreio** (`BS_CALL_TRACE` no build `msvc-release-mods`, roteiro `passo_load_slot1`, save `g1` importado):
  - a lista lê os cabeçalhos com `8C01CB18(drive 0, arquivo n, 0x22)`, chamado por `8C01DDBC` a cada quadro até
    terminar; o cabeçalho de `BLUESTIN.000` vai para `0x8C1F385C` (`8C01C4AE(0, nome, 0x8C1F385C, 1)`);
  - o **load** é `8C01CE00(drive 0, arquivo 0, 0x22)`, chamado por `8C01DE7C`. Ele chama
    `8C01C4AE(0, "BLUESTIN.000", 0x8C24F8BC, 1)`: **o save vai direto para a área de estado do jogo, em `0x8C24F8BC`**;
  - depois do load, `[8C24F94B]` = 4 (o `F94B` está dentro da área carregada);
  - com o painel em 7, o estado 9 do título escolhe entre 12 e 11: primeiro pelo `[8C24F94B]` = 4, depois por
    `8C064700()` = `[8C24FDE8]` (12) ou diferente (11).
- **Ponto de entrada nativo do LOAD (proposta):** sem o painel. A atualização do título, envolvida, chama a mesma rotina
  `8C01CE00(0, arquivo, 0x22)` que o painel usa, a cada quadro até terminar, e depois aplica a decisão do estado 9.
  Falta medir os valores de retorno de `8C01CE00` (ocupado, concluído, erro).

## 3. Estados do título desde o boot e partida

- Sem entrada, o título passa por 0, 1, 2 e 5 ("PRESS START"). Depois de uns 900 passos em 5, vai para 16 (a demonstração)
  e recomeça do 0. Amostras a cada 50 passos, `n9-2/ram3`.
- **Estado 5** (`8C079148`): com a borda `0x8` (START), toca o efeito `8C02A6B8(90, 0, 0)` e vai para o estado 6
  (`8C0795CE`).
- **Estado 11:** diminui +0x520 de 4 em 4 e, quando chega a 0, volta ao estado 6. É a volta ao menu quando o save não
  serve. **Estado 12:** segue para a partida carregada.
- **`8C01CE00`**, máquina de estados com estado em `u32 [8C1F3858]`, contador em `[8C1F3854]` e bandeira
  `u8 [8C1F3A60]`:
  - 0 → 11 → 7: espera uns quadros, monta o nome `BLUESTIN.00n` (`8C01C3C0`, com o índice em r5) e começa
    `8C01C4AE(drive, nome, 0x8C24F8BC, 1, 0)`;
  - 8: consulta `8C08C696` até terminar.

  Devolve 7 enquanto trabalha e 8 ao concluir.
- **Partida:** `main` (`8C0100AC`) chama a cena do título `8C078400` uma vez (literal em `8C010150`).
  - A cena roda a tabela do módulo, em que cada rotina devolve o índice da próxima, até receber 5.
  - Depois salta para `8C07BFC4`, que chama `8C173E6A` e `8C08BC92` e salta para `8C15E9C4`, o gameplay.
  - Não há literal nem desvio de volta para a cena do título, então o jogo não volta ao título sozinho. O
    **"Menu principal"** da Remastered é um reinício do jogo dentro do processo, que tem de dar os mesmos passos e o
    mesmo estado de um boot novo (prova pendente).

## 4. Implementação e provas (2026-11-04)

Código: `app/remaster/entrada_titulo.{h,cpp}`, instalado na Remastered a cada sessão (`GameOptions::onSessionStart`,
política `EditionProfile::native_menus`).
- A espera do VBlank `8C064A20` é envolvida por `mods::replace`, que no produto vale para as chamadas indiretas; a do laço
  do título (`jsr @r11` em `8C078532`) é uma delas.
- O módulo só age quando o retorno é `8C078536`, uma vez por quadro do título. Chama as rotinas do jogo como o `BS_CALL`
  do código gerado (`pr` e `ret_expect` coerentes) e restaura os registradores do chamador.
- Testes sem janela (build de validação, Preview v0.8.0 com `instalacao.toml` em Remastered, `n9-2/e2e`):

| Prova | Resultado |
|---|---|
| Reinício do jogo no processo (`GameHost::restart`, `BS_REINICIO_TESTE=1500`) × processo novo, `BS_STATE_PASSO` passo a passo | 1480 passos **idênticos** |
| NEW GAME nativo (eventos `794 menu`, `994 novojogo`) × entrada original (`passo_newgame_area1`: START em 793 e 993), mesmo roteiro para os logos | 1031 passos **idênticos** (estado, vida, posição); filme `NEWGAME.SFD` no passo 1030 nos dois |
| O mesmo com os eventos no passo da entrada original (793 e 993) | um passo à frente: o evento do passo S equivale à entrada lida no passo S−1. A gravação nativa registra o passo aplicado, então a reprodução fica igual |
| LOAD nativo (`803 carregar 0`, save `g1`) × LOAD original pelo painel (`passo_load_slot1`) | o mesmo ponto (126,853; 0; −560,417), vida 40, 74 passos depois do pedido (contra 1755 pelo menu). O cartão nativo registra 1 LOAD (o painel faz 3: dois cabeçalhos e o save) |
| RAM da área de estado (`8C240000`–`8C260000`), 100 passos depois de o jogador aparecer | original × original com a confirmação 300 passos depois: 20 bytes (`8C24D690`–`8C24D6FF`, dependentes do tempo). Original × nativo: 23 bytes, nos mesmos trechos **mais** os contadores de botão `8C24C5EC`–`8C24C5FD` (o A da confirmação, que o caminho nativo não aperta) |

Conclusão: o NEW GAME nativo é idêntico ao original. O LOAD nativo dá o mesmo estado de jogo que o LOAD original, a menos
dos campos que já mudam entre dois loads originais com tempos diferentes e dos contadores do botão de confirmação.

## 5. Pendente de análise
- A volta ao título a partir do gameplay ("Menu principal").
- O estado 7 em diante do NEW GAME, até o filme `NEWGAME.SFD` (passo 1030 no roteiro).
