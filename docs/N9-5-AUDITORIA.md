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
  - Há outras cópias com a distância 500 e alturas de 480 e 450 em `0x8C2D96xx` e `0x8C2D9C7x`. São telas de outros
    contextos [H].
- **[P] Quem referencia `0x8C24CCB4`:** 9 rotinas, entre elas o laço de gameplay `8C052240`, `8C0584CC`, `8C062E88`,
  `8C063530`, `8C064560`, o título (`8C078AC0`, `8C07AD80`) e `8C08A1CC`. O campo seguinte (`0x8C24CCC8`, um ponteiro) tem 72
  referências. `8C064560` e `8C08A1CC` projetam pontos isolados (distância × coordenada ÷ profundidade): é o uso de
  marcadores ou de testes de "na tela" [H sobre a finalidade].
- **[P] Experimento** (`BS_TELA_TESTE=854,427`, só no build de validação: reescreve a largura e o cx da estrutura a cada
  VBlank):
  - LOAD do slot 1 e caminhada no corredor, RAM e quadro comparados nos passos 1855 e 2300;
  - **a área de estado do jogo é idêntica** (0 bytes, fora a câmera e a tela);
  - **o quadro também é idêntico**.

  Conclusão: o desenho dos modelos **não lê** a largura nem o cx a cada quadro. Ele usa constantes calculadas antes, na
  matriz de projeção que o `njSetScreen` monta [H forte]. Nesse trecho, a lógica não dependeu desses campos.
- **[N] Onde ficam as constantes de projeção do desenho:** precisa rastrear quem escreve a estrutura e a matriz (o
  `njSetScreen` e a carga do XMTRX/FTRV). É o **bloqueador B1**.

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
