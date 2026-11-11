# N9.6: iluminação, sombras e materiais — auditoria e fase 0 (2026-11-10)

Autorizado pelo mantenedor em 2026-11-10 ("Pode continuar para a 9.6"). O plano (`docs/N9-PLANO.md`, N9.6) pede um
pipeline integrado à geometria 3D, não só filtros de imagem, e **provas técnicas antes de escolher os algoritmos**. Esta
fase 0 é só investigação, no build de validação; o produto e a edição Original não mudaram. A escolha das técnicas é do
mantenedor (seção 4).

Marcação: **[P]** provado (código, disassembly ou medida), **[I]** inferido, **[A]** em aberto.

## 1. Método

- IRs de render guardadas pelo jogo (`BS_RENDER_IR_GUARDAR=<render>:<arquivo>`, exe de validação de `2346602`, sem janela,
  4:3) na rota `passo_heliporto`: RCG00 (o cais, render 15400), RCG02 (o corredor, 21550) e RCG03 (o exterior, 24000), e a
  IR do cais da fase 1 do N9.5 (render 15581 da rota de combate). O render 18500 escolhido para RCG00A caiu numa tela 2D
  (0 desenhos 3D) e não conta.
- `bs-ir-tela geometria` (novo, `tools/ir_tela/geometria.cpp`, ferramenta só de investigação): reconstrói a posição de cada
  vértice no espaço da câmera, as normais por triângulo, a imagem das normais (rasterizador em software), os picos de
  direção por pixel visível e o sinal da luz nas cores. `--vertices` exporta os vértices para o ajuste da luz
  (`tmp/sessao-20261110/n96/luz_ajuste.py`, local).
- Disassembly (`bs-disasm`) do percurso do modelo `8C15F078` e da rotina por vértice `8C17EC50`.
- Código do backend Vulkan (`native/render/vulkan/vk_device.cpp`).

## 2. O que o jogo entrega

### 2.1 A IR não traz normais, posições nem luzes [P]

O vértice da IR (`native/render/render.h`) tem x e y de tela, 1/w, u, v, a cor ARGB e a cor de deslocamento. O estado
do desenho tem lista, blend, textura, fog e o bit de sombra dos volumes. Não há normal, posição no mundo, matriz nem
luz: o que chega ao TA já está projetado e aceso.

### 2.2 A posição na câmera volta exata pela projeção [P]

O Ninja projeta `sx = cx + dist·x/z`, `sy = cy + dist·y/z` (auditoria do N9.5, seção 11.1), com dist 500, cx 320 e cy 240 no
gameplay. Com z = 1/(1/w): `x = (sx − cx)·z/dist`, `y = (sy − cy)·z/dist`. Uma escala desconhecida em 1/w só escala as
posições por igual e não muda ângulos; os ângulos entre os planos dominantes provam a distância:

| Sala | Picos de normal (fração dos pixels 3D) | Ângulos entre os picos |
|---|---|---|
| RCG02 (corredor) | chão 0,32, parede 0,21, fundo 0,14, teto 0,09, parede 0,06 | **88,1–91,9°** entre chão, paredes, teto e fundo; 179,6° e 180,0° entre as faces opostas |
| RCG03 (exterior) | 0,69, 0,18, 0,05 | 90,0°, 91,5°, 92,0°, 92,4° |
| RCG00 (cais, curvo) | chão 0,52 e 0,16 (13,9° entre eles, o cais inclinado) | paredes a 92,8–103,9° do chão |

O resto do erro é a quantização de 8 bits da imagem das normais. A imagem das normais do corredor e do cais
(`tmp/sessao-20261110/n96/*/normais.png`) mostra o chão, as paredes, o teto e os personagens no lugar certo.

### 2.3 A geometria é pobre em detalhe; o detalhe está nas texturas [P]

- O corredor RCG02: cada parede, o chão e o teto são **um plano** (os cinco picos levam 0,82 dos pixels); grades,
  painéis e lâmpadas são textura. Personagens com facetas visíveis.
- Faixas planas (todo triângulo da faixa com a mesma normal, ≤ 2°): 58 de 698 no RCG02, 281 de 819 no RCG00, 279 de 957 no
  RCG03.
- Posições repetidas em dois ou mais desenhos: 2188 de 2559 (RCG02), 2258 de 3012 (RCG00), 2698 de 3655 (RCG03). As
  normais suaves podem ser feitas pela posição, sem os dados do modelo.
- 1/w de 1,0 (z = 1) aparece em todas as salas: vértices no plano próximo, a tratar no shader.

Consequência: luz por pixel só da geometria fica plana nas paredes; relevo pede mapas de normais derivados das texturas
(ligação com o N9.8).

### 2.4 A luz original é por vértice, na CPU, pelo Ninja [P]

- O percurso do modelo `8C15F078` só aceita o bloco de vértices do tipo 41 do formato chunk do Ninja (`cmp/eq #41` em
  `8C15F0EC`): `NJD_CV_VN`, posição **com normal**. Os modelos têm normais de autor.
- A rotina por vértice `8C17EC50` faz `ftrv` da posição e da normal (`8C17EC6C`, `8C17EC76`), `fipr` N·L (`8C17EC94`,
  `8C17ECD6`), `fipr` do vetor consigo e `fsrra` (`8C17ECDA`, `8C17ECF0`: distância e atenuação de luz pontual) e acumula
  a cor com `fmac`. **[I]** luzes direcionais e pontuais somadas por vértice.
- O resultado chega à IR como a cor Gouraud (e o especular na cor de deslocamento): cor = luz × cor do material do
  vértice, já multiplicadas. Desenhos 3D com cor variando no desenho: 874 de 887 (RCG02), 974 de 1116 (RCG00), 995 de 1244
  (RCG03). Especular ligado em 693–835 desenhos, não nulo só no cais (116 e 416).
- **Uma luz direcional não explica as cores** (normais suaves pela posição, a = ambiente + b·max(0, n·L), L procurado em
  4000 direções): R² global 0,25 (RCG02), 0,37 (RCG00), 0,28 (RCG03), 0,30 (cais da rota de combate); mediana por desenho
  0,20–0,34. Coerente com as luzes pontuais e várias luzes. **Não dá para separar luz e material só pela IR.** [A] ler a
  lista de luzes do Ninja na RAM (estrutura e número) exige rastrear quem preenche os parâmetros que `8C17EC50` lê.

### 2.5 Sombras originais [P]

Os volumes modificadores (N8.7) escurecem o que fica dentro deles (`FPU_SHAD_SCALE`): são as sombras do jogo. Não há
mapa de sombras. Uma sombra nova precisa de geometria vista da luz; a IR só tem a vista da câmera.

### 2.6 A profundidade já fica guardada depois dos opacos [P]

O passe A do Vulkan (opacos e punch-through) grava a profundidade D32 (o 1/w cru, `STORE_OP_STORE`, `SAMPLED_BIT`) e a
deixa em `DEPTH_STENCIL_READ_ONLY_OPTIMAL` para os volumes e as translúcidas (`vk_device.cpp`, `target()`). É o
pré-requisito do SSAO no N7 25.2: entre o passe A e o B, a profundidade dos opacos está pronta e a posição da câmera sai
dela pela mesma fórmula de 2.2. As translúcidas (autosort por pixel) não escrevem profundidade.

### 2.7 O que o N7 já tem

Pós-processamento por imagem (bloom, tonemap, cor, nitidez, CRT; `docs/N7-PLANO.md` seção 21). O N9.6 vai além disso:
age dentro do render, com a geometria.

## 3. Opções por técnica

| Tema | Opções | Recomendação |
|---|---|---|
| **D6-1 De onde vem a geometria** | **A.** Reconstruída da IR e da profundidade (2.2): posição exata, normal do triângulo ou suave pela posição; nenhum código do jogo muda. **B.** Normais de autor do Ninja: marca no percurso `8C15F078`/`8C17EC50` (como a marca leve do HUD, N9.5 3B) levando a normal de cada vértice à IR; mais fiel nos personagens, invasiva (caminho quente, ordem CPU → TA, custo a medir). **C.** A primeiro, B só se as normais de A não bastarem | **C** (A agora) |
| **D6-2 Modelo de luz** | **i.** Manter a luz original da cor do vértice como base e somar por cima (AO multiplica, luzes novas somam). **ii.** Reacender por pixel no lugar da luz original: exige separar luz e material, o que a IR não permite (2.4), e muda a arte | **i** |
| **D6-3 Oclusão ambiente** | SSAO/GTAO sobre a profundidade do passe A, só nos opacos, meia resolução com filtro bilateral | GTAO em meia resolução |
| **D6-4 Sombras** | **a.** Os volumes originais ficam e ganham borda suave. **b.** Sombras de contato em espaço de tela (raio curto na profundidade). **c.** Mapa de sombras com os triângulos do quadro (só o que a câmera vê faz sombra; ocluso fora da tela não) | a + b; c só como experimento com o limite registrado |
| **D6-5 Materiais** | Catálogo por textura (o ID de conteúdo da água do N9.7, sem arte no repositório): metal, vidro, plástico, molhado, emissivo; especular e Fresnel por material; relevo por mapa de normais derivado da luminância da textura (o feito à mão fica no N9.8/N10) | catálogo por textura + relevo derivado |
| **D6-6 Emissivos e luzes dinâmicas** | Superfícies emissivas do catálogo (as lâmpadas do corredor) viram luzes pontuais por quadro, no espaço da câmera; acúmulo por blocos (compute) | sim, com limite de luzes por perfil |
| **D6-7 Onde roda** | Só Remastered com o Vulkan, como o 16:9; Original e software intactos; desligado = imagem idêntica à de hoje | igual ao N9.5 |

## 4. Decisões para o mantenedor

1. **D6-1:** A (reconstrução), B (normais do Ninja) ou C (A primeiro). Recomendado: C.
2. **D6-2:** manter a luz original e somar (i) ou reacender (ii). Recomendado: i.
3. **D6-4:** quais sombras (a, b, c).
4. **Ordem e corte das fases** (seção 5).

### 4.1 Escolhas do mantenedor (2026-11-10)

- **D6-1: C.** A geometria vem da IR e da profundidade agora; as normais do Ninja (B) só se as de A não bastarem nos
  personagens.
- **D6-2: i.** A luz original da cor do vértice fica como base; AO multiplica e as luzes novas somam.
- **D6-4: a + b.** Os volumes originais com a borda suave e as sombras de contato em espaço de tela. O mapa de sombras (c)
  não entra.
- **Commits locais por fase**, com o master avançado preservando a água; sem push, tag nem release.

## 5. Fases propostas (cada uma com autorização)

| Fase | Entrega | Aceitação |
|---|---|---|
| 1 | Buffer de geometria a partir da IR no Vulkan (normal e posição da câmera pela profundidade) + GTAO, só Remastered; diagnóstico das normais só na validação | AO no RCG02/RCG00/RCG03 com custo medido (NVIDIA e AMD, 480/1080); Original × `referencias_n87` 10/0; estado por passo idêntico (H do N9.5); desligado = imagem igual |
| 2 | Catálogo de materiais por textura, especular/Fresnel, relevo derivado, superfícies molhadas | idem, por material, numa sala de referência |
| 3 | Emissivos e luzes pontuais dinâmicas | idem; limite de luzes por perfil |
| 4 | Sombras: volumes originais com borda suave e sombras de contato (D6-4: a + b) | idem |
| 5 | Menu e `graficos.toml` (qualidade da iluminação), matriz de desempenho, bateria | perfis dentro do orçamento; AMD integrada medida |

## 6. Riscos (para o N9.12)

- A AMD integrada já não sustenta 1080 na janela (N9.5 16.3): cada efeito soma custo; os perfis precisam do "desligado".
- Normais planas nas paredes (2.3): sem o relevo derivado, a luz nova fica chapada.
- Translúcidas sem profundidade: AO e sombras de contato só nos opacos.
- Vértices com z = 1 (plano próximo): posição degenerada, a ignorar no shader.
- O HUD e as telas 2D ficam fora: o critério de 1/w constante (N9.5 fase 2) e a marca leve do HUD (3B) já separam.
- A luz original não é recuperável pela IR (2.4): uma iluminação que dependa dela pede a opção B.
- C8 continua bloqueado; o resto do jogo fora das salas medidas não foi visto.

## 7. Fase 1: oclusão ambiente (2026-11-10)

Autorizada pelo mantenedor com a fase 0; a segunda parte (correções da revisão, meia resolução e qualidades) em
2026-11-10, depois da bateria da primeira versão. Só a Remastered com o Vulkan; o SSAO continua **desligado por padrão em
todos os presets** (decisão do mantenedor, D6 desempenho).

### 7.1 Método

- **Marca da cena** (`frame_prep`, `Batch::scene3d`): opacos e punch-through cujo 1/w é finito, positivo e varia no
  desenho (o critério da fase 2 do N9.5). O passe A grava o bit 1 do anexo de sombra (`F_SCENE` no `pvr.frag`); o AO só
  lê e só escreve esses pixels. O HUD, as telas 2D e o fundo de limpeza ficam fora.
- **AO** (`native/render/vulkan/shaders/ao.comp`, entre o passe A e os volumes): a posição na câmera sai da profundidade
  pela projeção do Ninja (cx, cy no centro do quadro do jogo, dist 500); a normal pelos vizinhos de profundidade mais
  próxima; raio 60 na câmera, queda `1 - |v| / 3R`, espessura `2R` (sem o halo de um objeto muito à frente), z ≤ 1,5
  ignorado. A cor da cena é multiplicada por `mix(1, ao, 0,8)`: a luz original fica (D6-2 i).
- **Qualidades** (`[efeitos] ssao_qualidade` no `graficos.toml`, `--ssao-qualidade` só na execução; a média é o padrão
  quando o SSAO é ligado):

  | Qualidade | Resolução do cálculo | Amostras | Imagem auxiliar |
  |---|---|---|---|
  | alta | o alvo inteiro (a primeira versão) | 8 direções × 4 passos | rgba8 no alvo: 4 B/px |
  | média | meia resolução | 8 × 4 | rgba32f em ⌈w/2⌉ × ⌈h/2⌉: 4 B/px do alvo |
  | baixa | meia resolução | 4 × 3 | idem |

- **Meia resolução:** um representante por bloco 2 × 2, em xadrez (o de 1/w maior nas casas pares, o de 1/w menor nas
  ímpares, para a frente e o fundo de uma borda aparecerem os dois); as amostras continuam na profundidade inteira; a imagem
  guarda o AO e o 1/w do representante. A reconstrução no alvo inteiro usa os 4 × 4 representantes em volta, com o peso
  `exp(-|Δ(1/w)| / 0,03·(1/w))`; sem nenhum representante a menos de ~9 % de diferença de profundidade (peso `exp(-3)`), o
  pixel fica sem AO, nunca com o de outra superfície.
- **Recursos sob demanda:** os pipelines nascem na primeira vez que o AO liga; só a imagem da qualidade escolhida existe;
  a outra sai na troca e as duas saem no primeiro submit com o AO desligado (`VulkanStats::ao_image_bytes`). Uma falha
  de criação desliga só o AO (o log diz uma vez). A estimativa do limite da resolução interna passou de 96 para 96 + 4
  B/px (a correção da constante continua no N9.12).
- **Ajuste automático:** não foi feito. A qualidade é escolha explícita do jogador; nenhuma troca por quadro. Um orçamento
  de GPU por perfil fica para a fase 5 (menu), com as medições abaixo.

### 7.2 Revisões Codex

- `reviews/codex-20261110-113738.md` (primeira versão): **MÉDIA**, superfícies 3D de frente exata para a câmera (1/w
  constante) ficam fora da cena, como o HUD: aceita e **transferida à fase 2** (marca de cena pela proveniência,
  arquitetura aprovada pelo mantenedor em 2026-11-10); o lado seguro está fixado em teste (a parede frontal não muda).
  **BAIXA**, objetos do AO criados sempre: corrigido (criação sob demanda). **BAIXA**, a matriz padrão sem `+ao`: aceita em
  parte (as baterias passam os pares explícitos).
- `reviews/codex-20261110-161728.md` (meia resolução): **BAIXA**, faltava um alvo de lados ímpares e a prova da liberação
  das imagens: corrigido (`ao_image_bytes`; teste com 641 × 481 e a cada troca e desligamento). **BAIXA**, sem oráculo de
  que a alta repete a primeira versão: aceita; um hash no teste dependeria do driver, então o oráculo foram as capturas reais
  (7.3: 28 de 28 iguais bit a bit ao binário da primeira versão).

### 7.3 Resultados dos testes

Duas baterias, separadas: **(A) binários de 14:31**, a primeira versão com as correções da revisão 113738 (só a qualidade
alta existia); **(B) binários de 16:16**, com a meia resolução e as qualidades; os de 19:43 só somam a estatística e os
testes da revisão 161728 (unitários, render, ctest e L1–L4 na média repetidos neles). A bateria anterior a essas correções
(13:19–14:14) fica só como histórico.

| Teste | (A) | (B) |
|---|---|---|
| `bs_unit_tests` / `bs_render_tests` | 374/0, 93/0 | 375/0, 94/0 (19:43) |
| ctest debug / release | 30/30; release 22/23: `n0_native_limpo` pegou o cabeçalho SPIR-V novo criado durante a execução (a palavra `0x00706572`, o nome `rep` na depuração do SPIR-V); a variável foi renomeada, a catraca não mudou | 30/30, 23/23 (5 pulados sem o disco, não contados) |
| `n96_luz` L1–L4 | 34 ok, 0 falhas | 0 falhas na alta, na média e na baixa |
| H1–H7 (`n95_headless`) | 0 falhas | 0 falhas |
| R1–R4 (`test_remaster`) | 4/0 | 4/0 |
| Original × `referencias_n87` (10 casos históricos) | 10 iguais | 10 iguais |

- **A alta é a primeira versão:** as 28 capturas da L (4:3, 16:9, 1080, diagnóstico, produto) da bateria (B) na alta são
  iguais bit a bit às da (A). No teste sintético, a quina dá 0,786 na parede e 0,781 no chão, os números de antes.
- **Média e baixa contra a alta, nas capturas reais (RCG02, VBlanks 3800 e 4640):** o escurecimento médio fica a até 0,1
  ponto percentual da alta (3800: alta 3,98 %, média 3,94 %, baixa 4,08 %); no 1080, a média difere da alta em 8 % dos
  pixels, com mediana 1,2 nível e p99 4; a baixa em 11 %, p99 12. Áreas com AO só numa das duas: 0,2–0,9 % da imagem.
- **Vazamento:** no teste sintético (parede estreita a z 600 na frente de um plano a z 3000), o fundo junto à borda fica em
  1,000 nas três qualidades; o HUD bit a bit; o AO só escurece.

### 7.4 Custos (binários de 16:16, telemetria do produto, `n95_desempenho.py`)

GPU por quadro (ms, média do trecho; sem janela, o render inteiro na GPU sem apresentação). NVIDIA RTX 5080:

| Configuração | Corredor | Cais | Exterior |
|---|---|---|---|
| 16:9@1080 desligado | 1,69 | 1,51 | 2,62 |
| alta / média / baixa | 2,77 / 2,02 / 1,82 | 3,25 / 2,51 / 2,27 | 2,80 / 2,47 / 2,46 |
| 16:9@2160 desligado | 1,99 | 4,53 | 4,28 |
| alta / média / baixa | 7,42 / 3,38 / 2,55 | 6,59 / 5,03 / 4,63 | 6,45 / 4,65 / 4,43 |

AMD Radeon integrada (corredor): 4:3@480 desligado 5,54; alta 11,36, média 7,80, baixa 6,66. 16:9@1080 desligado 26,0;
alta 124,3, média 55,4, baixa 39,7.

Na janela (1280 × 720, V-Sync, modo espectador, corredor):

| Configuração | fps | frametime p50 / p99 pico (ms) | quadros > 16,7 ms | GPU (ms) |
|---|---|---|---|---|
| AMD 16:9@1080 desligado | 59,4 | 15,4 / 53,4 | 33 % | 26,3 |
| alta / média / baixa | 29,4 / 53,0 / 57,3 | 14,7 / 163 · 13,5 / 87 · 15,8 / 68 | 37 / 36 / 42 % | 123,5 / 55,6 / 40,1 |
| NVIDIA 16:9@2160 desligado | 59,8 | 16,6 / 20,8 | 45 % | 3,18 |
| alta / média / baixa | 60,0 / 60,0 / 59,8 | 16,2 / 20,8 · 16,6 / 21,0 · 16,6 / 21,2 | 34 / 36 / 44 % | 7,74 / 3,75 / 3,34 |

Memória dos alvos (pico, MiB): 1080 610 → 619–620 com o AO; 2160 2 432 → 2 464–2 466; AMD 480 95,5 → 96,7 (≈ 4 B/px
do alvo nas três qualidades). Os números da bateria antiga (13:19–14:14, binários anteriores às correções, só a alta)
eram da mesma ordem (AMD 1080: 25,9 → 134,1; NVIDIA 2160 corredor: 2,00 → 7,42) e ficam só como histórico.

**Leitura:** na RTX 5080 a média custa até +1,0 ms em 1080 e +0,4–1,4 ms em 4K (a alta, até +1,7 ms e +5,4 ms). Na AMD integrada o
1080 já passa do quadro sem AO (26 ms) e mesmo a baixa soma ~14 ms: **o AO não é viável ali em 1080** e fica desligado;
em 480, a média soma 2,3 ms e a baixa 1,1 ms. As diferenças menores que ~0,5 ms nos trechos curtos da NVIDIA estão no
ruído (o exterior em 1080 desligado deu mais que ligado).

### 7.5 Problemas visuais (nas imagens de prova do diagnóstico, sem a janela)

- **Baixa:** manchas onduladas ao longo da quina vertical do corredor e em volta do personagem (4 direções); a média fica
  quase igual à alta, com um pouco de granulação na quina. A baixa é só para GPU fraca.
- O cabelo do personagem fica fora da cena (a cor do jogo) nas três; o piso de grade (translúcido) e a parede lateral de
  frente para a câmera também (cobertura de 56 % no quarto do VBlank 4640).
- A conferência visual na janela é do mantenedor.

### 7.6 Pendências transferidas

- **Fase 2:** a marca de cena pela proveniência (paredes e faces frontais; partículas, HUD, ambíguos e os modelos 3D dos
  menus fora, decisão de 2026-11-10); o catálogo de materiais por `TextureId`.
- **Fase 5 (menu):** a entrada SSAO do menu continua "em breve"; a qualidade e um orçamento de GPU por perfil, com as
  medições de 7.4.
- **N9.12:** a AMD integrada em 1080 sem AO viável; a cobertura da cena (frontais, translúcidas); dist fixa em 500 (o zoom
  do jogo muda o raio em poucos por cento); a constante de VRAM (agora 96 + 4 B/px); os artefatos da baixa.

## 8. Fase 2a: a marca de cena pela proveniência, medida antes de implementar (2026-11-10)

Autorizada pelo mantenedor ("Continuar para a fase 2"; arquitetura aprovada com condições em 2026-11-10). Antes de
instrumentar o produto, a pergunta foi medida, só com leitura e uma captura de validação.

- **Dados:** os 22 TSVs de proveniência da fase 3B do N9.5 (`tmp/sessao-20261109/fase3/c9`: corredor, combate, cais,
  exterior, inventário, menus de pausa, terminal ESER, título e C-GATE), com a cadeia de chamadas, a lista, o 1/w mínimo e
  máximo e a marca da validação de cada desenho; e a IR do quadro exato da L no quarto (render 2318 do `passo_load_slot1`,
  Remastered 4:3, exe de validação; o quadro do `bs-ir-tela` difere da captura do VBlank 4640 em 0,76 nível em média).
  Scripts locais `tmp/sessao-20261110/n96/f2_medir.py` e `f2_mascara.py`.
- **As entradas 3D do Ninja** (`8C15F856`, `8C15F8B8`, `8C161738`, `8C160C12`, `8C1601FE`) aparecem em 12 927 desenhos, todos
  com a marca **cena**; nenhum de tela, menu ou inventário. A identificação pelas rotinas é segura.
- **A lacuna da revisão Codex não ocorre nos dados:** os 10 180 desenhos opacos e punch-through de cena têm todos o 1/w
  variando; os 128 opacos com 1/w constante são todos HUD (`8C1736A0`/`8C07B4E0` e `8C16CED8`/`8C0899D6`). No quadro do quarto,
  os 1 003 desenhos opacos variam em profundidade e nenhum tem 1/w constante. A marca pela proveniência não acrescentaria
  nenhum pixel ao AO nas cenas medidas.
- **De onde vem a cobertura de 56 %:** dos pixels do quarto, 79,2 % são de opacos 3D (já marcados) e recebem o AO no passe
  A; 22,9 % deles ficam **cobertos por uma camada translúcida** desenhada depois, no passe B, que esconde a cena por baixo
  (os racks da parede direita, o piso de grade, o cabelo); os 20,8 % restantes são o fundo e o HUD. A falta de AO ali é da
  ordem dos passes (o AO antes das translúcidas), não da classificação.
- **Custo da marca:** com ela ligada, toda escrita da CPU na RAM anota o papel do bloco (`cpu/hud_scope.h`); hoje isso só
  liga na Remastered 16:9. Ligá-la também no 4:3 para o AO custaria CPU sem nenhum ganho medido.

### 8.1 Decisões do mantenedor (2026-11-10)

- **A marca de cena não entra no produto.** O critério do 1/w (fase 1) fica. No lugar dela, um **alarme só de validação**:
  `tools/dev/n96_cena.py <pastas>` lê os TSVs de proveniência (`*.ir.origem.tsv`) e falha se algum desenho opaco ou
  punch-through das entradas 3D do Ninja tiver 1/w constante; as entradas são conferidas contra as linhas de cena do
  `runtime/kamui/hud_table.h`. Nas 22 cenas: 10 180 desenhos, 0 alarmes (rc 0); negativo (um desenho do `8C15F856` com o
  1/w forçado constante): 1 alarme, rc 1; sem TSV: rc 1. Se o alarme tocar, a marca volta a ser avaliada. A revisão Codex
  20261110-113738 (MÉDIA) fica respondida por medição: o caso não ocorre nas cenas capturadas.
- **As translúcidas que cobrem a cena entram na 2b, pelo catálogo:** só texturas identificadas por `TextureId` que funcionam
  como superfície (alfa cheio), uma a uma e com prova; nada genérico.
- Os modelos 3D dos menus continuam sem AO (decisão anterior).
