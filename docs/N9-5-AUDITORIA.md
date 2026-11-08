# N9.5: auditoria gráfica e plano do widescreen verdadeiro e do HUD

Autorizado pelo mantenedor em 2026-11-05: **só a auditoria e o planejamento**. O código funcional não muda antes da aprovação
da arquitetura. Escopo oficial em `docs/N9-PLANO.md`, N9.5: 16:9 verdadeiro (21:9 só avaliado), projeção e campo de visão,
HUD independente da resolução, legendas e menus em áreas seguras, geometria fora do enquadramento verificada, alternância
Original/widescreen. Os pré-requisitos vêm do `docs/N7-PLANO.md`, seção 25.2.

Legenda: **[P]** provado (com o método), **[H]** hipótese, **[N]** ainda não observável sem uma ferramenta nova.

## 1. Como a imagem é feita hoje

```
 jogo recompilado (Ninja/Kamui do SDK)  --SQ/DMA-->  transitional/render_capture  -->  IR (native/render/render.h)
   projeção feita pelo jogo: o TA já          CommandList: desenhos com vértices em tela (x, y em 640x480,
   recebe x, y de tela e 1/w                  1/w), estado (lista, blend, textura, fog), volumes modificadores
                                                         |                               |
                                     native/render/vulkan (padrão)        native/render/soft (referência, recuo)
                                     alvo 640e x 480e (e = altura/480)    640 x 480
                                                         |
                       frame_prep: redução ao quadro 640x480 do jogo (capturas, hash) + o slot grande na GPU
                                                         |
                       app/window_present: o quadro na área 4:3 da janela (game_area) + camadas na resolução da
                       janela (legendas, menu interno N7c, interface Remastered N9.2)
```

- **[P] A projeção é do jogo, não do renderizador.** O TA do Dreamcast só recebe coordenadas de tela, e a IR as guarda
  assim (`render.h`; `docs/N7-PLANO.md`, seções 6 e 19). O Vulkan e o software desenham o que o jogo já projetou. Para ter
  16:9 verdadeiro, o jogo precisa projetar mais largo. Esticar ou recortar no renderizador não é widescreen verdadeiro.
- **[P] Resolução interna** (N7c): de 480 a 2160 linhas (`internal_height`, limitado por `max_internal_height` da GPU),
  escala de 50 a 200 %. As posições, os recortes e a tesoura escalam juntos, e o 1/w não muda. Na escala 1, o resultado é
  igual bit a bit ao software. A apresentação direta pelo Vulkan (caminho B) foi validada de 1080p a 4K, em janelas 4:3,
  16:9 e 21:9 (STATUS, N7c). Hoje 16:9 = **4:3 com faixas laterais**.
- **[P] O quadro lógico tem 640×480 fixos:** `FrameParams` usa `width = 640, height = 480` (`render.h:103`), e a área do jogo
  na janela é 4:3 (`window_present.cpp`, `game_area`). O quadro reduzido de 640×480 alimenta as capturas, o hash dos quadros
  e as comparações com as referências do N8.0 e do N8.7.
- **[P] Filmes:** o player nativo (N5) entrega o quadro do filme como imagem da CPU, mostrada na mesma área 4:3. Legendas,
  menus e a interface da Remastered são camadas da janela, na resolução da janela, posicionadas pela área do jogo. Já
  funcionam em qualquer proporção.
- **[P] O software e o Vulkan desenham a mesma IR.** O software é o recuo (sem Vulkan, ou depois de uma perda de
  dispositivo) e a referência de fidelidade.
- **[P] Windows e Linux** passam pelo mesmo caminho: IR, Vulkan e camadas pelo `native/text`. Só a janela e o texto das
  legendas no Windows (DirectWrite) mudam. Nada no 16:9 depende do sistema [H, até medir].

## 2. A projeção do jogo

- **[P] Estrutura de tela do Ninja** (`NJS_SCREEN`) em `0x8C24CCB4`: distância 500, largura 640, altura 480, cx 320, cy 240.
  - Foi achada na RAM de gameplay (LOAD do slot 1, corredor RCG02). Fica logo depois do objeto câmera do SDK (`0x8C24CC38`,
    0x78 B, o que a câmera Livre do N7c já usa).
  - Campo de visão: 2·atan(320/500) ≈ 65,2° na horizontal e 2·atan(240/500) ≈ 51,3° na vertical.
  - Há outras cópias com a distância 500 e alturas de 480 e 450 em `0x8C2D96xx` e `0x8C2D9C7x`. Na auditoria a hipótese era
    "telas de outros contextos"; a fase 0 mostrou que **não**: `0x8C2D9C70` é a cópia que o desenho usa, e `0x8C2D96xx` são
    os blocos de parâmetros montados por desenho a partir dela (seção 11).
- **[P] Quem referencia `0x8C24CCB4`:** 9 rotinas, entre elas o laço de gameplay `8C052240`, `8C0584CC`, `8C062E88`,
  `8C063530`, `8C064560`, o título (`8C078AC0`, `8C07AD80`) e `8C08A1CC`. O campo seguinte (`0x8C24CCC8`, um ponteiro) tem 72
  referências. `8C064560` e `8C08A1CC` projetam pontos isolados (distância × coordenada ÷ profundidade): é o uso de
  marcadores ou de testes de "na tela" [H sobre a finalidade].
- **[P] Experimento** (`BS_TELA_TESTE=854,427`, só no build de validação: reescreve a largura e o cx da estrutura a cada
  VBlank):
  - LOAD do slot 1 e caminhada no corredor, RAM e quadro comparados nos passos 1855 e 2300;
  - **a área de estado do jogo é idêntica** (0 bytes, fora a câmera e a tela);
  - **o quadro também é idêntico**.

  Conclusão da auditoria: o desenho dos modelos **não lê** a largura nem o cx a cada quadro. A fase 0 achou o motivo (abaixo):
  a hipótese "matriz do `njSetScreen`" estava **errada na forma** (não há matriz; há uma cópia de três números) e certa no
  efeito (constantes copiadas antes). Fica registrada como hipótese descartada.
- **[P, fase 0] A projeção do desenho** (seção 11): o `njSetScreen` é `8C1741D4` e copia distância, cx e cy para
  `0x8C2D9C70`; cada desenho de modelo lê dali. Isso resolve o B1.

## 3. Elementos 2D e 3D (mapa)

| Elemento | Quem desenha | Hoje | No 16:9 |
|---|---|---|---|
| Cena 3D (salas, personagens, inimigos, partículas 3D) | jogo → TA → IR | projetada a 640×480 | projeção mais larga (hor+) |
| HUD do jogo (moldura do alvo, "ELIOT" e a barra de vida, ícones, mensagens, prompts de interação) | jogo → TA → IR, em coordenadas 640×480 | dentro da imagem 4:3 | ancorado nas bordas ou centrado, sem esticar: exige a **marca cena/HUD por desenho** (B3) |
| Menus do jogo original (título da Original, inventário, mapa, opções, painel de saves) | jogo → TA → IR | 4:3 | **na Original, sem mudança.** Na Remastered, centrados com faixas (pilares), a não ser que a marca de HUD diga outra coisa |
| Legendas, menu interno N7c, interface N9.2 | camadas da janela (`native/text`, D2D/DWrite das legendas no Windows) | posicionadas pela área do jogo | já independentes: só a área de referência muda |
| Filmes (N5) | player nativo, imagem da CPU | 4:3 | 4:3 com pilares (o plano pede a proporção original) |
| Transições (fades e escurecimentos) | jogo → IR (polígonos de tela cheia) [H] | 640×480 | precisam cobrir a largura nova (B3/B4) |

- **[P] Identificação de texturas para mods** (contrato do N7, `native/render/texture_replacement.h`): `TextureId` =
  dimensões + FNV-1a de 64 bits dos texels decodificados, independente do endereço na VRAM e do formato. As UVs não mudam
  (fator inteiro), e há recuo ao original. Nada é carregado ainda: isso é do N9.8 e não depende do N9.5.

## 4. Bloqueadores

A situação de cada um depois da fase 0 está na seção 11.7.

| | Bloqueador | Por que bloqueia | Como resolver (proposta) |
|---|---|---|---|
| B1 | Onde está a projeção do desenho | sem isso não há hor+ | rastrear `njSetScreen` e a matriz de projeção. Ponto de entrada nativo **só na Remastered** que chama o `njSetScreen` do próprio jogo com a largura 16:9 e o mesmo campo vertical (dist e cy mantidos), como os pontos do N9.2 |
| B2 | Recorte e descarte do jogo nas laterais | geometria fora do 4:3 pode não existir (buracos, objetos que surgem) | medir pelas capturas da área nova, sala por sala. Onde o jogo não envia geometria, pilares ou recorte nessa sala (o plano já prevê). O recorte do Ninja pela tela entra junto com o B1 |
| B3 | Separar cena de HUD na IR | o HUD não pode esticar nem ficar preso ao 4:3 | marca por desenho na captura: a rotina do jogo que emitiu (o `cur_fn` e o chamador no envio ao TA) mais a tabela das rotinas de 2D do Ninja e do HUD, achadas por RE e conferidas por captura. O pré-requisito já estava registrado no N7 |
| B4 | Quadro lógico de 640 de largura | IR, tesoura, redução e capturas assumem 640 | na Remastered 16:9, um quadro lógico mais largo (854×480 a 480 linhas) só no Vulkan e na apresentação. O quadro de 640×480 que o jogo e o hash veem continua o recorte central, de forma determinística. O software segue em 4:3 como recuo (B5) |
| B5 | Software sem 16:9 | o recuo precisa existir | o software fica em 4:3 com faixas. Sem Vulkan, a Remastered volta ao 4:3 e registra no log |
| B6 | Fidelidade dos estados | a lógica pode usar a projeção (testes "na tela") | o estado por passo da Remastered 16:9 igual ao da 4:3 em todas as rotas (como o `BS_STATE_PASSO_SEM_CAMERA` do N7c). O experimento do item 2 já deu 0 diferença num trecho, mas falta o hor+ de verdade |

## 5. Plano incremental (cada fase com prova antes da próxima; nada começa sem aprovação)

1. **Fase 0, projeção (B1 e B6), só investigação:**
   - achar o `njSetScreen` e as constantes do desenho;
   - um diagnóstico de validação que chama a rotina do jogo com 16:9;
   - capturas sem janela da área nova;
   - estado por passo comparado nas rotas do `test_remaster`, do `newgame_area1` e do combate.
   - **Saída:** projeção hor+ provada e a lista das salas com buracos.
   - **Feita em 2026-11-05** (seção 11): a projeção hor+ foi provada em seis cenas, com o estado igual. Ficam em aberto as
     faíscas do cais e a cobertura de mais salas.
2. **Fase 1, quadro largo (B4 e B5):**
   - o Vulkan e a apresentação aceitam a largura lógica 854 (×e);
   - o software e os filmes ficam em 4:3;
   - as camadas usam a área nova;
   - na Original e no 4:3, nada muda (pixel a pixel).
3. **Fase 2, marca cena/HUD (B3):** tabela das rotinas 2D e marca por desenho. Até a fase 3, o HUD fica centrado num 4:3
   virtual.
4. **Fase 3, HUD e menus:**
   - âncoras (cantos, centro) para cada elemento do HUD, em proporção original;
   - menus e transições do jogo centrados com pilares;
   - legendas e interface N9.2 na área segura.
5. **Fase 4, as bordas (B2):**
   - salas sem geometria nas laterais: pilares ou recorte por sala;
   - cenas roteirizadas e de câmera fixa: tratamento próprio.
6. **Fase 5:**
   - alternância 4:3/16:9 no menu da Remastered, com recuo para 4:3;
   - medições de 1080p, 1440p e 4K;
   - 21:9 avaliado, sem entrar no escopo obrigatório.

## 6. Critérios de aceitação (verificáveis)

| Critério | Prova |
|---|---|
| Original inalterada | referências do N8.0 (16/16) e do N8.7 (16/16), pixel a pixel no software e nas regras de hoje; C8; `test_edicao` |
| Remastered em 16:9 verdadeiro, sem distorção | capturas do próprio jogo: um círculo de referência na cena (bocal, roda, prato) com a mesma razão de eixos em 4:3 e 16:9; o campo vertical igual (dist e cy), o horizontal = 2·atan(427/500) ≈ 81,0° |
| Sem buracos nas laterais | capturas da área nova por sala nas rotas de gameplay, sem pixels do fundo de limpeza onde o 4:3 tinha geometria. As salas com falha listadas, com pilares |
| HUD proporcional | os elementos do HUD com o mesmo tamanho em pixels que no 4:3 da mesma altura, ancorados nas bordas certas, em 16:9 (e 21:9 só avaliado) |
| Filmes e menus | filmes em 4:3 com pilares; os menus do jogo centrados; a interface N9.2 e as legendas intactas (`bs_remaster_tests`, sem cortes) |
| Controles e câmera | os roteiros por passo e a câmera Livre sem mudança: `test_edicao`, C8 com Livre + Vulkan |
| Fidelidade dos estados | o estado por passo da Remastered 16:9 = Remastered 4:3 (fora a câmera e a tela) nas rotas do `test_remaster`, no `newgame_area1` e no combate |
| Desempenho | tempo por quadro (média, máximo, acima de 16,7 ms) e VRAM por resolução, em 1080p, 1440p e 4K, na NVIDIA e na AMD integrada. **Medições pendentes:** nenhuma foi feita nesta auditoria |
| Windows e Linux | as mesmas capturas sem janela nos dois (no WSL; o Bazzite com o mantenedor) |

## 7. Matriz de impacto nas regressões

| Mudança | Toca | Roda |
|---|---|---|
| Ponto de entrada da projeção (só Remastered) | o título e o gameplay da Remastered | `test_remaster`, estado por passo 16:9 × 4:3, C8 (as duas edições) |
| Quadro lógico largo (Vulkan e apresentação) | o renderizador e a janela | referências do N8.0 e do N8.7 (Original), `n7_janela.py`, `n7_resolucao.py`, `test_gameplay` |
| Marca cena/HUD na captura | `transitional/render_capture` | referências do N8.0 e do N8.7 (a imagem não pode mudar), `bs_render_tests` |
| HUD, menus e camadas | a apresentação e a interface | `bs_remaster_tests`, `n7_menu_pausa`, `test_edicao` |

As baterias longas só rodam quando a mudança as toca (impacto antes de regressão). Os comparadores não mudam.

## 8. Memória e custo (cálculo, não medição)

- **Alvo do Vulkan:** cor RGBA8 + profundidade de 32 bits, 8 B por pixel.
  - 4:3 em 2160 linhas (2880×2160): ≈ 49,8 MB.
  - 16:9 em 2160 linhas (3840×2160): ≈ 66,4 MB, ou seja, +33 %.
  - 1080p 16:9: ≈ 16,6 MB.
  - Mais os slots de apresentação (anel do N7c), na mesma proporção.
- **Cena:** o hor+ desenha mais geometria nas laterais. O custo depende das salas, então é medição pendente.
- **CPU:** a captura da IR não muda de tamanho por pixel, só pelo número de polígonos enviados.

## 9. Riscos

- A lógica do jogo usa a projeção em testes de visibilidade (alvo, ativação de inimigos, gatilhos de câmera). Seria uma
  mudança de gameplay, por isso o critério de estado por passo é eliminatório.
- Geometria descartada pelo jogo nas laterais (salas montadas para o 4:3): buracos. A mitigação é pilares por sala.
- Efeitos de tela cheia em 640: fades, filtros e sobreposições que não cobrem a largura nova.
- A AMD integrada (pendência do N7) com o alvo maior: a perda de dispositivo continua possível. O recuo é o software, que
  fica em 4:3.
- GPUs antigas: o limite de resolução continua o `max_internal_height`, e o 16:9 só aumenta a largura.
- **Recuo para 4:3:** sempre disponível. É o padrão da Original e uma escolha na Remastered.

## 10. Dependências com N9.6–N9.8 (não antecipadas)

- **N9.6, iluminação e materiais, e N9.7, água e reflexos:** usam a projeção nova (a profundidade e o SSAO no alvo largo)
  e a profundidade guardada depois do render pass (N7, seção 25.2). O N9.5 só deixa o alvo largo; a profundidade fica para
  o N9.6.
- **N9.8, texturas HD e AA:**
  - o contrato do `TextureId` já existe;
  - o AA (MSAA) depende do alvo do N9.5;
  - os manifestos, o cache e os pacotes são do N9.8.

  Nada disso entra no N9.5.

## 11. Fase 0: a projeção hor+ (resultados, 2026-11-05)

Autorizada pelo mantenedor em 2026-11-05: **só a fase 0**, investigação e prova da projeção, no build de validação. O
executável de distribuição não mudou (prova em 11.4). Evidências em `%LOCALAPPDATA%\BlueStingerRecomp\n9-5\fase0`, `fase0c`
e `fase0m`.

### 11.1 A rotina que projeta (B1)

- **[P] `njSetScreen` = `8C1741D4`.** Copia três campos da estrutura de tela (`0x8C24CCB4`): distância (+0), cx (+12) e cy
  (+16), para a **cópia do desenho** em `0x8C2D9C70` (+0, +12, +16). A largura e a altura (+4, +8) **não** são copiadas.
  Chamadores:
  - o título (`8C078AC0`, `8C07AD80`): grava dist, w, h e cx = w/2, cy = h/2 e chama;
  - o laço de gameplay (`8C052240`): só quando o bit 2 de uma flag está ligado, aproxima a distância de um alvo em
    `8C2501F0` a 1/16 por quadro (o zoom);
  - `8C062E88`;
  - `8C063400`/`8C0634C0`: desenha numa textura 256×256 com o centro em 128,128 e depois **recalcula cx = w/2 e cy = h/2
    pela largura da estrutura** e chama de novo.
- **[P] Por que reescrever a estrutura não mudava o quadro** (o experimento da seção 2): o desenho lê a cópia, e a cópia só
  muda quando o jogo chama o `njSetScreen`, o que é raro. A hipótese "matriz de projeção" foi **descartada**: não há matriz
  de tela, há a cópia de três números.
- **[P] O desenho por modelo:** as rotinas de desenho do Ninja (`8C15F856`, `8C15F8B8` e as irmãs) montam a cada chamada um
  bloco de parâmetros em `0x8C2D9654`:
  - +16 = dist·`[8C2D9CA4]` e +20 = dist·`[8C2D9CA8]` (`9CA4`/`9CA8` são as escalas de aspecto, 1,0);
  - +24 = cx e +28 = cy.

  Depois chamam o percurso do modelo `8C15F078`, que passa esse bloco à transformação por vértice. Há quatro blocos assim
  (`8C2D966C`, `96C0`, `9744`, `9770`), e todos passaram de 320 para 427 no experimento.
- **[P] A fórmula.** O `njProjectScreen` (`8C170A54`) faz a mesma conta para um ponto:
  - `FTRV` pela matriz corrente;
  - `sx = cx + dist·ax·x/z` e `sy = cy + dist·ay·y/z`, com dist e cx da cópia `8C2D9C70` e ax, ay de `9CA4`/`9CA8`.

  A prova dinâmica abaixo (todo vértice 3D anda exatamente Δcx) confirma que a transformação por vértice segue a mesma
  forma.
- **[P] `njClipZ` = `8C174098`**: perto e longe, limitados a −1 e −65535, gravados negados em `8C2D9C9C`/`9CA0`. O recorte é
  em profundidade, não pelas laterais da tela.
- **O caminho até a imagem:**
  1. rotina do jogo → `njSetScreen` (a cópia);
  2. desenho do Ninja (bloco por desenho, vértices em coordenadas de tela);
  3. listas do TA → `transitional/render_capture` (a IR de `render.h`: x e y em pixels de tela, 1/w);
  4. software ou Vulkan. O renderizador não projeta nada.

**Hor+ verdadeiro:**
- dist e cy mantidos (o campo vertical não muda);
- largura lógica 854 a 480 linhas;
- **cx = 427** na cópia do desenho e na estrutura (largura 854, por causa do recálculo de `8C0634C0`).

O campo horizontal fica 2·atan(427/500) ≈ 81,0°.

### 11.2 Ferramentas (só validação)

| Ferramenta | O que faz |
|---|---|
| `BS_TELA_TESTE=<largura>,<cx>[,<cx_copia>][,estrutura]` | a cada VBlank, a largura e o cx da estrutura e o cx da cópia do desenho; `,estrutura` reproduz o experimento da seção 2 (só a estrutura) |
| `BS_TELA_LOGICA43=1` ou `=<end>[,<end>]` | roda rotinas do jogo com a projeção do 4:3 (a cópia e a estrutura voltam a 320/640/320 só durante a chamada). `1` = o teste "na tela" `8C05D1C0` e o pan do som `8C064560`. Precisa das chamadas diretas por slot (`msvc-release-mods`) |
| comando `ir <nome>` do gameplay assistido | grava a IR do último render no passo pausado (formato do `BS_RENDER_IR_GUARDAR`) |
| `bs-ir-tela` (`tools/ir_tela`) | `quadro` desenha a IR em software numa largura escolhida (BMP); `medir` dá os limites dos vértices; `comparar` alinha os desenhos de duas IRs e classifica cada um (lista abaixo) |

Como o `bs-ir-tela comparar` trabalha:
- **Pareamento:**
  - primeiro pela ordem de envio (LCS sobre lista, número de vértices e textura);
  - o que não fecha é pareado de novo pela geometria, exigindo o mesmo estado de desenho. Numa sequência com a mesma
    assinatura, um desenho a menos desalinha os seguintes.
  - Cada par no TSV diz por onde foi pareado e se havia candidato ambíguo.
- **Classes:**
  - **cena:** o mesmo desenho, com todo vértice andando dx em x (no arredondamento do float) e igual em y, 1/w, u, v e cores;
  - **tela:** o mesmo desenho sem mudar nada (2D em coordenadas de tela);
  - **só no 4:3 / só no largo:** enviado por um dos dois.

### 11.3 Experimento controlado

- **Mesma entrada, mesmos passos, só dados sintéticos.** Duas rotas:
  - o cartão de teste `n9-2/saves1` (importado do save `g1`, gerado pela sessão roteirizada do N6, o mesmo da suíte C8) com
    o `passo_load_slot1`: LOAD e corredor RCG02, passos 1855 e 2300;
  - o `passo_combate` desde o NEW GAME, só com entrada roteirizada: cais com cena em tempo real, porta, menu, exterior e
    combate, passos 2000, 6000, 10000 e 11660.
- **Como:** sessões assistidas sem janela, 4:3 × 16:9 (`BS_TELA_TESTE=854,427`). Em cada uma:
  - o estado por passo (`BS_STATE_PASSO_CADA=1`);
  - a RAM e a IR nos passos marcados;
  - na rota de combate, os efeitos sonoros (`BS_EFEITOS_WAV`).
- **Controle:** duas sessões 4:3 iguais até o passo 2000 dão a mesma IR (1796 = 1796 desenhos, todos pareados pela ordem e
  idênticos) e o mesmo quadro. O método separa o que muda do que é ruído.

| Cena (passo) | Desenhos 4:3 / 16:9 | Cena (andou +107, y igual) | Tela (não andou) | Enviados só num | Cena além do 4:3 | Inteiros fora do 4:3, visíveis no 16:9 |
|---|---|---|---|---|---|---|
| corredor RCG02, perto (1855) | 511 / 511 | 487 | 24 | 0 | 280 | 90 de 181 |
| corredor RCG02 (2300) | 1153 / 1153 | 1129 | 24 | 0 | 684 | 6 de 666 |
| cais, cena em tempo real (2000) | 1796 / 1786 | 1766 (71 pareados pela geometria, nenhum ambíguo) | 20 | **10 só no 4:3** | 689 | 198 de 602 |
| porta trancada, mensagem (10000) | 1290 / 1290 | 1247 | 43 | 0 | 665 | 44 de 606 |
| telas 2D (6000 e 11660) | 818 / 818 e 134 / 134 | 0 | todos | 0 | — | — |

### 11.4 Os critérios

1. **[P] Geometria extra nas laterais, sem esticar.** Em todas as cenas 3D, centenas de desenhos que no 4:3 saíam da tela
   aparecem nas faixas novas. As imagens do próprio jogo, desenhadas pela IR em 854×480 (`bs-ir-tela quadro --largura
   854`), mostram parede, piso, terminal e mar continuando nas laterais (`lado*.png` nas pastas de evidência).
2. **[P] Proporções.** Todo vértice 3D andou **exatamente** Δcx = 107 em x e 0 em y, dentro do arredondamento do float (perto
   do plano da câmera, x chega a 4·10⁷). É uma translação pura: a escala não muda, e as proporções dos personagens e
   objetos também não.
3. **[P] Campo vertical constante.** Dist e cy não mudam, e o y é idêntico em todos os vértices.
4. **[P] Estado do jogo.**
   - A região 0x8C240000–0x8C260000 é idêntica, fora a própria estrutura de tela, nos passos 1855, 2300, 2000, 6000, 10000
     e 11660.
   - A vida, a posição, o passo e o VBlank são iguais em **todos os passos**: 2241 da rota de LOAD e 11661 da rota de
     combate, nas três variantes da seção 11.5.
   - O resto da RAM que muda é do desenho: as cópias do cx, os rascunhos de vértices projetados (+107 exato), os ponteiros
     das listas e áreas referenciadas só pela biblioteca do SDK (código a partir de `0x8C1580D4`).
5. **[P] Original intacta.**
   - O código novo só existe com `BS_VALIDACAO` e só age com as variáveis acima.
   - `app/game_host.cpp` e `app/assist.cpp` foram pré-processados (`cl /EP`) com o comando exato do `msvc-release`, na
     versão do HEAD e na da árvore, e saíram **iguais**. O controle negativo (`-DBS_VALIDACAO=1`) dá diferente.
   - O `bs-ir-tela` só existe com `BS_VALIDACAO`.
6. **[P] Reprodutível:** seis cenas em duas rotas, com o controle 4:3 × 4:3.

### 11.5 Divergências achadas e a causa

- **[P] Pan do som (`8C064560`).** A rotina projeta a fonte com o cx da cópia, limita x a [0, largura da estrutura] e
  calcula pan = (x − cx da estrutura)·0,1. No 4:3 o pan fica em ±32; no 16:9, as fontes além das bordas do 4:3 vão até ±42.
  - **Medido:** na rota de combate, os efeitos diferem em 7686 amostras, todas em 0,18 s perto de 843,5 s (um efeito).
  - **Correção provada:** com `BS_TELA_LOGICA43=1` (a rotina roda com a projeção do 4:3), os efeitos ficam **iguais amostra
    a amostra** aos do 4:3 (0 diferença em 37,8 milhões de amostras), e o estado continua igual.
  - Na fase 1, o pan fica na projeção do 4:3.
- **[P] Teste "na tela" (`8C05D1C0`).** A rotina confere o ângulo com a câmera (±90°), projeta com o cx da cópia e devolve 1
  se 0 ≤ sx ≤ **640,0** e 0 ≤ sy ≤ **480,0** (constantes fixas). Com o cx 427, a janela do teste anda 107 px.
  - Chamadores conhecidos (por ponteiro): `8C069880`, `8C06D610` (`jsr` em `8C06D704`) e `8C06F7E0` (`jsr` em
    `8C06F940`). Os três testam o retorno logo depois.
    - Errata da revisão Codex: a primeira versão deste relatório dizia "único chamador real" e tomava os outros dois por
      coincidência de bytes. Era um erro de leitura: o disassembly tinha começado dentro do literal pool.
    - O envoltório da própria `8C05D1C0` cobre os três. A lista não foi provada exaustiva.
  - Nas rotas medidas, não mudou o estado. Na fase 1, roda na projeção do 4:3, como o pan, para a lógica de visibilidade
    continuar a do Original.
  - **[H]** A finalidade dos três chamadores não foi identificada.
- **[Em investigação] Faíscas da cena do cais (passo 2000).**
  - **10 quads translúcidos** (lista 2, 4 vértices, textura) que o 4:3 desenha no meio da tela (x 293–467) **não são
    enviados** no 16:9. Os outros 71 que pareciam "mexidos" eram pareamentos errados (mesma assinatura) e acham o par exato
    pela geometria.
  - **[P] Causa no caminho do cx da cópia:** só a estrutura (`854,427,estrutura`) não muda nada; só a cópia (`640,320,427`)
    reproduz os 10.
  - **Refutado.** Rodaram na projeção do 4:3, e as 10 continuaram faltando:
    - o `njProjectScreen` (`8C170A54`) e os chamadores dele no jogo (`8C031B80`, `8C032708`, `8C034FC0`, `8C03BF78`,
      `8C03D548`, `8C0584CC`);
    - o teste "na tela" e o pan;
    - as 8 rotinas do jogo com as constantes 640,0 ou 320,0 (`8C032838`, `8C0151DC`, `8C063F4C`, `8C09BA04`, `8C063CE0`,
      `8C0351E0`, `8C01F500`, `8C0221A0`).
  - **[H]** Um descarte de sprites dentro da biblioteca do SDK, que lê a cópia e a região 2D de `8C2D9CAC`–`9CBC` (montada
    por `8C172642`, que usa 640 e 320).
  - **Efeito:** cosmético (as faíscas de um efeito). Sem mudança no estado nem no som. A rotina tem de ser achada antes de
    aceitar a cena, na fase 1.
- **Hipóteses descartadas (registro):**
  - "matriz de projeção do `njSetScreen`";
  - "as cópias em `0x8C2D96xx`/`9C7x` são telas de outros contextos": são a cópia do desenho e os blocos por desenho;
  - "o teste na tela causa as faíscas".

### 11.6 HUD e efeitos (impacto)

- **[P] O HUD e os textos do jogo são 2D em coordenadas de tela.** A moldura do alvo, "ELIOT" e a barra de vida, a mensagem
  "The door is locked." e as telas 2D (passos 6000 e 11660) não andam com o cx.
  - No 16:9 eles ficam na grade do 4:3, colados à esquerda da imagem larga (107 px antes do centro).
  - Isso confirma o **B3**: o HUD precisa da marca cena/HUD por desenho e de âncoras (fases 2 e 3). Até lá, o HUD ficaria
    deslocado.
- **[P] A classificação diferencial** (cena × tela, por duas IRs) serve de **oráculo** para a marca do B3: a marca de cada
  desenho tem de bater com a classe medida.
- **[H] Os efeitos de tela cheia** (fades, filtros) são desenhos "tela" em 640×480 e precisam cobrir a largura nova (fase 3).
  Nenhum foi capturado nestas cenas.

### 11.7 Os bloqueadores depois da fase 0

| | Situação | Por quê |
|---|---|---|
| B1 projeção | **resolvido com prova** | `njSetScreen` `8C1741D4`, a cópia `8C2D9C70`, a fórmula e o caminho (11.1). Hor+ = cx da cópia e da estrutura, dist e cy mantidos |
| B2 recorte nas laterais | **parcialmente resolvido** | o jogo não descarta geometria pela largura da tela (desenhos inteiros fora do 4:3 continuam sendo enviados) e não houve buraco nas 4 cenas 3D. Falta cobrir as salas e o caso das faíscas |
| B3 cena/HUD | **em investigação** | o oráculo diferencial existe e mostra quem é 2D. A marca por desenho na captura continua pendente (fase 2) |
| B4 quadro lógico de 640 | **bloqueado até a fase 1** | o experimento desenhou 854 fora do pipeline (`bs-ir-tela`). O Vulkan, a redução e a apresentação continuam em 640 |
| B5 software | **parcialmente resolvido** | o rasterizador em software aceita a largura 854 (as imagens das evidências são dele). O recuo 4:3 continua sendo o plano |
| B6 fidelidade dos estados | **parcialmente resolvido** | o estado ficou igual em todos os passos das duas rotas. O pan e o teste "na tela" foram identificados, e o pan tem a correção provada. As faíscas seguem em investigação |

O widescreen não exige antecipar o N9.6–N9.8: a projeção é do jogo, e nada da iluminação, da água ou das texturas entra
nela.

### 11.8 Os riscos que ficam

- As faíscas (a rotina não foi achada) e outros efeitos com descarte próprio em salas não medidas.
- Outros usos do cx fora das rotas medidas (cutscenes em tempo real, mapas, chefes). A fase 1 repete a comparação em mais
  rotas.
- No produto, envolver `8C064560` e `8C05D1C0` exige um ponto de entrada sobre chamadas diretas. Pelo N9.2, o
  `mods::replace` do produto só vale nas chamadas indiretas. Estas são "alvo esperado + chamada direta", com o alvo lido de
  um literal na RAM.
- O recálculo de cx = w/2 em `8C0634C0`: a largura da estrutura tem de ser a nova, senão o desenho volta a 320 depois de
  cada desenho em textura.
- O zoom do gameplay muda a distância, e o campo vertical muda junto, como no original.

### 11.9 O próximo passo recomendado

A fase 1, com aprovação:
1. o ponto de entrada da projeção só na Remastered (a cópia e a estrutura com a largura nova, dist e cy intactos);
2. o pan e o teste "na tela" na projeção do 4:3;
3. achar a rotina das faíscas;
4. o quadro lógico largo no Vulkan e na apresentação, com o software e a Original em 4:3;
5. a comparação 4:3 × 16:9 repetida nas rotas do `test_remaster` e do `newgame_area1`.

## 12. Fase 1: o widescreen verdadeiro na Remastered (2026-11-05, fechada em 2026-11-08)

Autorizada pelo mantenedor em 2026-11-05: **só a fase 1** (o hor+ e o quadro largo no Vulkan). Sem HUD (fases 2 e 3), sem as
bordas por sala (fase 4), sem a opção no menu nem as medições (fase 5). A implementação é de 2026-11-05. A sessão foi
interrompida antes do registro, e em 2026-11-08 as provas foram **refeitas** num worktree só com a fase 1, separada do
protótipo de água do N9.7 (seção 12.9). As evidências de 2026-11-05 em `%LOCALAPPDATA%\BlueStingerRecomp\n9-5` não existem
mais. Os números abaixo são os de 2026-11-08, menos onde está escrito "2026-11-05".

### 12.1 Decisão de arquitetura: translação no Vulkan

A fase 0 provou duas coisas:
- todo vértice 3D do 16:9 é **exatamente** o do 4:3 + 107 (sx = cx + dist·ax·x/z: mudar o cx é uma translação pura);
- o jogo **já envia** a geometria que fica fora do 4:3, e o conjunto de desenhos é o mesmo nas duas larguras.

Com isso, o hor+ pode ser feito sem tocar no jogo. Foram apresentadas ao mantenedor duas opções:
- **projeção no jogo** (cx 427 na estrutura e na cópia), que exigiria isolar o pan, o teste "na tela" e as faíscas com
  ganchos novos no gerador (chamadas diretas);
- **translação no Vulkan**: o jogo continua projetando em 4:3, e o renderizador desenha a mesma IR deslocada num alvo
  mais largo.

O mantenedor escolheu a **translação no Vulkan** (2026-11-05). Consequências:
- **Por construção:** a RAM, o `njSetScreen`, o pan do som, os testes "na tela" e os efeitos ficam os do Original. O estado
  e o som não mudam.
- **Fica na apresentação:** o quadro largo e o tratamento das faixas.
- **Nenhum endereço do jogo** entra no produto para isso.

### 12.2 Arquitetura implementada

```
 jogo (4:3, intocado) -> IR 640x480 (geometria alem do 4:3 inclusa)
      |
 transitional/render_core (Mode::Native, render 640x480, quadro largo ligado)
      |   native/render/wide_frame: widen() -> x + 107 em todos os vertices e recortes; largura 854;
      |   faixas de largura inteira sem textura, de cor e 1/w uniformes (fades, barras) cobrem a largura larga
      v
 Vulkan: alvo 854e x 480e; o quadro inteiro vai para um slot (tambem na escala 1: set_always_slot)
      |                                   |
      |  read_frame 854 x 480 -> recorte central 640 x 480 -> framebuffer emulado (o jogo, capturas, hash: 4:3)
      v
 apresentacao (Vulkan direta): o slot no maior 16:9 da janela; a imagem da CPU (filmes, telas da CPU, recuo) e as
 camadas (legendas, menus N7c/N9.2) no 4:3 central desse 16:9
```

| Peça | Arquivo | O que faz |
|---|---|---|
| Adaptador do quadro largo | `native/render/wide_frame.{h,cpp}` | `widen`, `crop_center`, `fit_area`/`center_part`, `kWideWidth` = 854 (deslocamento 107) |
| Renderizador | `transitional/render_core.cpp`, `render_capture.h` | `set_wide(854)`: a IR larga só no `Mode::Native` e só no render 640×480 (o desenho em textura, 256×256, fica como está); o recorte central para o framebuffer; o slot largo para a apresentação |
| Dispositivo Vulkan | `native/render/vulkan/vk_device.*` | `set_always_slot`: o slot também na escala 1; o mesmo caminho de cópia e de semáforo do N7c |
| Apresentação | `native/render/vulkan/vk_present.cpp`, `app/window_present.*` | a área do slot (16:9) e a da imagem da CPU (4:3 central) separadas: sem slot, a imagem 4:3 nunca é esticada |
| Política | `native/platform/edition.*`, `graphics_settings.*` | `EditionProfile::wide_frame` (só a Remastered); `Capabilities::wide_frame` (Remastered + Vulkan com a apresentação direta); `aspect = "16:9"` no `graficos.toml` |
| Aplicação | `app/main_sdl.cpp`, `app/game_host.*` | `--proporcao 4:3\|16:9` (só nesta execução); `GameHost::setWideWidth` na thread do jogo; a janela padrão 16:9; o recuo para o SDL desliga o quadro largo; o limite de resolução interna reduzido para caber no alvo largo |
| Validação | `tools/dev/n95_headless.py` (H1–H5), `tools/dev/n95_janela.py` (J1–J5), `tests/unit/test_widescreen.cpp`, `test_render_capture.cpp` | provas da seção 12.4 |

- **Ativação:** só na Remastered, com o Vulkan e a apresentação direta, e com `aspect = "16:9"` no `graficos.toml` ou
  `--proporcao 16:9`. O padrão continua 4:3.
  - A opção no menu continua "em breve" (`Features::aspect_wide`, fase 5).
  - `Aspect::Auto` continua 4:3 enquanto o HUD não for corrigido.
- **Original:** `wide_frame` falso. O `graficos.toml` com 16:9 volta a 4:3 com o motivo, e `--proporcao 16:9` sem janela é
  recusado (saída 2).
- **Recuos:**
  - sem Vulkan, ou com o software: 4:3, também recusado com saída 2;
  - perda transitória do dispositivo: recriado, e o quadro continua largo;
  - perda definitiva: software em 4:3 até o fim da sessão;
  - apresentação SDL: 4:3.

  O software **não** oferece widescreen e não é declarado como tal.

### 12.3 As faíscas do cais (investigação concluída)

1. **A rotina responsável** (achada em 2026-11-05 por varredura do binário e confirmada pelo experimento do item 3):
   - a **região de recorte 2D das sprites do Ninja** é montada por `8C1736A0` a partir da largura e da altura da tela;
   - essas medidas ficam na cópia do desenho: `8C2D9C74` (float, 640) e `8C2D9C78` (480), e os inteiros `8C2D9CBC`/`9CC0`;
   - quem as fixa é a inicialização do modo de vídeo `8C172642`;
   - o `njSetScreen` (`8C1741D4`) **não** atualiza essa largura.

   Com o cx 427, as sprites andam 107 px, mas a região continua [0, 640]: as que caem além de 640 são descartadas.
2. **Causa:** descarte (culling) 2D contra uma largura fixa. Não é RNG, nem a transformação, nem a projeção dos modelos.
3. **Experimento (2026-11-08, build de validação, rota `passo_combate` desde o NEW GAME, IR do render 15581 = a cena do
   cais, `bs-ir-tela comparar`):**

   | Variante (Remastered) | Comparada com o 4:3 | Estado |
   |---|---|---|
   | projeção larga no jogo, `BS_TELA_TESTE=854,427` (cópia com largura 640) | 1800 × 1789 desenhos: **11 só no 4:3** (as faíscas que faltam) | vida, posição, passo e VBlank iguais em 2054 passos; o hash da RAM muda (a estrutura de tela gravada pelo diagnóstico) |
   | a mesma com a largura da cópia, `854,427,427,854` | 1800 × 1818: **0 só no 4:3**, 18 sprites a mais, todas nas faixas | idem |
   | **o produto: translação, `--proporcao 16:9`** | 1800 = 1800, **todos idênticos** (deslocamento 0) | **idêntico**, inclusive o hash da RAM |

4. **Tratamento:** com a translação, o jogo projeta e descarta exatamente como o Original, e as faíscas são as mesmas.
5. **Limitação que fica (fase 4):** as sprites que o jogo descarta além das bordas do 4:3 não aparecem nas faixas, como no
   Original fora da tela. Partículas que cruzam a borda são enviadas inteiras e aparecem. Alargar a região 2D do jogo é uma
   opção medida (18 sprites a mais), mas muda a RAM do jogo: fica para a fase 4, com prova de estado.

### 12.4 Resultados (2026-11-08)

Executáveis do worktree só com a fase 1 (`tmp/wt-n95f1`, sem o protótipo de água). Sem janela, com o Vulkan offscreen.

| Prova | Resultado |
|---|---|
| `n95_headless.py` H1, H2 (produto) | Original com `--proporcao 16:9` e Remastered no software: recusados, saída 2 |
| H3: rota de LOAD (`passo_load_slot1`), Remastered 4:3 × 16:9 (produto, escala 1) | estado por passo idêntico **2326/2326**; efeitos sonoros idênticos em **6 915 908** amostras; imagem larga com 854 colunas; o centro dela (no formato 555 do framebuffer) **igual pixel a pixel** ao quadro do jogo nas duas capturas; o quadro do jogo 16:9 × 4:3 difere em 2 pixels (máx. 8 e 9 níveis), nas bordas de triângulos (x + 107 em float); laterais com cena 100 % / 100 % (corredor de frente) e 8 % / 76 % no fim do corredor (vazio fora da sala, B2) |
| H4: 1080, 1440 e 2160 linhas | larguras 1922, 2562 e 3843; centro reduzido × quadro do jogo: PSNR 37,3, 37,5 e 37,4 dB; laterais com cena; estado idêntico |
| H5: Remastered 4:3 na escala 1 | nenhuma imagem larga (o caminho do N7c como antes) |
| Faíscas (12.3) | IR do jogo idêntica entre 4:3 e 16:9 na cena do cais; estado idêntico |
| `ctest --preset msvc-debug` | 30/30 aprovados (6 pulados por falta do disco); `bs_unit_tests` 361/0, `bs_render_tests` 83/0 (com `wide_frame_slot_choice`), `bs_remaster_tests` 5/0 |

- O cartão da rota de LOAD é o das referências do N8 (`analysis/n8/referencias/passo_load_slot1_nativo/saves`, o `g1`
  carregado sem SAVE). O `g1` original sumiu de `%LOCALAPPDATA%` (seção 12.8).
- **2026-11-05, não refeito hoje** (registro da sessão anterior; as evidências não existem mais): a rota de combate inteira
  (11 680 passos, 75,6 milhões de amostras de efeitos) idêntica entre 4:3 e 16:9; o escurecimento de tela cheia cobrindo as
  faixas; e a bateria de janela J1–J5 (`n95_janela.py`), que abre a própria janela e captura só a apresentação dela.
- **Nenhuma medição de desempenho** foi feita nesta fase (fica para a fase 5 e o N9.11).

### 12.5 Achados e decisões desta fase

- **Fades e barras de largura inteira:** o escurecimento do jogo é um retângulo translúcido de tela cheia, sem textura,
  `C0000000`, com o mesmo 1/w. Só deslocado, deixava as faixas acesas. Uma cor uniforme estendida não distorce nada;
  imagens e gradientes nunca são esticados.
- **O render exibido (N7c):** a apresentação acha o slot do render exibido comparando amostras com tolerância. Ela aceitava
  o **primeiro** dos últimos três renders aceitáveis, e numa cena quase parada o vizinho também passava (o personagem um
  quadro adiante).
  - No quadro largo ela escolhe o que **melhor** confere, e o centro ficou igual pixel a pixel.
  - Sem o quadro largo, o comportamento do N7c fica como era, para a Original não mudar.
  - **Para o mantenedor decidir:** o mesmo defeito existe na Original com resolução interna maior que 480.
- **Vazio fora da sala (B2):** no fim do corredor RCG02, uma lateral mostra o vazio fora da sala (8 % de cena). É o caso
  previsto da fase 4 (pilares ou recorte por sala).

### 12.6 Revisões Codex

**`reviews/codex-20261105-180747.md`** (2026-11-05):
- **Média, aceita:** sem o slot (o apresentador Vulkan não o fixou, ou recuou para o SDL), a imagem 640×480 da CPU seria
  desenhada na área 16:9, esticada. **Corrigido:** o apresentador recebe as duas áreas (a do slot e a da imagem da CPU) e usa
  a da fonte que desenhou; o recuo para o SDL desliga o quadro largo.
- **Baixa, aceita:** o limite de resolução interna era calculado para o alvo 4:3. Com o 16:9 pedido, ele cai pelo fator
  (480/854)/(3/4), o mais conservador entre a memória e a dimensão máxima.

**`reviews/codex-20261108-134326.md`** (2026-11-08, conferência das correções):
- Confirmou as duas correções no caminho nominal, a translação de vértices, volumes e recortes, o recorte central, o caminho
  inerte com o quadro largo desligado, a política Original/software e o `BS_TELA_TESTE` só na validação.
- **Média, aceita:** depois de trocar para o software pelo menu, com o jogo pausado, nenhum quadro novo chega, e o slot largo
  antigo podia ser desenhado na área 4:3, comprimido. **Corrigido:** `use_slot` (`wide_frame.h`) só deixa a apresentação usar
  um slot largo com o quadro largo ligado; senão vale a imagem da CPU. Teste `wide_frame_slot_choice`.
- **Baixa, aceita em parte:** faltava teste dirigido da escolha da fonte. A decisão agora é uma função pura com teste. O
  recuo real Vulkan → SDL depois de 120 falhas continua **sem teste automático** (exige janela e falha injetada na
  apresentação). Fica para a validação manual e para a fase 5.

### 12.7 Bloqueadores depois da fase 1

| | Situação | Por quê |
|---|---|---|
| B1 projeção | **resolvido com prova** | a translação é equivalente à projeção larga para a geometria 3D (fase 0) e não toca o jogo (fase 1) |
| B2 recorte nas laterais | **parcialmente resolvido** | geometria nas faixas na maior parte das cenas medidas; o vazio fora da sala aparece onde o jogo não tem geometria, e as sprites que o jogo descarta além do 4:3 não aparecem nas faixas. Fase 4 |
| B3 cena/HUD | **em investigação** | o HUD e os textos ficam centrados no 4:3 virtual (o plano até a fase 3); a marca por desenho continua pendente (fase 2) |
| B4 quadro lógico de 640 | **resolvido com prova** | o Vulkan desenha 854 (×e) colunas, o framebuffer do jogo continua 640×480 (o recorte central), e a apresentação mostra o slot largo |
| B5 software | **resolvido (recuo 4:3)** | o software continua 4:3 e é o recuo; `--proporcao 16:9` com ele é recusado; nada o declara widescreen |
| B6 fidelidade dos estados | **resolvido com prova nas rotas medidas** | estado e som idênticos na rota de LOAD (hoje) e na de combate (2026-11-05); as faíscas como no Original (IR idêntica) |

### 12.8 Pendências e limites

- **HUD (fases 2 e 3):** o HUD, as mensagens e os menus do jogo ficam centrados no 4:3 virtual.
- **Bordas por sala (fase 4):** o vazio fora da sala nas faixas e as sprites descartadas além do 4:3.
- **Fase 5:** a opção no menu (ainda "em breve"), o 21:9, as medições de desempenho e de VRAM.
- **Validação manual com o mantenedor:** a janela 16:9 em uso real, o redimensionamento, o recuo para o SDL e o Linux nativo
  no Bazzite.
- **`test_remaster` (2026-11-08, à tarde; exe de validação do worktree, cartão reconstruído das referências do N8, não o
  `g1` original): R1, R2, R3 e R4 aprovados**, cada um com ao menos uma execução completa: R1 e R3 nas duas rodadas, R2 na
  segunda (0 diferenças em 1030 passos) e R4 na primeira (0 diferenças em 1030 passos). Nas duas rodadas, um caso foi
  encerrado pelo vigia de memória (`tmp/`): o commit livre do sistema caiu abaixo de 2,5 GB por processos de outros
  trabalhos (um `nup verify` de 15 GB e, depois, um emulador em uso pelo mantenedor). O jogo ficou em 226–227 MB por
  instância. Os logs mostram a saída cortada sem o resumo final e o caso seguinte começando no mesmo segundo.
- **Pendentes por falta de memória livre (o mantenedor usando a máquina):** a Preview nova em `tmp/` e o `test_edicao`, a
  bateria de janela J1–J5 e a conferência da Original contra as referências do N8.7. Sem elas, a fase 1 **não** está aprovada.
- **Aviso do filme NEWGAME:** toda execução sem janela que passa pelo filme registra uma "falta de progresso" (thread 1 em
  `8C174AAA`) e continua; aparece também nas execuções aprovadas. Não é deste trabalho.
- **C8 bloqueado por dados:** em 2026-11-08, às 03:07, `%LOCALAPPDATA%\BlueStingerRecomp` foi esvaziado (sobraram `build` e
  `recompiler.ini`). Perderam-se o `g1`, as **gravações reais de 2026-10-21** (`gravacoes\sessao-20261021-140145` e
  `-145006`, bases do C8 e da campanha), o estado da regressão e as evidências da fase 1. As referências do N8 em
  `analysis/n8` guardam as saídas, mas não as entradas. Sem um backup do mantenedor, o C8 não pode rodar. A causa não foi
  identificada.

### 12.9 O protótipo de água do Codex (N9.7) diante do N9.5

Avaliado em 2026-11-08 a pedido do mantenedor. **Classificação: protótipo.** Não é o N9.7 iniciado nem concluído.

- **Onde está:** só na árvore de trabalho, sem commit e sem branch (feito na madrugada de 2026-11-08). Cópia de segurança do
  diff completo em `analysis/n9/backup-arvore-20261108/`. Ficou **fora** do commit da fase 1.
- **Arquivos só dele:** `native/render/water.{h,cpp}`, `tests/unit/test_water.cpp`, `docs/N9-7-AGUA.md`, os shaders
  `pvr.frag`/`pvr.vert` e os seis SPIR-V gerados, `app/settings_menu.cpp`, `tests/unit/test_edition.cpp`,
  `tests/unit/test_graphics_settings.cpp`, a entrada N9.7 do `STATUS.md`, os subcomandos `materiais`, `agua` e `telacheia`
  do `bs-ir-tela` e a mudança no `CMakeLists.txt` raiz. **Arquivos compartilhados com a fase 1:** `vk_device.cpp`,
  `render_core.{h,cpp}`, `render_capture.h`, `game_host.{h,cpp}`, `main_sdl.cpp`, `edition.{h,cpp}`,
  `graphics_settings.{h,cpp}`, `native/CMakeLists.txt` e `tests/CMakeLists.txt`. Foram separados hunk a hunk.
- **Técnica:** o mar do cais RCG00 é escolhido por um único `TextureId` (`32x32-7458e3539d31cd8d`, 46 desenhos). Usa três
  ondas nas UVs da cópia da IR e, no fragment shader, normais procedurais, cópia da cena opaca entre os passes A e B,
  refração e reflexo espelhado em espaço de tela em torno do horizonte da malha, Fresnel, especular e cristas. A qualidade
  (`original`/`aprimorada`/`alta`) vai no `graficos.toml` e no menu.
- **Só na Remastered:** `EditionProfile::enhanced_water` e o menu forçam `original` na Original. Sem janela, a água nunca
  liga (o `--headless` não pede a qualidade). **Prova de hoje:** a edição Original com o produto com e sem o protótipo deu
  **60 de 60 quadros iguais byte a byte** e estado igual em 1039 passos (rota `passo_cgate`, que sem save não sai do
  título). O mar ainda não foi coberto por essa comparação.
- **Dependências:** não depende do N9.6. Com o N9.5 é **compatível**: o horizonte e o fundo da água vêm só do y dos
  vértices, que a translação não muda; a cópia da cena e o reflexo usam o tamanho do alvo, que já é o largo.
- **Interferências a resolver no N9.7:**
  - o shader muda em todas as pipelines (um ramo por `water.x`), e o push constant passa de 64 para 80 bytes;
  - a imagem `waterScene_`, do tamanho do alvo, é criada **sempre**, inclusive na Original e com a água desligada, e o
    limite de resolução interna (N7c e seção 12.6) não conta com ela;
  - a marca de qualidade usa os 2 bits altos do `TextureHandle` (handles reais limitados a 30 bits);
  - o `CMakeLists.txt` raiz passa a montar o `bs-ir-tela` também fora da validação, contra o registrado na seção 11.4.
- **Evidências do Codex** (não refeitas hoje): `bs_render_tests` 85/85, `bs_unit_tests` 341/341 (o worktree da fase 1
  tem 361 testes; a diferença não foi explicada), a prova Vulkan sintética e a IR do cais com 35 189 pixels diferentes entre
  Original e Alta.
- **Opções de integração** (decisão do mantenedor, no N9.7):
  1. manter o protótipo na árvore de trabalho até o N9.7 (o estado atual);
  2. guardá-lo numa branch local sobre o commit da fase 1, para não depender da árvore de trabalho;
  3. no N9.7, refazer o commit por cima da fase 1 com as interferências acima corrigidas e as provas de aceitação do plano.
