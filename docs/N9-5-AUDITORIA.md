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
- **Feitas em 2026-11-08 à noite (fase 1 aprovada nos critérios pedidos pelo mantenedor; seção 12.10):** a Original contra
  as referências do N8.7, a Preview nova em `tmp/` com o `test_edicao` e a bateria de janela J1–J5.
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

### 12.10 Aprovação da fase 1 (2026-11-08, à noite)

Executáveis do worktree em `96e2339` (só a fase 1, sem o protótipo de água). Uma instância pesada por vez, com o vigia de
memória em `tmp/` (encerra só o jogo se o commit livre cair abaixo de 2,5 GB; não disparou em nenhuma destas).

| Prova | Resultado |
|---|---|
| Original × `analysis/n8/referencias_n87` (`n8_conferir`, exe de validação com o layout de mods): `newgame_area1`, `cgate`, `heliporto`, `load_opcoes` e `combate`, cada uma com `--tempo nativo` e `experimental` | **10 casos, 0 diferentes**; pico do jogo 347 MB |
| Preview pelo próprio Builder (`make_preview.ps1 -Out tmp/...`, `BS_BUILDER_CACHE` em `tmp/`) | `v0.8.0-15-g96e2339`; a de `dist/` (v0.8.0) intocada |
| `test_edicao.ps1 -Preview <tmp> -Saida <tmp>` | **11 ok, 0 falha** (E0–E8) |
| J1–J5 (`n95_janela.py`, exe da Preview, cartão importado em `tmp/` do mesmo save do R3) | J1, J4 e J5 ok na 1ª rodada; **J2 e J3 ok** na 2ª (abaixo); 0 falha |

- **J2 e J3 na 1ª rodada:** as dez capturas saíram com **3840×2019** (a janela maximizada no monitor 4K, sem a barra de
  tarefas) em vez de 1024×768 e 1280×720. O jogo não tem código que maximize a janela, cada caso tem o próprio
  `graficos.toml`, e o J4, com a mesma janela de 1280×720 do J3, passou logo depois. Repetidos uma vez, com `--manter` para
  guardar o log e as capturas e com o aviso de não mexer nas janelas: 1024×768 com a imagem larga em 1024×575 a partir da
  linha 96 (J2) e 4:3 em 960×720 a partir da coluna 160 (J3). A causa provável é externa (a janela maximizada durante o
  teste); não foi provada.
- **C8 continua bloqueado por dados** (seção 12.8): as gravações reais de 2026-10-21 não existem mais e não foram trocadas
  por dados sintéticos. A fase 1 foi aprovada sem ele, com essa ressalva.
- **Publicação:** o mantenedor pediu cada etapa publicada; a fase 1 sai como `v0.9.5-1`, só com o pacote do Windows (os
  insumos do pacote Linux no WSL foram apagados em 2026-11-07; o Linux volta numa release seguinte).

## 13. Fase 2: a marca cena 3D × HUD 2D por proveniência (2026-11-08)

Autorizada pelo mantenedor com a fase 1 aprovada: classificar cada desenho como cena 3D ou HUD 2D pela proveniência e pela
semântica, **só com metadados e diagnóstico**. Nada é reposicionado, nenhum desenho muda, e a Original não muda.

### 13.1 O que foi feito (só no build de validação)

| Peça | Onde | O que faz |
|---|---|---|
| Cadeia de chamadas do guest | `runtime/cpu/sh4_context.h` (`fn_ring[64]`, `fn_depth`), `sh4_gen.h` | cada `BS_FN_ENTER` grava a entrada num anel; `BS_CALL` e `BS_CALL_REENTRANT` devolvem a profundidade do chamador no retorno. A chamada de cauda (chamada direta + `return`, sem `BS_CALL`) fica acima de quem saltou e é desfeita pelo `BS_CALL` de fora |
| Interrupções e threads | `irq_bridge.cpp`, `sh4_intc.cpp`, `sh4_threads.cpp` | o despacho de um tratador empilha o separador 0 e restaura a profundidade; cada thread do guest guarda o anel e a profundidade ao ser suspensa |
| Origem por parâmetro | `pvr/ta.cpp` (`origin_of`), `memory/sh4_dmac.cpp` | na entrada do TA, as 16 entradas mais internas da cadeia (até o separador) e se o bloco veio da cópia do canal 2 (DMA) |
| Origem por desenho | `kamui/list_sink.{h,cpp}` (`ParamOrigin`, `DrawOrigin`, `ListFrame::origins`) | alinhada a `cl.draws`: a do vértice que abriu o desenho e a do global dele. Um parâmetro de 64 bytes fica com a origem da 1ª metade (o PCW); metades de cadeias diferentes ficam marcadas (`split`) |
| Origem na IR da validação | `render_capture` (`Frame::origins`) | copiada do `ListSink` só quando a IR dele confere com a da captura **desenho a desenho** (`check_list_sink`) |
| A marca | `kamui/draw_class.{h,cpp}` | `classify(origem, desenho, IR)` → cena, tela, ambíguo ou desconhecida (13.3); o produto não tem o arquivo |
| Diagnóstico | `<ir>.origem.tsv` (comando `ir` do assistido e `BS_RENDER_IR_GUARDAR`) | um desenho por linha: a cadeia do vértice e a do global, DMA, tipo, vértices, 1/w mínimo e máximo, profundidade, textura, blend e a marca |
| Oráculo | `tools/dev/n95_proveniencia.py` (`capturar`, `cruzar`) | as cenas da fase 0 em 4:3 e com a projeção larga (`BS_TELA_TESTE=854,427`), o `bs-ir-tela comparar` em cada par e a marca contra a classe medida; um par cuja proveniência difere nos dois lados é erro da captura |

### 13.2 O produto não muda (prova)

- **Pré-processador:** o comando exato do `msvc-release` (tirado do `ninja -t commands`) com `/EP`, no HEAD (`e9dc91d`,
  worktree em `tmp/`) e na árvore, com os espaços colapsados (comparação por tokens). **Iguais** em `irq_bridge.cpp`,
  `sh4_intc.cpp`, `sh4_threads.cpp`, `sh4_context.cpp`, `list_sink.cpp`, `render_core.cpp`, `assist.cpp`, `area4_bus.cpp`,
  `render_bus.cpp`, `dma_bus.cpp`, `game_host.cpp`, `generated/dispatch.cpp` e os 11 `generated/funcs_*.cpp`. O controle
  negativo (`/DBS_VALIDACAO=1`) dá diferente em todos. A primeira versão das macros deixava um `((void)0);` no código gerado
  do produto; a prova pegou, e as macros passaram a não deixar nenhum token.
- `draw_class.cpp` só entra no `bs_kamui` com `BS_VALIDACAO`; `pvr/ta.cpp` e `memory/sh4_dmac.cpp` são da validação.
- **A instrumentação não muda o jogo:** a Original com o exe de validação desta fase contra `analysis/n8/referencias_n87`,
  as 5 rotas × `--tempo nativo` e `experimental`: **10 casos, 0 diferentes** (pico 371 MB).

### 13.3 A regra da marca

A **entrada na biblioteca** de um desenho é a última entrada da cadeia (de dentro para fora) antes da primeira rotina do
jogo. O limite entre o jogo e as bibliotecas do SDK fica em `0x8C158000` (`kGameCodeEnd`): as cadeias medidas chegam ao jogo
bem abaixo dele, e as bibliotecas (Ninja, Kamui, Shinobi) estão acima. Ele só serve para achar a entrada; a cadeia inteira
fica no TSV.

| Entrada | Classe | Por quê |
|---|---|---|
| `8C15F856`, `8C161738`, `8C160C12`, `8C1601FE` | **cena** | o desenho dos modelos e os polígonos projetados do Ninja |
| `8C1736A0` chamada por `8C07B4E0` | **tela** | o recorte dos sprites 2D do Ninja, com o chamador medido |
| `8C16CED8` chamada por `8C0899D6` | **tela** | as strips 2D, com o chamador medido |
| `8C1736A0` ou `8C16CED8` com outro chamador | desconhecida | o recorte dos sprites também recebe sprites já projetados da cena (as faíscas do cais passam por ele até o DMA; seção 12.3) |
| `8C173044` (a cópia do canal 2), 1/w variável no desenho | **cena** | só a perspectiva dá 1/w diferente entre os vértices |
| `8C173044`, 1/w constante | **ambíguo** | 13.5 |
| parâmetro com metades de cadeias diferentes, qualquer outra entrada, cadeia que não chegou ao jogo | desconhecida | nunca chutada |

### 13.4 Resultados contra o oráculo (seis cenas da seção 11.3)

Capturas do gameplay assistido sem janela (`n95_proveniencia.py capturar`, exe de validação desta fase), 4:3 × projeção
larga, nos passos 1855 e 2300 da rota de LOAD (cartão importado do mesmo save do R3) e 2000, 6000, 10000 e 11660 da rota de
combate. Quatro capturas completas ao longo da fase (a última com as correções da revisão), todas com as mesmas contagens.

| Cena (passo) | Oráculo (cena / tela / só 4:3) | Marca |
|---|---|---|
| corredor RCG02 (1855) | 487 / 24 / 0 | todos certos, exceto os ambíguos do DMA |
| corredor RCG02 (2300) | 1129 / 24 / 0 | idem |
| cais (2000) | 1766 / 20 / 10 | idem (as 10 faíscas ficam ambíguas) |
| tela 2D (6000) | 0 / 818 / 0 | idem |
| porta trancada (10000) | 1247 / 43 / 0 | idem |
| tela 2D (11660) | 0 / 134 / 0 | idem |

- **As contagens do oráculo são as da fase 0** (seção 11.3), cena a cena: a captura reproduz o experimento.
- **Proveniência igual nos dois lados de todo par** (cadeia, cadeia do global, DMA, tipo e marca): 0 divergências.
- **Marca × oráculo, nas seis cenas (5702 desenhos):**
  - cena marcada cena: **4543**; tela marcada tela: **48**;
  - **marca errada (cena ↔ tela): 0**; desconhecida: **0**;
  - ambígua: **1111** (1015 tela, 86 cena e as 10 faíscas que só o 4:3 envia), todas do caminho do DMA com 1/w constante.
- **Fora do DMA, a cadeia separa sem erro:** toda cadeia sem DMA é só cena ou só tela. Medido em três níveis (o escritor, a
  rotina do jogo e a cadeia inteira), a única chave mista é a do DMA.
- Evidências (fora do git): `tmp/sessao-20261109/fase2/captura4` (IRs, `.origem.tsv` e `cruzado/`: um TSV por cena,
  `rotinas.tsv` e `resumo.txt`).

### 13.5 A ambiguidade: o caminho do DMA

- Pela cópia do canal 2, a cadeia registrada é a de quem **dispara a cópia** (`8C064A52` → `8C173044` → `8C18BC3C` →
  `8C18BB84` → `8C1C1E16`), não a de quem montou os vértices no buffer da RAM. Todos os desenhos desse caminho são da lista
  translúcida.
- Os de 1/w variável são cena (projetados). Os de 1/w constante misturam o HUD translúcido (textos e molduras das telas 2D
  e dos menus) com os billboards da cena (as partículas do cais e as 10 faíscas que só o 4:3 envia). O estado de desenho é o
  mesmo (profundidade, escrita de z, blend), e os valores de 1/w se intercalam (tela em 1,7·10⁻⁵ e 6,25·10⁻⁴; cena em 2·10⁻⁴
  e 2,8·10⁻³).
- Só a textura separava esses grupos na cena do cais. Uma regra por textura dependeria do conteúdo de cada sala e não foi
  adotada.
- **Para resolver** (proposta, não feita): registrar a cadeia de quem escreve no buffer de vértices do DMA. O código gerado
  escreve na RAM pelo ponteiro direto, então seria preciso o build de vigia de escritas (`BS_WRITE_WATCH`) ou uma marca nas
  rotinas do Kamui que montam o buffer. Fica para a fase 3, com decisão do mantenedor.

### 13.6 Testes

- `bs_render_tests`: `kamui_list_sink_origin_follows_each_draw` (strip, sprite, global, DMA, metades de 64 bytes de cadeias
  diferentes, `list_init`) e `kamui_draw_class_by_library_entry_and_dma_inv_w` (as cadeias reais medidas, o chamador fora
  do medido, o 1/w do DMA, cadeia que não chega ao jogo, desenho fora da IR).
- `bs_unit_tests`: `call_chain_follows_calls_and_returns`, `call_chain_ring_keeps_the_innermost_entries` (recursão de 101
  entradas), `call_chain_tail_call_is_restored_by_the_outer_call` e `call_chain_stops_at_the_interrupt_separator`.
- **Sem teste integrado:** a interrupção real e a troca real de fibras (conferidas pela leitura do código e pela revisão
  Codex; a Original igual às referências passa pelas duas).

### 13.7 Revisão Codex

**`reviews/codex-20261108-201953.md`:**
- **Média, aceita:** um parâmetro de 64 bytes ficava com a origem da 2ª metade. **Corrigido:** a da 1ª (o PCW), com
  `split` quando as metades diferem; teste com origens diferentes.
- **Média, aceita em parte:** a tabela não tinha prova à vista (as capturas ficam em `tmp/`, fora do que o revisor lê, e
  esta seção não existia). O ponto central procede: `8C1736A0` é o recorte geral dos sprites e também recebe sprites
  projetados. **Corrigido:** as entradas de tela só valem com o chamador do jogo medido; com outro, desconhecida.
- **Média, aceita:** o `cruzar` contava pares cuja proveniência diferia nos dois lados. **Corrigido:** compara a cadeia,
  a do global, o DMA, o tipo e a marca; um par divergente sai das contagens e a ferramenta falha.
- **Baixa, aceita em parte:** faltavam testes integrados. **Acrescentado** o da chamada de cauda; a interrupção e a troca de
  fibras ficam sem teste integrado (13.6).

### 13.8 Bloqueadores e pendências depois da fase 2

| | Situação |
|---|---|
| B3 cena/HUD | **resolvido em parte, com prova:** a marca acerta todos os desenhos que afirma, nas seis cenas; o caminho do DMA com 1/w constante fica ambíguo (13.5) |
| Fase 3 (HUD e menus) | espera autorização; depende de decidir como resolver o DMA (13.5) e de levar a proveniência ao produto (hoje só existe na validação; o custo no código gerado não foi medido) |
| Cobertura | seis cenas em duas rotas; outras salas, menus e telas ainda não medidos |

## 14. Fase 3: HUD e menus no widescreen (2026-11-09)

Autorizada pelo mantenedor em duas partes, com validação entre elas: **A**, identificar os elementos do HUD com
segurança, inclusive os desenhos ambíguos do DMA; **B**, reposicionar o HUD na Remastered. As fases 4 e 5 não começam.

### 14.1 Parte A: o caminho do DMA (investigação)

- **Mecanismo.** O jogo monta parte das listas num buffer de vértices na RAM e as envia ao TA pela cópia do canal 2. A
  cópia é disparada por `8C064A52` → `8C173044` → `8C18BC3C` → `8C18BB84` → `8C1C1E16` (o registrador do DMA); `8C173044`
  é um laço de espera e sincronização do quadro, não uma rotina de desenho. Por isso, na fase 2, a cadeia registrada era
  a de quem dispara a cópia.
- **Quem escreve o buffer.** Toda escrita da CPU na RAM passa por `sh4_write8/16/32` (as store queues para a RAM também,
  `memory/sh4_sq.cpp`). A **sombra dos buffers** (`kamui/dma_origin.h`, só validação, ligada por `BS_PROVENIENCIA_DMA=1`):
  1. a cópia marca, num mapa de 1 bit por bloco de 32 bytes da RAM, os blocos que lê;
  2. cada escrita num bloco marcado anota a cadeia de chamadas de quem escreveu e os bytes cobertos;
  3. quando a cópia lê o bloco de novo, o parâmetro recebe a cadeia do **escritor**, mas só se os 32 bytes foram escritos
     pela mesma cadeia desde a cópia anterior. Escrita parcial, vários escritores ou conteúdo que não passou pela CPU
     deixam o bloco sem escritor, e vale a regra da fase 2 (nunca um palpite). A cópia consome a anotação.
- **Reutilização e sincronização.** Os blocos são reutilizados a cada quadro: a sombra só vale para o conteúdo escrito
  depois da cópia anterior, e a cópia do canal 2 é síncrona no runtime (lê a RAM no momento em que é disparada), então
  não há escrita concorrente durante a leitura. Uma sessão nova no mesmo processo limpa a sombra (`ta_reset`). As threads
  do guest são cooperativas no mesmo contexto; a cadeia de cada thread é guardada na troca (fase 2), e a do escritor é a
  da thread que escreveu.
- **Identificador estável.** O par **(entrada na biblioteca, rotina do jogo)**: a entrada é a última rotina do SDK antes da
  primeira do jogo na cadeia do escritor; a rotina é essa primeira do jogo. São endereços de código do binário original:
  não mudam entre quadros, salas ou execuções.

### 14.2 Parte A: a tabela e os resultados

A tabela medida fica em `runtime/kamui/hud_table.h` (classe e papel de cada par; só pares medidos, sem curinga). A marca
da validação (`kamui/draw_class`) a usa; o produto vai usá-la na Parte B.

Capturas do gameplay assistido sem janela (`tools/dev/n95_proveniencia.py capturar --escritor-dma`, exe de validação
desta parte), 4:3 × projeção larga, **16 cenas em quatro rotas**:
- LOAD (`passo_load_slot1`, cartão importado do save do R3): 1855, 2300;
- combate (`passo_combate`): 2000 (cais), 6000 (terminal ESER), 10000 (porta trancada, HUD e mensagem), 11660 (menu);
- heliporto (`passo_heliporto`): 7900 (exterior RCG03), 8840 (inventário), 11240 (item encontrado, o monstro na cena),
  11345, 11500, 11800, 12100, 12190 (menus);
- C-GATE (`passo_cgate`): 4000, 7900 (cenas de história).

| Medida | Resultado |
|---|---|
| Desenhos (16 cenas) | **12 562**: oráculo 10 819 cena, 1 729 tela, 12 só no 4:3 e 2 só no largo (as faíscas e outras partículas) |
| Contagens do oráculo nas seis cenas da fase 0 | iguais às da seção 11.3 |
| Pares com proveniência diferente nos dois lados | 0 |
| Desenhos do DMA com o escritor confirmado (os 32 bytes de cada bloco pela mesma cadeia) | **4 220 de 4 220** (1 601 tela, 2 619 cena) |
| Marca × oráculo | cena 10 819 e tela 1 729 certas; os 14 de um lado só marcados cena; **errados 0, desconhecidos 0, ambíguos 0** |
| Cadeias mistas (cena e tela na mesma cadeia completa) | 0 de 108 |
| A rotina do jogo sozinha | mistura cena e tela (1 chave, 128 desenhos na minoria): a chave precisa ser o par |

- **Os 1 111 ambíguos da fase 2 foram resolvidos** (as seis cenas da fase 0 estão entre as 16): o escritor do buffer
  separa o HUD translúcido (`8C1692CC`/`8C16CED8` com as rotinas do HUD e dos menus) das partículas e faíscas
  (`8C16938C`/`8C057D00`). Nenhuma partícula, faísca ou efeito foi marcado como tela.
- Quatro capturas completas ao longo da Parte A (a última com as correções da revisão), com as mesmas contagens.
- **A instrumentação não muda o jogo:** a Original com o exe de validação desta parte contra `referencias_n87`: **10 casos, 0 diferentes** (as 5 rotas × `--tempo nativo` e `experimental`; pico 403 MB).
- **O produto não muda:** o pré-processador do `msvc-release` dá a mesma saída que `153632b` em todos os arquivos de
  produto tocados ou que incluem os cabeçalhos tocados (`irq_bridge`, `sh4_intc`, `sh4_threads`, `sh4_context`,
  `list_sink`, `render_core`, `assist`, `area4_bus`, `render_bus`, `dma_bus`, `sh4_sq`, `memory`, `game_host`) e nos 12
  arquivos do código gerado; o controle com `/DBS_VALIDACAO=1` difere em todos.
- **Testes:** `bs_render_tests` 86/0 (com `kamui_dma_origin_shadow_needs_the_whole_block_from_one_writer` e a tabela),
  `bs_unit_tests` 365/0, ctest 30/30.
- Evidências (fora do git): `tmp/sessao-20261109/fase3/c4`.

### 14.3 Parte A: elementos do HUD

| Elemento | Par (entrada, rotina do jogo) | Caminho | Papel |
|---|---|---|---|
| Moldura do alvo e nome do personagem ("ELIOT") | `8C1692CC`, `8C060278` | DMA | canto inferior esquerdo |
| Barra de vida | `8C16CED8`, `8C0899D6` | direto e DMA | canto inferior esquerdo |
| Mensagens ("The door is locked.") | `8C1692CC`, `8C03C0C4` | DMA | centro |
| Menu de pausa (MAP, ITEMS, WEAPON, CHANGE, EXIT; textos) | `8C1692CC`/`8C021560`, `8C16CED8`/`8C01F0AE`, `8C01FAEE`, `8C01FD40`, `8C01FC60` | DMA | menu (4:3 centralizado) |
| Terminal ESER | `8C16CED8`/`8C020140`, `8C03C276` | DMA | menu (4:3 centralizado) |
| Elemento sempre fora da tela (x ≈ 800) | `8C1692CC`, `8C0626AA` | DMA | sem papel identificado |
| Sprites degenerados na origem (invisíveis) | `8C1736A0`, `8C07B4E0` | direto | sem papel |
| Partículas e faíscas do cais | `8C16938C`, `8C057D00` | DMA | **cena** (nunca HUD) |
| Sprite preso ao mundo (o jogo o projeta: `8C0584CC` chama o `njProjectScreen`) | `8C1692CC`, `8C0584CC` | DMA | **cena** |

- As rotinas do canto esquerdo são chamadas por `8C060380`, a rotina que desenha o HUD do gameplay.
- A mesma entrada `8C1692CC` serve ao HUD (canto, mensagens, menus) e a um sprite preso ao mundo: só o par separa.
- **Sem identificação confiável:**
  - **arma, munição e a mira de combate:** nenhuma das rotas disponíveis mostra o HUD com uma arma equipada ou um combate
    em curso. No roteiro do heliporto, os passos anotados como combate (11453 em diante) ainda estão no menu de itens
    nesta execução (o roteiro e as anotações não batem; não investigado);
  - **GAME OVER**: pela mesma razão;
  - o elemento `8C0626AA` (talvez o indicador de arma, que entraria com uma arma equipada): papel não provado.

### 14.4 Parte A: classificação leve no produto (arquitetura e custo)

A instrumentação da validação (a cadeia de 64 entradas por chamada e a sombra da RAM) não vai para o produto. O produto
só precisa saber, por desenho, se ele veio de uma rotina de HUD com papel conhecido.

**Proposta (A1, marca de escopo):**
- o gerador emite, só nas rotinas de HUD da tabela (hoje `8C060278`, `8C0899D6`, `8C03C0C4` e os menus), uma marca de
  escopo no `Sh4Context` (RAII: liga na entrada, restaura na saída por qualquer caminho); nenhuma outra função muda;
- caminho direto (store queue para o TA): a entrada da área 4 grava a marca corrente em cada parâmetro (um byte);
- caminho do DMA: toda escrita da CPU na RAM testa a marca; com ela ligada, anota o papel nos blocos do buffer (o mesmo
  mapa de blocos da cópia); a cópia lê o papel do bloco;
- a Remastered 16:9 lê o papel de cada desenho; a Original e o 4:3 não leem nada.

**Custo medido do termo que pesa** (o teste da marca em toda escrita na RAM, com a marca sempre desligada; protótipo em
`tmp/wt-n95-proto`, fora dos commits; rota de combate, 20 000 VBlanks sem janela, 3 rodadas alternadas, tempo de CPU do
processo): base 163,11 / 162,45 / 163,55 s; com o teste 167,80 / 165,38 / 163,52 s. Média **+1,6 %** (+2,5 s em 163 s),
dentro do ruído (na 3ª rodada as duas empatam). Tempo de parede +0,7 %. Memória: 64 KiB do mapa de blocos + 1 byte de
papel por bloco usado. Não medido: o custo dentro das rotinas de HUD (poucas escritas por quadro) e em outras rotas.

**Alternativas avaliadas:**
- **A2, ponteiro do buffer do Kamui:** registrar, na entrada e na saída das rotinas de HUD, o ponteiro de escrita do buffer
  de vértices e atribuir a faixa escrita. Custo por escrita zero, mas exige achar e provar o ponteiro dentro do Kamui
  (análise estática não feita) e tratar vários buffers; mais frágil.
- **A3, regra por posição/estado:** rejeitada (o mantenedor vetou classificar só pela posição, textura ou transparência).

### 14.5 Parte A: revisão Codex

**`reviews/codex-20261109-024548.md`** (três médios, todos aceitos):
- **Escrita parcial assumia a autoria do bloco inteiro.** Corrigido: a sombra guarda os bytes cobertos e se houve mais de
  um escritor desde a cópia anterior; só um bloco escrito inteiro pela mesma cadeia tem escritor (`kamui/dma_origin.h`,
  teste dedicado). Na medição final, os 4 220 blocos de desenho do DMA passaram nesse critério.
- **Uma sessão nova no mesmo processo herdava a sombra.** Corrigido: `ta_reset` limpa o mapa, as anotações e os contadores.
- **Entradas curinga na tabela.** Corrigido: só os pares exatos observados; um chamador não medido fica desconhecido
  (teste negativo para cada entrada que era curinga).
- Não verificado pelo revisor: as evidências ficam em `tmp/` (fora do repositório); os números estão nesta seção.

### 14.6 Parte B: a marca leve no produto (arquitetura A1, escolhida pelo mantenedor em 2026-11-09)

| Peça | Onde | O que faz |
|---|---|---|
| Pontos de chamada do HUD | `config/bluestinger_us.toml` (`[remaster] hud_chamadas_canto_esquerdo`) | os cinco retornos medidos (14.7): `8C0605CE`, `8C061346`, `8C060838`, `8C0608DE`, `8C0609EC` |
| Gerador | `translator/codegen` (`Options::hud_sites`, `hudScope`), `translator/app/config` | põe a chamada (direta, indireta de alvo resolvido ou pelo despacho) dentro de `BS_HUD_SCOPE(1)`; a chave é o retorno. O C++ gerado difere do anterior **só nessas cinco linhas** |
| Escopo | `runtime/cpu/hud_scope.{h,cpp}` (`HudScope`, RAII) | liga `Sh4Context::hud_role` durante a chamada e devolve o anterior em qualquer saída; só com `hud_marks_enable` |
| Interrupções e threads | `irq_bridge.cpp`, `sh4_intc.cpp`, `sh4_threads.cpp` | o tratador não herda a marca; cada thread do guest guarda a sua |
| Escritas na RAM | `runtime/cpu/sh4_mem.h` | com a marca ligada, a escrita anota no bloco de 32 bytes o papel e o render corrente (fora a pilha corrente) |
| Cópia do canal 2 | `system/dma_bus.cpp` (produto), `pvr/ta.cpp` + `sh4_dmac.cpp` (validação) | lê e consome o papel do bloco copiado; só vale a marca do render corrente ou do anterior |
| Escrita direta no TA | `system/area4_bus.cpp` (produto), `pvr/ta.cpp` (validação) | o parâmetro leva a marca em vigor |
| IR | `kamui/list_sink` (`ListParam::role`, `ListFrame::roles`) | o papel de cada desenho, só quando a lista recebeu papéis; o parâmetro de 64 bytes fica com o da 1ª metade |
| Quadro largo | `native/render/wide_frame` (`widen(cl, 854, âncoras)`), `transitional/render_core` | os desenhos ancorados à esquerda não andam (vértices e recorte); o resto anda +107 como na fase 1 |
| Ligar | `app/game_host.cpp` | `hud_marks_enable` junto com o quadro largo (a Remastered 16:9); desligada no 4:3, na Original e no software; a troca 16:9 ↔ 4:3 descarta as marcas pendentes |
| Só validação | `pr_ring` (o PR em cada entrada da cadeia), a coluna `sitio` do TSV, `BS_HUD_MARCA=1`, `papel_tabela`/`papel_produto`, `n95_proveniencia.py --marca --variantes` | o ponto de chamada de cada desenho e a conferência da marca do produto contra a tabela |
| Diagnóstico | `BS_HUD_ANCORAS_LOG=<arquivo>` (`render_core`, inerte sem a variável) | o retângulo dos desenhos ancorados de cada render, para o critério H3 |

**Custo.** O termo que pesa é o teste da marca em cada escrita na RAM: medido no protótipo (14.4) em +1,6 % de CPU na
média, dentro do ruído. Memória: 2 MiB para o mapa de blocos (4 bytes por bloco), alocados só na primeira vez que a marca
liga; 1 byte de papel por desenho com a marca. O código gerado só muda nos cinco pontos de chamada.

### 14.7 Parte B: o erro das rotinas genéricas e a marca por ponto de chamada

- **O erro.** A primeira versão marcava a **entrada** de `8C060278` e `8C0899D6`. O H3 com o diagnóstico de âncoras mostrou
  desenhos ancorados de tela cheia em todos os renders anteriores ao gameplay (renders 6–1768: logos, título, LOAD). As
  duas rotinas são **genéricas** (sprite 2D e retângulo 2D): o título, os menus de LOAD e o inventário também as chamam. A
  tabela da Parte A tinha o mesmo defeito (as 16 cenas não incluíam o título), e a marca do produto concordava com ela.
- **A medição.** A validação passou a gravar o PR de cada entrada (o retorno do ponto de chamada), e o TSV, o ponto de
  chamada da 1ª rotina do jogo e o retângulo do desenho. Em 17 cenas (as 16 e o título, `passo_load_slot1` 100–1700):

| Rotina | Chamador | Retorno | Retângulo (4:3) | Cenas | O que é |
|---|---|---|---|---|---|
| `8C060278` | `8C060380` | `8C0605CE` | x 72–176 | 16 | o nome ("ELIOT") |
| `8C060278` | `8C061312` | `8C061346` | x −8–64 | 7 | a moldura do alvo (animada) |
| `8C0899D6` | `8C060380` | `8C060838`, `8C0608DE`, `8C0609EC` | x 179–339 | 16 | a barra de vida |
| `8C060278` | `8C0797C0`, `8C079940`, `8C079AAC`, `8C079D20` | vários | até a tela cheia | título | **não é HUD** |
| `8C0899D6` | `8C078FC0` | `8C0792BC` | tela cheia | título | **não é HUD** |
| `8C0899D6` | `8C08A054` | `8C08A1C2` | tela cheia | inventário | **não é HUD** (a sobreposição do menu) |

  O y chega a ~620 em alguns quadros: o HUD escondido abaixo da tela. Das 7 chamadas de `8C060380` a `8C060278`, só
  `8C0605CE` apareceu; as outras 6 ficam sem âncora (centralizadas, como na fase 1).
- **A correção.** A tabela ganhou o campo do ponto de chamada (`hud_table.h`, `site`): o HUD do canto só nesses cinco
  retornos; qualquer outro é desconhecido. O gerador põe o escopo só nesses pontos (duas das chamadas da barra de vida são
  `jsr @r2`, pelo despacho em execução: o escopo envolve o despacho, e a validação mediu que dali sai a barra de vida).

**Resultados da Parte B** (builds da worktree com o C++ gerado em `tmp/`; uma instância pesada por vez):

| Prova | Resultado |
|---|---|
| C++ gerado × o anterior | só os cinco `BS_HUD_SCOPE(1)` nos pontos de chamada medidos |
| Marca do produto × tabela (`BS_HUD_MARCA=1`, 17 cenas com o título, 15 442 desenhos do 4:3) | canto 204 / 204, **0 errados**; título, LOAD e a sobreposição do inventário sem âncora |
| Original × `referencias_n87` (exe de validação desta parte) | **10 casos, 0 diferentes** |
| H1, H2 | Original e software recusam o 16:9 (saída 2) |
| H3 (rota de LOAD, Remastered 4:3 × 16:9) | estado por passo idêntico 2326/2326; efeitos sonoros idênticos em 6 915 908 amostras; HUD ancorado em x 23–339, y 407–448 (4,7 % do quadro); fora dele, o framebuffer difere em 2 px (bordas de triângulo, máx. 8–9 níveis); o HUD aparece no quadro largo na posição x do 4:3 (1 811 e 1 796 px, 100 % iguais) |
| H4 (1080, 1440, 2160 linhas) | estado idêntico; larguras 1922, 2562, 3843; centro reduzido × quadro do jogo PSNR 37,3 / 37,5 / 37,4 dB (os mesmos da fase 1) |
| H5 (Remastered 4:3) | nenhuma imagem larga |
| `test_remaster` R1–R4 | **4/0** |
| J1–J5 (janela) | **0 falhas** (J1 e J2 16:9 com o HUD ancorado; J3 Original continua 4:3; J4 Remastered 4:3; J5 SDL fica 4:3) |
| ctest | validação 30/30, produto 23/23; `bs_unit_tests` 368/0, `bs_render_tests` 88/0 |

**Comparação visual 4:3 × 16:9** (imagens do próprio jogo, `tmp/sessao-20261109/fase3/vis`): no corredor RCG02 (J1, janela
1280×720), a moldura do alvo, "ELIOT" e a barra de vida ficam no canto inferior esquerdo da tela, com a cena na largura
inteira; na Remastered 4:3 (J4), no canto do 4:3, entre as faixas pretas. Tamanho e proporção iguais.

**Desempenho:** o teste da marca em cada escrita foi medido no protótipo (14.4: +1,6 % de CPU, no ruído); nenhuma outra
medição de desempenho foi feita nesta fase (fica para a fase 5).

### 14.8 Parte B: elementos reposicionados

| Elemento | Papel | Na Remastered 16:9 |
|---|---|---|
| Moldura do alvo, nome ("ELIOT") e barra de vida | canto inferior esquerdo | presos à borda esquerda do quadro largo (x como no 4:3, sem os +107) |
| Mensagens ("The door is locked.") | centro | continuam centralizadas (+107, como na fase 1) |
| Menus do jogo (pausa, inventário, terminal ESER, título, LOAD) | menu | continuam no 4:3 centralizado, desenho preservado |
| Fades e faixas de tela cheia | — | como na fase 1: cor uniforme estendida à largura inteira |
| Partículas, faíscas, sprites presos ao mundo, modelos | cena | seguem a projeção (+107); nunca ancorados |
| Arma, munição, mira de combate, GAME OVER | — | **não identificados** (nenhuma rota os mostra); centralizados como na fase 1 |

### 14.9 Parte B: o critério H3 (decisão do mantenedor, 2026-11-09)

O framebuffer de 640 colunas é o recorte central do quadro largo, e o HUD ancorado (x ≈ 23–339 no quadro largo) fica fora
dele ou deslocado: com a primeira medição, 3 033 e 2 550 px (~1 %) diferentes no "quadro do jogo 16:9 × 4:3". A tela mostra
sempre o quadro largo; a apresentação SDL (que mostra o framebuffer) desliga o quadro largo e a marca; a lógica do jogo
não lê pixels. **Não provado:** se o jogo reaproveita a imagem do framebuffer em algum efeito. O mantenedor escolheu
**ajustar o H3** (`tools/dev/n95_headless.py`): na Remastered 16:9,
- o "quadro do jogo 16:9 × 4:3" compara o framebuffer **fora** dos retângulos dos desenhos ancorados, lidos da IR
  (`BS_HUD_ANCORAS_LOG`): onde o HUD está no 4:3 e onde a parte dele que cai no recorte aparece (107 colunas à esquerda);
  o limite continua 0,1 % dos pixels;
- o retângulo excluído não pode passar de 10 % do quadro (senão o critério ficaria vazio: foi o que o erro de 14.7
  produziu, um retângulo de tela cheia);
- sem nenhuma âncora, o caso falha (a marca regrediu; revisão Codex 20261109-171852);
- novo: os pixels do HUD (verdes) do 4:3 aparecem iguais no quadro largo na mesma posição x (≥ 95 %).
No 4:3, na Original e sem quadro largo, o critério continua inteiro.

### 14.10 Parte B: revisões Codex

**`reviews/codex-20261109-094056.md`** (versão por entrada de rotina; dois médios):
- **Aceito em parte — o recorte central tira o HUD ancorado do framebuffer 4:3:** 14.9 (decisão do mantenedor; limitação
  documentada).
- **Aceito — um bloco reutilizado podia ancorar um desenho de cena:** a marca guarda o render (expira depois de dois), a troca
  16:9 ↔ 4:3 descarta as pendentes, a pilha corrente não é marcada (`hud_block_marks_expire_and_skip_the_stack`).

**`reviews/codex-20261109-171852.md`** (versão por ponto de chamada; dois médios). O revisor conferiu os cinco retornos no
binário (`8C0605CA: bsr 8C060278`, `8C061342: jsr @r3`, `8C060834`, `8C0608DA` e `8C0609E8: jsr @r2`), o escopo nos três
caminhos de chamada, as threads e as interrupções, e a recusa de um retângulo de tela cheia no H3.
- **Aceito em parte — uma marca de HUD sobrevive se o bloco for reescrito pela cena antes da cópia (no render corrente ou no
  seguinte).** Os parâmetros do TA são blocos de 32 bytes alinhados, e a cópia de cada quadro consome as marcas dele; o caso
  pede um bloco do HUD não copiado no seu quadro e reaproveitado pela cena logo depois. **Medido:** não ocorreu nos 15 442
  desenhos das 17 cenas. A correção completa (invalidar em toda escrita na RAM, como a sombra da validação) tem custo por
  escrita não medido; fica como risco residual para a fase 5 (desempenho), com a decisão do mantenedor.
- **Aceito — o H3 passaria sem nenhuma âncora.** Corrigido (14.9).

### 14.11 Pendências e limites depois da fase 3

- **Sem identificação:** arma, munição, mira de combate e GAME OVER (nenhuma rota disponível os mostra); o elemento
  `8C0626AA` (sempre fora da tela); as outras 6 chamadas de `8C060380` a `8C060278` (sem âncora).
- **Framebuffer 4:3 com o quadro largo:** sem o HUD ancorado (14.9).
- **Marca de um bloco reescrito pela cena antes da cópia:** sem tratamento no código; não observada (14.10).
- **C8:** continua bloqueado (as gravações reais de 2026-10-21 não existem mais).
- **Fase 4:** autorizada com a publicação da fase 3 (seção 15). **Fase 5:** espera autorização (menu 4:3/16:9, 21:9, desempenho).

## 15. Fase 4: as bordas do widescreen (2026-11-09)

Autorizada pelo mantenedor com a publicação da fase 3. O objetivo (bloqueador B2): onde a sala não tem geometria nas
faixas novas, não mostrar o vazio fora da sala.

### 15.1 O levantamento por sala

`tools/dev/n95_bordas.py` roda o produto sem janela na Remastered 16:9 (Vulkan, o quadro largo da fase 1) nas rotas por
passo, captura a imagem larga (o slot, 854 colunas) a cada 180 VBlanks e liga cada captura à sala pelo registro de arquivos
(`BS_FILE_LOG`: o último `R*.BIN` lido). Em cada captura, a fração de vazio (preto) em cada faixa e no centro.

| Sala | Capturas largas | Com cena no centro | Com vazio só nas faixas |
|---|---|---|---|
| título, LOAD (sem sala) | 30 | 2 | 0 |
| RCG00 (o cais) | 12 | 12 | 0 |
| RCG00A | 58 | 55 | 0 |
| RCG02 (o corredor) | 11 | 11 | **2** (o fim do corredor: faixa esquerda 95 % e 89 % vazia) |
| RCG03 (o exterior) | 70 | 58 | 0 |

- Rotas: `passo_heliporto` (do NEW GAME ao GAME OVER: cais, C-GATE, RCG02, exterior, menus, o monstro) e
  `passo_load_slot1`. 181 capturas largas (as outras são filmes e telas de outro tamanho).
- **O vazio fora da sala só aparece no fim do corredor RCG02**: a câmera de canto mostra além da parede. As outras salas
  medidas têm cena nas faixas em todas as capturas, inclusive as cenas de câmera fixa e a cena roteirizada do cais.
- **Cobertura limitada:** cinco salas do começo do jogo. As gravações reais que cobririam o resto se perderam
  (`C8 bloqueado`); o restante do jogo não foi medido.

### 15.2 O tratamento: o pilar dinâmico (decisão do mantenedor, 2026-11-09)

O mantenedor escolheu, entre o pilar dinâmico, o pilar por sala (tabela), manter como está e esperar mais rotas, o
**pilar dinâmico**: vale para qualquer sala, inclusive as que as rotas não cobrem.

| Peça | Onde | O que faz |
|---|---|---|
| A medida | `native/render/wide_frame` (`band_uncovered`) | uma grade de 8 × 24 pontos em cada faixa contra os triângulos da IR alargada, com as regras de descarte do rasterizador (degenerado, orientação, "small cull") e o recorte "dentro"/"fora" de cada desenho; sem o HUD ancorado nem as faixas de largura inteira (fades) |
| A histerese | `PillarState` | liga depois de 10 renders seguidos com ≥ 80 % da faixa descoberta, desliga depois de 10 com ≤ 50 %; uma vez por render lógico (a recuperação do Vulkan não conta duas vezes) |
| O pilar | `pillar_bands` | os desenhos da cena perdem as colunas da faixa (o recorte "dentro" de cada um); a faixa mostra o fundo, com borda reta em x = 107, e o HUD continua por cima |
| Onde liga | `transitional/render_core` | só no quadro largo (a Remastered 16:9 com o Vulkan); no 4:3, na Original e no software nada muda; um render de outro tamanho (para textura) não zera o estado |
| Diagnóstico | log `[render] pilar: …` a cada troca; `[captura] vNNNNNN pilar esquerda=… direita=…` em cada captura larga | o estado do pilar por captura (o critério H6) |

- **O que o pilar não toca:** o centro (o 4:3 e o framebuffer do jogo, o recorte central), o HUD ancorado, as faixas de
  largura inteira (fades e barras) e os desenhos com recorte "fora".
- **Escolha visual registrada:** com o limiar de 80 %, até 20 % da grade de uma faixa pode ter cena e ainda assim ser
  recortada quando o pilar liga (revisão Codex 15.4). Nos dois casos medidos, a faixa tinha 89–95 % de vazio.

### 15.3 Os resultados (bateria de 2026-11-09)

Builds `msvc-validacao`, `msvc-debug` e `msvc-release` do mesmo código (o script para no primeiro erro; os exes testados
são os do build, conferidos pela hora). Logs em `tmp/sessao-20261109/logs` (fora do git).

| Teste | Resultado |
|---|---|
| ctest `msvc-debug` / `msvc-release` | 30/30, 23/23 |
| `bs_unit_tests` / `bs_render_tests` | 368/0, 91/0 (os novos: a medida com descarte e recorte, a histerese, o pilar) |
| H1–H5 (fase 1, sem janela) | 0 falhas: estado e efeitos idênticos ao 4:3 |
| **H6** (o pilar) | 0 falhas: RCG02 003800 (corredor de frente, 93 % da faixa com cena) sem pilar; 004640 (o fim do corredor) com o pilar esquerdo, a faixa esquerda com 0 % de cena fora do HUD e o centro intacto (74 % com cena); o estado do pilar registrado em cada captura confere |
| Original × `referencias_n87` | 10 casos, 0 diferentes |
| R1–R4 (`test_remaster`) | 4 casos, 0 falhas |
| J1–J5 (janela real, só as do Blue Stinger) | 0 falhas; quadro largo só em J1 e J2, como na fase 1 |

- Memória: pico de 5,1 GB do `bluestinger`, commit livre mínimo de 20 GB; o vigia não interrompeu nada.
- O pilar não muda o estado do jogo (H1 e R1–R4) nem a Original (N8.7); só a imagem das faixas do quadro largo.

### 15.4 Revisão Codex

**`reviews/codex-20261109-203527.md`** (dois médios, dois baixos; todos aceitos):
- **Média — o limiar de 50 % podia apagar até metade da faixa com cena.** O limiar passou a 80 % (desliga em 50 %), e a
  escolha está documentada (15.2).
- **Média — a medida contava triângulos que o rasterizador descarta ou recorta.** A medida aplica agora o descarte
  (degenerado, orientação, "small cull") e o recorte "dentro"/"fora" de cada desenho; testes com cada caso.
- **Baixa — a recuperação do Vulkan contava o mesmo render duas vezes na histerese.** Uma atualização por render lógico.
- **Baixa — o H6 não ligava o estado do pilar à captura.** O produto registra o estado do pilar em cada captura larga, e o
  H6 confere o da captura (004640 com o pilar esquerdo; 003800 sem pilar).

### 15.5 Pendências

- **Cobertura:** cinco salas do começo do jogo; o resto do jogo não foi medido (sem rotas). O pilar dinâmico vale para
  todas, mas o efeito nas outras salas não foi visto.
- **Sprites descartados além do 4:3** (a nota do B2 da fase 1): o jogo não envia algumas sprites que ficariam nas faixas;
  com o pilar ou sem ele, elas não aparecem. Não tratado.
- **Fase 5:** espera autorização (menu 4:3/16:9, 21:9, desempenho, o risco residual da marca do HUD da seção 14.10).

## 16. Fase 5: configurações, transições e desempenho (2026-11-10)

Autorizada pelo mantenedor em 2026-11-10, com as decisões D5-1 a D5-4. A fase 4 fica no commit local `17963bb`, sem
publicação separada. Antes da fase 5, o protótipo de água do Codex (N9.7) foi isolado numa branch local (16.0).

### 16.0 O protótipo de água isolado (antes da fase 5)

- **Branch local `n9-7-agua-prototipo`** (commit `8cf3971` sobre o `17963bb`), na worktree `tmp/wt-n97-agua`. Leva os 32
  arquivos da água: os 28 rastreados alterados (shaders, os 7 SPIR-V, `vk_device`, `render_core`, `render_capture.h`,
  `graphics_settings`, `settings_menu`, `main_sdl`, `game_host`, `edition`, os testes, o `bs-ir-tela`, os dois
  `CMakeLists.txt` e o bloco N9.7 do `STATUS.md`) e os 4 novos (`water.{h,cpp}`, `test_water.cpp`, `docs/N9-7-AGUA.md`).
- **Fora dela:** os arquivos particulares (`known_versions.h`, `DISTRIBUICAO-COMPILADOR.md`, hashes conferidos sem mudança),
  o `_bs-extract.exe` e o `tools/extractor/build/`.
- **Conferência:** os 32 arquivos da branch iguais aos da árvore de trabalho (dois só no fim de linha, CRLF). Cópia
  independente em `tmp/sessao-20261110/agua/` (bundle verificado, patch e o diff de origem); o backup de 2026-11-08 ficou
  intacto. A árvore principal não foi limpa. O N9.7 continua não iniciado; a branch não foi testada nem integrada.

### 16.1 Fase 5A: o menu e o arquivo

**Como era (confirmado no código antes da mudança):** a linha "Proporção" existia, mas dependia de
`Features::aspect_wide`, que nenhum caminho ligava, e aparecia "em breve"; ela percorria três valores (com o automático);
a proporção pedia a confirmação de 15 s, mas o arquivo gravava a escolha antes dela; com o software escolhido, o menu e o
arquivo diziam 16:9 com a imagem em 4:3.

**Decisões do mantenedor (2026-11-10):**

| | Decisão | Implementação |
|---|---|---|
| D5-1 | sem o automático no menu; arquivo antigo vale 4:3 | `from_toml` lê `"auto"` (o nome gravado até a fase 4) e `"automatico"` como 4:3, com o aviso no log; `clamp_to` leva `Aspect::Auto` a 4:3; a próxima gravação escreve `"4:3"` |
| D5-2 | a troca vale na hora; o arquivo guarda a proporção aplicada de fato | `requires_confirmation` sem a proporção (`apply_kind` continua `Recreate`); `aspect_for_file` grava a escolha só quando ela vale na imagem |
| D5-3 | telemetria no produto, desligada por padrão | fase 5C |
| D5-4 | a Original fica em 4:3 fixo | a linha mostra "4:3 (original)" sem mudança; a Original nunca reescreve a proporção do arquivo |

**As regras (funções puras em `native/platform/graphics_settings`):**
- `aspect_choice`: `Fixed` (a edição sem o quadro largo), `Unavailable` (a Remastered sem a apresentação Vulkan direta,
  inclusive depois do recuo para o SDL), `NeedsVulkan` (o software escolhido) e `Free`.
- `effective_aspect`: 16:9 só com `Free` e o 16:9 escolhido.
- `aspect_for_file`: a proporção do arquivo quando a escolha não é `Free`, quando veio do `--proporcao` ou quando não valeu;
  senão, a escolhida. A escolha 16:9 fica guardada com o software, sem Vulkan e depois do recuo para o SDL: só a imagem fica
  em 4:3.

**O menu (`app/settings_menu`):** só 4:3 e 16:9; o valor é a proporção em uso; "4:3 (16:9 só com Vulkan)" com o software
e "4:3 (16:9 indisponível)" sem o quadro largo, as duas sem mudança. **`main_sdl`:** `apply` usa `effective_aspect`; o
`persist` usa `aspect_for_file`; o `drop_wide` avisa o menu; uma mudança da proporção no menu tira o `--proporcao`.

**Testes:**

| Teste | Resultado |
|---|---|
| `graphics_aspect_rules_fase5`, `settings_menu_aspect_fase5` (novos) e os existentes | `bs_unit_tests` 370/0 |
| `bs_render_tests` | 91/0 |
| ctest `msvc-debug` / `msvc-release` | 30/30, 23/23 (no `release`, os 3 testes que pedem a imagem do disco ficam pulados, como antes) |
| **P1–P5** (`tools/dev/n95_persistencia.py`, novo, janela com o controle virtual) | 0 falhas: P1 16:9 na hora, sem confirmação, gravado e lido na execução seguinte; P2 arquivo com `"automatico"` vale 4:3 com aviso e some na troca; P3 software (linha indisponível, 16:9 guardado, de volta ao Vulkan o 16:9 vale) e Original (4:3 com 16:9 no arquivo, a proporção do arquivo intacta); P4 `--proporcao` nos dois sentidos e mudado no menu; P5 o recuo real para o SDL |
| `n7_menu_pausa` (o menu pausa e retoma) | 6/0 |
| J3, J4 (Original e 4:3 na janela) | 0 falhas |

- **P3, critério corrigido:** a primeira execução cortava o log na troca do renderizador; a thread do jogo registra o quadro
  largo antes dela, no mesmo pedido (o software nunca desenha o largo: `render_core` só alarga no modo nativo). O corte
  passou a ser a linha do menu que aplica o 16:9, mais um critério novo: a Direita na linha indisponível não gera troca.
- **Só validação, novo:** `BS_APRESENTACAO_FALHAR=<n>` dá a apresentação Vulkan como falha a partir do n-ésimo quadro (o
  caminho real do recuo para o SDL), para o P5.

**Revisão Codex `codex-20261110-004711`:**
- **Média — o limite da resolução não acompanha a troca para 16:9:** aceita; é a lacuna da fase 5B (16.2).
- **Baixa — faltavam o `--proporcao` e o recuo para o SDL nos testes integrados:** aceita; P4 e P5.

### 16.2 Fase 5B: as transições e o limite da resolução

**Como era:** o limite da resolução interna do quadro largo (o alvo tem 854/640 da largura) só valia quando o jogo já
começava em 16:9, e ele reduzia a própria escolha do jogador; uma troca para 16:9 pelo menu (desde a 5A) podia pedir um alvo
largo acima do limite da GPU. A `--resolucao-interna` passava por fora do limite. A desistência do Vulkan (o render no software
até o fim da sessão) não chegava à janela nem ao menu.

**O que mudou:**

| Peça | Onde | O que faz |
|---|---|---|
| O limite por proporção | `graphics_settings`: `max_internal_height`, `effective_internal_height`; `Capabilities::max_height_wide` | o limite largo é `limite 4:3 × (480/854) / 0,75` (o mais conservador entre a memória e a dimensão máxima da imagem); a altura em uso desce pelos degraus do menu (a escala volta a 100 %, depois a resolução) até caber, e a escolha do jogador não muda: de volta ao 4:3, a escolha inteira vale |
| A ordem dos pedidos | `InGameMenu::apply` | a thread do jogo atende a altura e a largura no VBlank seguinte; a altura que desce vai antes do alargamento e a que sobe depois dele, então nenhum render usa o alvo largo com a altura do 4:3 acima do limite |
| A linha de comando | `--resolucao-interna` | vale até o jogador mudar a resolução no menu, com o limite da GPU da proporção em uso; sem janela, o mesmo limite |
| A desistência do Vulkan | `render_capture::vulkan_gave_up` (atômico; recomeça a cada `install`) | a janela tira o quadro largo e o menu mostra "4:3 (16:9 indisponível)", também com o 4:3 em uso |
| O recuo para o SDL | `drop_wide` | reaplica a configuração inteira pelo caminho do menu: a altura escolhida e o CRT voltam com o 4:3 |
| O CRT | `apply` e o menu | pela altura em uso (o limite do 16:9 pode reduzi-la abaixo de 1080) |
| O menu | `settings_menu` | "4K (16:9: em uso 1440p)" quando o limite reduz a escolha |
| Diagnóstico | `render_core` | `[render] pilar: desligado com o 4:3` quando a troca encontra um pilar ligado |
| Só validação | `game_host`, `main_sdl` | `BS_PROPORCAO_TROCA=<vblank>:<largura>[,...]` (a troca pelo mesmo caminho do menu), `BS_VK_FALHAR=<vblank>[:sempre]` (falha do dispositivo transitória ou permanente), `BS_VK_ALTURA_MAX=<altura>` (uma GPU com esse limite) |

**Verificado (o pedido do mantenedor):**

| Transição | Prova |
|---|---|
| 4:3 → 16:9 → 4:3 → 16:9 no gameplay | H7 (sem janela, VBlanks 3000/3900/4500) e J6 (pelo menu da pausa, na janela) |
| Vulkan ↔ software | P3 (pelo menu) e H7 (a falha permanente) |
| Recuperação e perda do dispositivo | H7 (transitória: continua no largo; permanente: o software até o fim, sem imagem larga, o quadro igual ao do 4:3) e J7c (a janela tira o 16:9) |
| Limites de resolução e de memória | unitários; J7a (2160p com o limite simulado: 1440 em uso no 16:9, a escolha inteira de volta, nenhum render acima do limite) |
| Recuo para o SDL com a altura reduzida | J7b |
| Pilar, marcas do HUD, slots | H7: o pilar religa depois da volta ao 16:9; o quadro do jogo no 4:3 igual ao da execução 4:3 (as marcas limpas: o HUD de volta ao lugar); nenhuma imagem larga no 4:3 (nenhum slot velho) |
| Menus, filmes e legendas | J6 (a pausa e as configurações da N9.2 nas duas proporções), J1–J5 e P1–P5 |
| Redimensionamento da janela | sem estado: a área é recalculada a cada quadro (`frame_area`); J1/J2 (1280×720 e 1024×768); o redimensionamento durante o jogo fica para a conferência visual do mantenedor |

**Resultados (2026-11-10, exes do worktree com a 5B):**

| Teste | Resultado |
|---|---|
| `bs_unit_tests` / `bs_render_tests` | 371/0, 91/0 (novos: `graphics_internal_height_limit_fase5b`, o rótulo "em uso" e o CRT pela altura em uso no menu, `vulkan_gave_up` no recuo do render) |
| ctest `msvc-debug` / `msvc-release` | 30/30, 23/23 |
| H1–H7 (sem janela) | 0 falhas. H7: estado e efeitos idênticos ao 4:3 nas quatro execuções (as trocas, a 16:9 do início, a falha transitória e a permanente); as três trocas na ordem; imagem larga nas capturas largas e nenhuma no 4:3; o quadro do jogo no 4:3 igual ao da execução 4:3 (0 px); a imagem larga depois da volta igual à da execução 16:9 desde o início (0 px); o pilar religa; falha transitória: recuperado e ainda largo; permanente: o software até o fim, sem imagem larga, o quadro igual ao 4:3 do Vulkan (0 px) |
| J6 (janela, pelo menu da pausa) | 0 falhas: 12 capturas no 4:3 antes, 22 no 16:9 (todas com cena nas laterais), 19 no 4:3 depois (laterais pretas: nenhum quadro largo velho) |
| J7a (2160p, limite simulado de 2160 no 4:3) | 0 falhas: os alvos na ordem 2880×2160 → 2562×1440 → 2880×2160; nenhum render acima do limite; o aviso "2160 pedida, 1440 em uso" |
| J7b (16:9 com 1440 em uso e o recuo para o SDL) | 0 falhas: a escala volta a 4,5 (2880×2160) depois do recuo |
| J7c (o Vulkan desiste no 4:3) | 0 falhas: "4:3 (o renderizador ficou no software...)" e o 16:9 sai do menu |
| P1–P5 | 0 falhas |

- **J6, critério do primeiro trecho:** a primeira execução contou as capturas do boot (logotipos e telas pretas) como "4:3
  antes" e falhou em 14 delas por "área do jogo vazia"; o log não dá o VBlank do fim do carregamento, e o trecho passou a ser
  os 12 s de gameplay antes da primeira pausa. Os outros dois trechos e todos os critérios ficaram iguais.
- Memória: pico de 5,1 GB do `bluestinger`, commit livre mínimo de 19,7 GB.

**Revisão Codex `codex-20261110-011045`** (três médios e um baixo, todos aceitos):
- **Média — a desistência do Vulkan com o 4:3 em uso não tirava o 16:9 do menu:** a janela consulta `vulkan_gave_up` sempre; J7c.
- **Média — o recuo para o SDL não devolvia a altura do 4:3:** `drop_wide` reaplica a configuração inteira; J7b.
- **Média — o CRT decidia pela altura escolhida:** a altura em uso, no `apply` e no menu; unitário novo.
- **Baixa — nenhum teste exercitava a altura junto com a largura:** `BS_VK_ALTURA_MAX` (só validação) e J7a/J7b.

### 16.3 Fase 5C: a telemetria e a matriz de desempenho

**A telemetria (D5-3):** no executável de produto, desligada por padrão; liga só com `BS_TELEMETRIA=<arquivo .csv novo>`
(`_DE`/`_ATE` limitam o trecho). Regras, colunas e origem de cada número em `docs/TELEMETRIA.md`: somente leitura, nada pela
rede (volume remoto recusado), criação atômica sem sobrescrever, nenhum caminho ou dado pessoal no arquivo, o resumo
gravado depois das threads pararem. Código em `native/platform/telemetry.{h,cpp}`; os pontos de medida no `vk_device`
(timestamps em cinco pontos do render, `VK_EXT_memory_budget` ligada só com a telemetria), nos escopos de memória
(`MemScope`, inerte desligada), no `render_core`, no `game_host` e no `main_sdl` (um quadro conta só com a apresentação
bem-sucedida e a janela visível). A matriz: `tools/dev/n95_desempenho.py`.

**Bateria de 2026-11-10** (`tmp/sessao-20261110/bateria_5c.ps1`; os três builds do mesmo código, `bs_unit_tests` 372/0;
todos os 45 casos "completo", o resumo gravado no `_ATE`). Exe de produto, Remastered, modo espectador, Vulkan; NVIDIA GeForce
RTX 5080 (16 GB) e AMD Radeon(TM) Graphics (a integrada, memória compartilhada). Na janela, 1280×720 com V-Sync a 60 Hz (o
padrão do produto). O jogo desenha a 30 renders por segundo (um a cada dois VBlanks).

**NVIDIA, janela, o corredor (RCG02, VBlanks 3600–4700):**

| Config | fps | frametime p99 (pico) | acima de 25 ms | VBlanks/s mín. | GPU por render (média / p99) |
|---|---|---|---|---|---|
| 4:3 480 | 59,9 | 20,0 ms | 0 % | 59 | 0,76 / 1,38 ms |
| 16:9 480 | 60,0 | 19,5 ms | 0 % | 60 | 0,92 / 1,61 ms |
| 4:3 1080 | 60,0 | 19,7 ms | 0 % | 60 | 1,88 / 4,56 ms |
| 16:9 1080 | 59,8 | 20,8 ms | 0 % | 59 | 2,40 / 6,89 ms |
| 4:3 1440 | 59,8 | 21,2 ms | 0 % | 59 | 3,02 / 8,13 ms |
| 16:9 1440 | 59,8 | 21,3 ms | 0 % | 60 | 3,54 / 9,75 ms |
| 4:3 2160 | 59,9 | 23,6 ms | 0 % | 59 | 3,38 / 11,26 ms |
| 16:9 2160 | 59,9 | 22,8 ms | 0 % | 60 | 3,01 / 9,22 ms |

- **Nenhum quadro perdido** (0 % acima de 25 ms) em nenhuma das oito; o jogo no tempo real. A coluna "acima de 16,7 ms"
  marca 30–48 % só pela oscilação do V-Sync em torno de 16,67 ms (o p50 é 16,66).
- CPU: a thread do jogo (o SH-4 recompilado e o render) usa 23,8–26,1 % de um núcleo, a da janela 0,0–1,9 %, sem relação com a altura.

**NVIDIA, sem janela** (o render inteiro, sem apresentação; o jogo corre acima do tempo real, 71–110 VBlanks/s, então o
relógio da GPU varia e a ordem entre configurações vizinhas não é estrita). GPU por render, média em ms:

| Config | corredor | cais (RCG00) | exterior (RCG03) | combate (`passo_combate`) |
|---|---|---|---|---|
| 4:3 480 | 0,75 | 1,74 | 1,24 | |
| 16:9 480 | 0,90 | 2,05 | 1,78 | |
| 4:3 1080 | 1,36 | 3,09 | 2,82 | 3,09 |
| 16:9 1080 | 1,72 | 2,19 | 2,19 | 1,80 |
| 4:3 1440 | 1,82 | 2,53 | 2,24 | |
| 16:9 1440 | 1,39 | 2,34 | 2,66 | |
| 4:3 2160 | 1,58 | 3,93 | 3,67 | |
| 16:9 2160 | 1,86 | 4,53 | 4,31 | 4,89 |

O pior p99 por render na RTX 5080 fica em 11,3 ms (4:3 2160 na janela): sobra larga contra os 33 ms de um render a 30 Hz.

**AMD Radeon integrada** (o corredor; GPU por render, média / p99):

| Config | sem janela | janela |
|---|---|---|
| 4:3 480 | 5,50 / 11,2 ms | |
| 16:9 480 | 6,24 / 12,6 ms | |
| 4:3 1080 | 22,6 / 49,1 ms | 23,1 / 49,4 ms; fps 59,8, mas 12,0 % dos quadros acima de 25 ms e o jogo cai a 34 VBlanks/s |
| 16:9 1080 | 26,0 / 55,6 ms | 26,5 / 54,9 ms; fps 59,2, 13,8 % acima de 25 ms, o jogo cai a 30 VBlanks/s |
| 4:3 1440 | 36,9 / 83,0 ms | |
| 16:9 1440 | 43,8 / 95,1 ms | |
| 4:3 2160 | 81,2 / 184 ms | |
| 16:9 2160 | 94,9 / 207 ms | |

- **Na integrada, 1080 não se sustenta:** no trecho pesado do corredor (o fim, câmera de canto) o render passa de 40 ms,
  o jogo fica mais lento que o tempo real (30–40 VBlanks por segundo em vez de 60) e a janela repete quadros. O `fps` da
  janela continua perto de 60, porque a apresentação segue a 60 Hz: a lentidão aparece no `vblanks` e no `acima_25_pct`
  (anotado em `docs/TELEMETRIA.md`). 480 linhas cabem (p99 de 11–13 ms); 720 não foi medido.
- O 16:9 custa 13–19 % a mais de GPU que o 4:3 na mesma altura na integrada; na RTX 5080 a diferença some no ruído do relógio.

**Memória da GPU** (pico por categoria, MiB, medido pelo tamanho das alocações; iguais nas duas GPUs a menos de 2 MiB):

| Altura | alvos 4:3 | alvos 16:9 | slots | texturas | uso do driver (16:9, NVIDIA) | orçamento do driver |
|---|---|---|---|---|---|---|
| 480 | 95 | 97 | 34 | 3,6–11,3 | 359 | 15 209 (NVIDIA), 10 252 (AMD) |
| 1080 | 398 | 610 | 34–36 | | 902 | |
| 1440 | 707 | 1 084 | | | 1 402 | |
| 2160 | 1 586 | 2 432 | 128 | | 2 817 | |

- **Medido × estimado:** o limite da resolução interna (`vk_device.cpp`, N7c) supõe ~96 bytes por pixel do alvo e reserva
  1/4 da maior memória local. Os alvos medidos dão **~268 B por pixel no 4:3** (1080, 1440 e 2160 iguais) e o 16:9 custa
  **1,53×** o 4:3 na mesma altura (os pixels são 1,33×). Com a folga de 1/4, os alvos no limite ficariam em ~70 % da
  memória no 4:3 e ~80 % no 16:9, sem estourar; mas a constante e o comentário estão errados por ~2,8×. Só pesa numa GPU
  com menos de ~2,4 GB de memória local (abaixo disso o limite tira o 2160 do menu). **Não corrigido** (muda o que o menu
  oferece nessas GPUs): fica como recomendação, com a decisão do mantenedor.
- **O orçamento:** o pico do uso do driver (2,8 GB no 16:9 2160) é 18,5 % do orçamento da RTX 5080. Na NVIDIA o driver
  usa ~220–250 MiB além das alocações do jogo; na AMD o uso do driver bate com elas.
- **Indisponível:** a memória do driver por fora das alocações, por categoria; o tempo de GPU da apresentação (a cadeia de
  troca da janela não tem timestamps); a integrada em 720.

**Vazamento (a memória privada do processo, primeira linha × última × pico):** no corredor, +8 a +14 MiB em todas as 26
execuções, igual em todas as alturas e nas duas GPUs (texturas e dados da sala que entram no trecho, não alvos); na rota do
heliporto (256 s), +22 a +56 MiB, também sem relação com a altura, e a curva sobe e desce com as salas (ex.: 4:3 480:
440 → 475 → 456 → 466 MiB); no combate (60 s), −1 a +2 MiB. **Nenhum sinal de vazamento.**

**A/B** (sem janela, `passo_load_slot1`, 4700 VBlanks, o "trabalho por campo" do jogo, processos alternados):

| Caso | Sem | Com | Diferença |
|---|---|---|---|
| A marca leve do HUD (`BS_HUD_MARCA=1`, validação, 4:3; 3 + 3) | 9,697 ms | 9,953 ms | +0,26 ms (+2,6 %) |
| A telemetria ligada (produto, 16:9; 3 + 3) | 7,663 ms | 7,940 ms | +0,28 ms (+3,6 %) |
| A telemetria desligada (o produto da 5B, `304922c`, × o da 5C; 5 + 5) | 7,698 ms (desvio 0,18) | 7,830 ms (desvio 0,11) | +0,13 ms (+1,7 %) |

- A variação entre repetições do mesmo lado chega a 0,5 ms; as duas primeiras diferenças estão perto desse ruído.
- **Desligada:** a diferença de +0,13 ms fica abaixo da variação do próprio lado da 5B (7,48–7,94 ms), mas nos pares da mesma
  repetição a 5C nunca saiu mais rápida (0; +0,26; +0,10; +0,17; +0,13 ms). Desligados, os pontos de medida custam a leitura de
  um `bool` algumas vezes por campo (nanossegundos: nenhum relógio, alocação, consulta na GPU, timestamp ou extensão); o que
  sobra não se separa do arranjo do código entre os dois builds. Registrado como medido (até +1,7 %), não como zero.

**Revisões Codex:**
- **`codex-20261110-014042`** (quatro médios, três baixos; todos aceitos e corrigidos antes da bateria): a criação atômica
  (`CREATE_NEW`/`O_EXCL`), o volume remoto pela pasta resolvida, o fim depois do `join` com o `atexit` sem bloquear, a matriz
  que encerrava a janela antes do resumo, o fps de apresentações que falharam ou com a janela minimizada, o `_DE` além do fim
  ("não alcançado"), o `MemScope` inerte com a telemetria desligada.
- **`codex-20261110-041638`** (dois médios, dois baixos; todos aceitos):
  - **Média — o tempo minimizado virava um frametime:** `FrameClock` e `frame_gap()`: uma volta sem quadro visto recomeça
    a base; unitário `telemetry_frame_clock_fase5c`.
  - **Média — a pasta podia mudar entre a consulta e a abertura (TOCTOU):** `create_exclusive` confere o volume do arquivo
    aberto pelo handle (`GetFinalPathNameByHandleW`; `fstatfs`, com `O_NOFOLLOW`); recusado, nada é gravado e no Windows o
    arquivo é apagado pelo handle.
  - **Baixa — `quadros` contava os intervalos (N − 1):** um contador próprio das apresentações vistas.
  - **Baixa — os testes não exercitavam o arquivo:** a criação exclusiva (cria, nunca abre um existente, o conteúdo intacto,
    a leitura durante a gravação) e o relógio dos quadros. A pasta do teste passou do `%TEMP%` para a pasta de execução.
  - Na correção, a primeira versão abria o arquivo com o acesso de exclusão e um leitor (a matriz) não conseguia abrir o
    CSV durante a execução; o teste com janela pegou, o handle que grava ficou sem esse acesso (a exclusão é por
    `ReOpenFile`) e o unitário lê o arquivo durante a gravação.
- **Depois das correções:** `bs_unit_tests` 373/0; ctest 30/30 (`msvc-debug`) e 23/23 (`msvc-release`, 5 pulados sem a
  imagem do disco); a recusa de um arquivo existente (o conteúdo intacto, o jogo segue); a janela no corredor 4:3 480: 1101
  quadros apresentados em 1100 VBlanks, a soma das linhas igual ao resumo, nenhum quadro acima de 25 ms. A bateria da
  matriz acima foi feita antes desta revisão: a contagem `quadros` tinha um a menos no total (o primeiro quadro); a janela não foi minimizada, então frametimes, GPU,
  memória e CPU valem como estão.
