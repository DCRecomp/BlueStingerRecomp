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

## 9. Fase 2b: o catálogo de materiais e o AO sob as translúcidas (2026-11-10/11)

Escolha do mantenedor (2026-11-10): o núcleo do catálogo primeiro, neutro, e como primeiro material o AO sob as
translúcidas que cobrem a cena (seção 8).

### 9.1 O catálogo

- `native/render/material_catalog.h/.cpp`: `TextureId` (o contrato do N7: dimensões + FNV-1a dos texels ARGB8888 da
  textura **original**) → bits de material. Um só catálogo para o N9.6 (materiais), o N9.7 (água) e o N9.8 (HD: a
  substituição continua com o material da original). Só linhas medidas, sem curinga, só identidades (nenhuma arte).
- `render_core`: o id é calculado **uma vez por envio** da textura e só quando alguém o usa (o HD ou o catálogo); o catálogo
  só liga na Remastered (`render_capture::set_materials`, pelo `main_sdl` antes do jogo) e só no Vulkan (o software, a
  referência, não o consulta). O material vai ao dispositivo por `IRenderDevice::set_texture_material` (sem efeito no
  software) e fica na textura do Vulkan.
- Na Original e com o catálogo vazio, nenhuma identidade é calculada para ele e nada muda.

### 9.2 Inventário (o que entrou e o que ficou de fora)

- `bs-ir-tela texturas <ir> <tsv> [--pasta] [--cobertura]` (novo, só investigação): por desenho, a lista, o `TextureId`, o
  alfa dos texels, a mistura, o 1/w e o retângulo; `--cobertura` conta os pixels finais que mudam sem cada translúcida (o
  efeito da remoção, não a cobertura rasterizada direta: revisão Codex 20261110-225319); `--pasta` exporta as texturas em BMP
  (só `tmp/`).
- No quadro do quarto (render 2318), a área de opacos coberta por translúcidas (22,9 %) é quase toda de três desenhos: os
  racks `128x128-743d83eda77f1bb8` (45 637 px finais) e o piso de grade `128x128-563c7fcc146c1f60` (20 629 px); o brilho
  laranja `64x64-e6aa04e85bbde090` é luz, não superfície. Nas 22 cenas da fase 3B, as duas texturas só aparecem nessa área,
  sempre translúcidas, SrcAlpha/InvSrcAlpha, escrevendo a profundidade.
- **O 1/w do opaco por baixo, por pixel** (`tmp/sessao-20261110/n96/f2_coplanar.py`, os triângulos opacos rasterizados da
  IR): nos racks, 0,66 % de 1/w à frente da parede opaca em todos os 43 583 pixels com opaco por baixo (um decalque um
  pouco afastado, ~3 unidades a z 500, bem menos que o raio do AO de 60); no piso de grade, mediana 36 % (o fundo de um
  fosso). A primeira leitura pelos vértices (plano ajustado) tinha sugerido o contrário e foi descartada.
- **Entrou:** os racks. **Ficou de fora:** o piso de grade (o AO do fundo do fosso não é o da grade).

### 9.3 O AO sob a translúcida

- Quando o quadro tem uma translúcida do catálogo (com o AO ligado no render do framebuffer), o Vulkan cria sob demanda
  `aoSurf_` (RGBA8 no alvo, 4 B/px) antes de gravar os comandos e regrava o conjunto do quadro (binding 6 do `pvr.frag`;
  sem ela, a textura neutra de 1 × 1; o binding 5 fica para a água do N9.7). O passe final do AO (variantes `-DSURF` de
  `ao.comp`, alta e meia resolução) grava em todos os pixels o fator aplicado ao opaco e o log2 do 1/w dele em 24 bits.
- No passe B, uma translúcida do catálogo (`F_AO_SURF`, bit 28) multiplica a cor pelo fator **só a até 1 % de 1/w do
  opaco por baixo**; mais longe, a cor do jogo. A imagem sai ao desligar o AO.
- Nas máscaras de validação (`BS_AO_DIAGNOSTICO=2/3`), o fator gravado vira 0 e 1: os pixels que o catálogo pode mudar
  ficam do lado "depende da cena" na L2, e todo o resto continua bit a bit (sem afrouxar o critério).

### 9.4 Resultados

- **Revisão Codex** `codex-20261110-225319`: **MÉDIA**, o teste do material só cobria a alta: corrigido (as três
  qualidades). **BAIXA**, sem as fronteiras do limite de 1 %: corrigido (decalques a 0,9 %, recebe, e a 1,2 %, não recebe).
  **BAIXA**, `--cobertura` mede o efeito da remoção: aceito, descrição corrigida na ferramenta e no catálogo.
- **Testes** (binários de 00:43 de 2026-11-11, os finais): unitários 375/0, render 96/0 (o material nas três qualidades:
  junto à quina 0,745–0,771, longe 0,995; a translúcida longe de opacos, a de 1,2 % e a não cadastrada sem mudança; o HUD
  bit a bit; o desligado igual ao software); ctest 30/30 e 23/23 (5 pulados sem o disco, não contados); L1–L4 na média 0
  falhas (catálogo: 1 envio nas duas execuções da Remastered; o AO nas translúcidas em 128 renders, só com o AO ligado);
  alarme da marca de cena 0 (22 cenas).
- **Bateria anterior, binários de 22:48** (iguais aos finais no comportamento: mudaram só os testes e textos): L1–L4 0
  falhas nas três qualidades; H1–H7 0; R1–R4 4/0; Original × `referencias_n87` 10/0.
- **Equivalência:** as capturas da L na alta comparadas com as da fase 1: o VBlank 3800, todas as execuções com o AO
  desligado e a máscara branca **iguais**; no 4640, mudanças só na caixa dos racks (x 397–639, y 6–195 do jogo), só
  escurecendo (4:3: 5 806 px; 1080: 64 439 px, mediana 0,92 da luminância de antes).
- **Custo** (corredor, que inclui o quarto; GPU ms): RTX 5080 16:9@1080 média 2,02 → 2,10; @2160 média 3,38 → 3,40, alta
  7,42 → 7,43; AMD 4:3@480 média 7,80 → 7,84, baixa 6,66 → 6,73 (no ruído). Memória +4 B/px só nos quadros com uma
  translúcida do catálogo e o AO ligado (1080: +8 MiB; 4K: +32 MiB).
- **Visual** (recorte de 1080, `tmp/sessao-20261110/n96/f2b/racks_antes_depois.png`): os racks escurecem de leve nas bordas,
  como a parede de trás; sem artefato visível. A conferência na janela é do mantenedor.
- **Registro de processo:** a primeira execução da bateria (22:53) gravou por engano os logs L, H, R e N87 na pasta da fase 1
  (`f1d`), sobrescrevendo os logs de texto da bateria final da fase 1 (os resultados já estavam na seção 7, as capturas em
  `pytemp` e as matrizes `d-*`/`j-*` não foram tocadas), e as etapas de custo e do alarme não rodaram (o script gerado tinha
  quebras de linha no caminho). Os custos e o alarme foram rodados de novo nos binários finais (00:44–00:56).

### 9.5 Pendências

- O piso de grade e outras translúcidas que não são decalques: o AO delas pede a própria geometria (um passe de profundidade
  das translúcidas do catálogo), fora desta fase.
- O inventário das outras áreas (só o quarto do `load` foi medido) e os materiais seguintes do plano (especular, relevo,
  molhado, emissivos).

## 10. Fase 3: a luz das fontes (emissivos e luzes pontuais dinâmicas) (2026-11-11)

Autorizada com as fases seguintes ("Vai para as próximas fases", 2026-11-11). **Só nível 1 nesta fase**; a validação vai
para a bateria final do N9.6 (`docs/N9-6-TESTES-FINAIS.md`, seção 4).

### 10.1 Inventário das fontes

- O brilho da textura sozinho não acha as lâmpadas: as texturas mais claras das salas são pele, olho, fonte e placa (a cor
  final vem da textura vezes a cor do vértice). As fontes foram achadas pelos **desenhos sob as luzes visíveis** dos quadros
  (`bs-ir-tela texturas` nas IRs do corredor RCG02, do quarto, do cais e do exterior; `tmp/sessao-20261110/n96/f3`) e
  conferidas pela textura exportada.
- **Entraram (kMaterialEmissive):** `64x64-ea09b01a84c2c4aa` (o brilho azul das faixas de luz do teto do corredor,
  translúcida One/Zero), `64x16-10b0adb7ecfefb45` (a placa EXIT, opaca), `64x32-14cb38a2d38b4ed9` (a placa HEIGHT LIMIT,
  opaca), `64x64-e6aa04e85bbde090` (o brilho laranja da faixa do chão do quarto). **Ficou de fora:** o fogo do cais
  (`256x256-ab9ef1baad7940ad`), um atlas com fogo e fumaça juntos (a fumaça não é fonte).
- A cor de cada luz sai dos texels (`emissive_color`: a média pesada pelo brilho e pelo alfa, normalizada no maior canal),
  calculada no envio da textura; o catálogo continua só com identidades.

### 10.2 Método

- **Na CPU, antes de gravar os comandos** (`vk_device` `gatherLights`): cada desenho de uma fonte no quadro vira uma luz
  pontual no centro dos vértices, na câmera pela projeção do Ninja (z = 1/(1/w), dist 500, cx e cy no centro do quadro do
  jogo); as que ficam a menos de 80 unidades viram uma (as partes de uma lâmpada); no máximo 16; raio 400, intensidade 1.
- **Passe de luz** (`lights.comp`, depois do AO e antes dos volumes, só com luz no quadro): por pixel da cena 3D, a posição e
  a normal como no AO; cada luz soma cor × (1 − d/R)² × max(0, n·l); a cor da cena (luz original e material juntos, 2.4) é
  multiplicada por (1 + 0,6 × soma), limitada a 1. Fora da cena e do raio, nada muda (D6-2 i: a luz original fica e a nova
  soma).
- **Onde liga:** só na Remastered com o Vulkan, no render do framebuffer; a chave `[imagem] iluminacao` do `graficos.toml`
  (desligada por padrão; `Features.lighting` só na Remastered) e `--iluminacao ligado|desligado` (só na execução, não
  gravado; a Original e o software recusam com 2). A entrada do menu continua "em breve" até a fase 5, como a do SSAO.
- Recursos sob demanda: os objetos do passe e o buffer das luzes (16 × 32 bytes) nascem na primeira vez que há luz; uma
  falha desliga só a luz.

### 10.3 Nível 1 (2026-11-11)

- Os três presets compilados; `bs_unit_tests` 375/0; `bs_render_tests` 98/0: `render_vulkan_lighting_emissive` (o chão junto
  da lâmpada clareia 1,25×; a parede fora do raio e o HUD bit a bit; a luz só soma; as duas partes da lâmpada viram uma luz;
  sem a fonte no quadro, nenhuma luz; desligada igual ao software) e `material_emissive_color`; ctest 30/30 e 23/23;
  `gera_spirv.ps1 -Conferir` 35/35.
- A suíte ganhou o L5 (`n96_luz.py`: a recusa da Original, o estado igual, a luz só na ligada, só clareia) e a matriz o
  sufixo `+luz` (`n95_desempenho.py`), para a bateria final. **Nada disso rodou ainda.**

### 10.4 Riscos

- A luz multiplica a cor final (o material não se separa da luz original pela IR): superfícies escuras clareiam pouco, as
  claras saturam; o raio, a intensidade e a força (400, 1, 0,6) são valores de partida, a conferir na janela.
- A luz não tem sombra (a fase 4 trata as sombras); paredes entre a luz e o ponto não bloqueiam.
- As fontes são só as do corredor e do quarto; as outras áreas pedem inventário.

## 11. Fase 4: as sombras (volumes originais com a borda suave e sombras de contato) (2026-11-11)

D6-4 a + b. **Só nível 1**; a validação vai para a bateria final (`docs/N9-6-TESTES-FINAIS.md`, seção 4).

### 11.1 Método

- **(a) A borda suave dos volumes originais.** Com as sombras aprimoradas, os volumes modificadores da lista 1 rodam pela
  variante `volumes.comp -DMASK` (as três variantes do ambiente FP, a mesma cobertura do caminho fiel), que grava a máscara
  "dentro do volume" dos pixels com o bit de sombra em vez de escalar a cor. O passe `soft_shadows.comp` faz, por pixel com o
  bit de sombra, a média da máscara numa vizinhança de 2 pixels do jogo (pesos gaussiano e de profundidade: a penumbra não
  atravessa bordas de profundidade). Média 1, o interior: a conta inteira do original (cor × FPU_SHAD_SCALE / 256). Entre 0
  e 1, a borda: a mistura. Média 0: nada. A máscara e os seus conjuntos têm layout próprio: o layout, o conjunto e o
  pipeline fiéis dos volumes não mudam (o SPIR-V fiel de `volumes.comp` ficou igual).
- **(b) As sombras de contato da luz das fontes.** No passe de luz (fase 3), cada luz percorre em 12 passos o caminho do
  ponto até ela, cada passo projetado na tela. Se a profundidade ali mostra algo à frente do caminho (até 24 unidades de
  espessura), a luz daquela fonte não chega ao ponto. A espessura começou em 60 e caiu para 24 porque, no teste, um painel
  na frente da luz bloqueava um ponto sem obstáculo (a limitação clássica do espaço de tela).
- **Onde liga:** `[imagem] sombras = "aprimoradas"` (já existia; `Features.shadows` só na Remastered) ou `--sombras
  aprimoradas` (só na execução, não gravado; a Original e o software recusam com 2). Desligadas, o modo fiel. A entrada do
  menu fica "em breve" até a fase 5. A máscara (RGBA8 no alvo, 4 B/px) nasce sob demanda e sai ao desligar.

### 11.2 Nível 1 (2026-11-11)

- Os três presets; `bs_unit_tests` 375/0; `bs_render_tests` 100/0, com dois testes novos:
  - `render_vulkan_soft_shadows`: o fundo do volume e o lado de fora iguais ao fiel; a borda uma rampa monótona de 127 a 255
    em 4 pixels; desligadas, igual ao software;
  - `render_vulkan_contact_shadows`: o ponto atrás do painel com 0,800 da luz; o ponto sem obstáculo igual.
- ctest 30/30 e 23/23; `gera_spirv.ps1 -Conferir` 39/39; `n0_native_limpo` ok.
- A suíte ganhou o L6 (`n96_luz.py`) e a matriz o sufixo `+sombras`, para a bateria final. **Nada disso rodou ainda.**

### 11.3 Riscos

- A sombra de contato só vê o que está na tela: luz atrás da câmera ou um obstáculo fora do quadro não bloqueiam; um objeto
  fino à frente do caminho pode faltar ou sobrar.
- A borda suave tem 2 pixels do jogo: é a conferir na janela; um volume pequeno (a sombra sob o personagem) pode ficar mais
  claro no centro se for menor que a penumbra.

## 12. Fase 5: o menu, o `graficos.toml` e a matriz de desempenho (2026-11-11)

**Só nível 1**; a bateria final do N9.6 roda em seguida, pela lista `docs/N9-6-TESTES-FINAIS.md`.

- **Menu** (`app/settings_menu.cpp`): "Sombras" (original/aprimoradas) e "Iluminação aprimorada" na página Gráficos; "SSAO" e
  "Qualidade" (baixa/média/alta, só com o SSAO ligado) na página Efeitos de imagem. Cada uma só com o seu recurso
  (`Features::shadows`, `::lighting`, `::ssao`, que o `main_sdl` só liga na Remastered com o Vulkan); sem ele, "em breve" e
  indisponível. A página Efeitos de imagem passa a abrir também só com o SSAO. As escolhas vão ao `graficos.toml` (`[imagem]
  sombras`, `[imagem] iluminacao`, `[efeitos] ssao` e `ssao_qualidade`) e às mesmas ligações das fases 1–4 (`applyGraphics`).
- **Padrões:** o SSAO e a luz das fontes desligados em todos os presets. **Decisão pendente com o mantenedor:** o preset
  "Alto" (do N7c) já tinha `sombras = "aprimoradas"`; com a fase 4, quem escolher "Alto" passa a ter a borda suave e as sombras
  de contato. Não foi mudado.
- **Matriz:** `n95_desempenho.py` com os sufixos `+ao-<qualidade>`, `+luz` e `+sombras`, combináveis.
- **Nível 1:** os três presets; `bs_unit_tests` 376/0 (`settings_menu_lighting_options`: as quatro entradas com e sem o
  recurso, as trocas e a qualidade só com o SSAO); `bs_render_tests` 100/0; `bs_ui_tests` 25/0; `bs_remaster_tests` 5/0;
  ctest 30/30 e 23/23; `gera_spirv.ps1 -Conferir` 39/39.
- **Registro de processo (avanço do master da fase 4):** o script de resolução de um conflito falhou (erro de sintaxe) e a
  mescla não gravou nada, mas a cadeia de comandos seguiu (o código de saída ficou com o `tail`) e moveu o ref do master e o
  índice para `8cd14eb` com os arquivos ainda da fase 3 com a água. Os 39 arquivos sujos foram conferidos iguais ao backup
  (`tmp/sessao-20261110/backup-main-antes-f4`); a mescla foi refeita medindo o sujo contra a base (`dc8aa61`) e o status e o
  numstat ficaram iguais aos de antes; os arquivos particulares intactos. Desde então a gravação é conferida pelo log antes
  de mover o ref.

## 13. A bateria final do N9.6 (2026-11-11)

Pela lista `docs/N9-6-TESTES-FINAIS.md`, uma bateria pesada por vez, com o vigia de memória e TEMP/TMP em `tmp/`. Script
`tmp/sessao-20261110/n96/bateria_final.ps1`, saída em `tmp/sessao-20261110/n96/final/`, de 01:44 a 05:41.

### 13.1 Binários

- **Bateria principal:** os três presets do commit `c49e8c8` (fase 5), exes de 2026-11-11 01:39 (`msvc-release` e
  `msvc-validacao`).
- **Correções da revisão Codex `20261111-014426`** (seção 13.4), depois da bateria, nos três presets recompilados de
  `c49e8c8` com elas: os testes (unitários, render, ui, remaster, ctest, SPIR-V), o L7 e as caixas. O código do produto não
  mudou entre os dois binários: as correções tocam só `tests/unit/test_render_vulkan.cpp`, `tests/unit/test_settings_menu.cpp`
  e `tools/dev/n96_luz.py`.

### 13.2 Resultados (nenhum item pulado conta como aprovado)

| Item | Resultado |
|---|---|
| 1.1–1.4 antes | exes conferidos (data e `git log -1`); SPIR-V 39/39; unitários 376/0, render 100/0, ui 25/0, remaster 5/0; ctest debug 30/30 e release 23/23, com 5 pulados sem o disco; arquivos particulares com o mesmo hash (`c4ad9e1a87788ed4`, `fd68ddd2a0c9c054`) |
| ctest com o disco | `real_disc`, `runtime_data`, `analysis_real` aprovados com `BS_DISC_IMAGE`; `movie_manifest` aprovado com `BS_DATA_DIR` na Preview nova (110 s). **`boot_trace` não rodou**: precisa dos snapshots do Flycast (`BS_SNAPSHOT_DIR`), que não existem nesta máquina. Fica sem aprovação (testa o boot da CPU, não o render) |
| R1 | Original × `referencias_n87`: 10 iguais, 0 diferentes |
| R2 | H1–H7: 53 ok, 0 falhas (as "falhas" do log do H7 são as injetadas pelo próprio caso) |
| R3 | R1–R4: 4 casos, 0 falhas (R4: Remastered = Original sem a câmera, 1030 passos, 0 diferentes) |
| R4 / F3-6 / F4-6 / F5-2 | P1–P5: 22 ok, 0 falhas |
| R5 / F5-6 | Preview nova pelo Builder (`v0.9.5-3-13-gc49e8c8-dirty`; o "dirty" vem da água e dos arquivos particulares da árvore principal, que o Builder não leva) rc 0; `test_edicao` 11 ok, 0 falhas |
| R6 / F5-1 | J1–J7: 0 falhas (J6/J7a com as trocas pelo menu da pausa; J7b e J7c com o recuo) |
| L1 | `n96_luz.py --qualidade media` L1–L6: 54 ok, 0 falhas |
| L2 | baixa e alta (L1–L4): 40 ok e 40 ok, 0 falhas |
| L3 | A alta contra a fase 1 (`pytemp/n96_luz_c2f0h_3r` × `n96_luz_syho4vaj`): o desligado igual em todas as capturas (4:3, 16:9, 1080); o ligado igual no 003800 e, no 004640, só a caixa dos racks do 2b: x 397–639, y 11–195 no 640 (5 806 px, 1,9 %, diferença até 25), x 504–853, y 11–198 na imagem larga e x 1135–1921, y 24–448 na de 1080. A luz e as sombras não estão nesses casos (desligadas por padrão) |
| L4 | `n96_cena.py`: 22 TSVs, 10 180 desenhos opacos, 0 com 1/w constante, rc 0 |
| L5 | AO e efeitos desligados = modo fiel: as máscaras do L2 e o H (acima); os testes de render na escala 1 desligados = software bit a bit |
| L6 | o catálogo: 5 envios na Remastered (L2); **0 na Original no Vulkan** (L7, abaixo) |
| F3-1 | L5: a Original recusa (rc 2); estado e efeitos iguais (2 326 passos); luz em 571 renders, até 8 luzes; só clareia |
| F3-2 | luz desligada × ligada no 4:3: 003800 muda 22,95 % (diferença até 9; a maior região x 0–244, a parede junto da câmera; as demais, as faixas do teto e as placas, x 402–639); 004640 muda 16,47 % (até 25; x 0–381, y 0–345 e a faixa x 376–416). Fora da cena 3D nada muda (L7) |
| F3-3 | **L7** (caso novo do `n96_luz.py`): 4:3 tudo desligado × AO média + luz + sombras: estado idêntico (2 326 passos), efeitos idênticos, os três rodaram (AO em 2 333 renders), fora da cena 3D bit a bit (2,2 % e 32,0 % da imagem fora da cena; 27,2 % e 25,6 % mudaram) |
| F4-1 | L6: a Original recusa; estado e efeitos iguais; borda suave em 571 renders (critério novo, > 0); o HUD do canto igual (0 px) |
| F4-2 | borda suave: 571 renders na rota do corredor e 12 040 na do heliporto (cais e exterior), 0 sem as sombras. As caixas (luz ligada nas duas): 003800 3,15 %, dentro da região que a luz clareia (x 0–212 ⊂ x 0–244, diferença até 9, a mesma da luz): sombra de contato tirando a luz; 004640 0,60 %: o contorno da sombra do personagem no chão (x 369–525, y 346–410), o alto do teto e pontos no personagem. Imagens em `tmp/sessao-20261110/n96/codex/f4_*.png` |
| F4-3 | R1 10 iguais; `bs_render_tests` 100/0 |
| C1 / F3-4 / F4-4 / F5-3 | medidos (13.3) |
| C2 | medidos (13.3) |
| C3 | medidos (13.3) |
| V1, F3-5, F4-5 | **com o mantenedor** (não aprovados aqui) |
| X1 | seção 13.4 |
| F5-4 | **pendente:** decisão do mantenedor |
| Memória | vigia: pico do Blue Stinger 5 120 MB (as três execuções em paralelo), commit livre mínimo 19,41 GB |

### 13.3 Custo e memória

GPU por quadro, média em ms (`gpu_total_ms`), sem janela, `n95_desempenho.py`:

| Config | corredor | cais | exterior |
|---|---|---|---|
| RTX 5080 16:9@1080 | 0,68 | 1,40 | 1,07 |
| + AO média | 0,89 | 1,44 | 1,15 |
| + luz | 0,56 | 1,61 | 2,12 |
| + sombras | 1,86 | 2,02 | 2,33 |
| + AO média + luz + sombras | 2,38 | 2,28 | 2,33 |
| RTX 5080 16:9@2160 | 1,95 | 4,37 | 4,24 |
| + AO média | 3,38 | 5,00 | 4,62 |
| + luz | 2,06 | 4,40 | 4,18 |
| + sombras | 3,31 | 5,55 | 4,60 |
| + AO média + luz + sombras | 4,85 | 6,14 | 5,31 |

AMD Radeon integrada, corredor: 4:3@480 5,53; + AO média 7,87; + AO baixa 6,66; + luz 5,78; + sombras 6,42; + AO baixa + luz
+ sombras 8,39. 16:9@1080 26,0; + AO baixa 39,8; + luz 28,0; **+ sombras 53,5**.

Na janela (C2, corredor): AMD 16:9@1080 59,3 fps, p50 16,0 ms, 30,9 % dos quadros acima de 16,7 ms, GPU 26,5 ms; com AO
baixa + luz + sombras 41,8 fps, 28,1 % acima de 25 ms, GPU 72,7 ms. RTX 5080 16:9@2160 59,98 fps, GPU 3,4 ms; com AO média
+ luz + sombras 59,8 fps, GPU 5,0 ms.

Memória dos alvos (`mem_alvos_mib_pico`): RTX 1080 610,4 MiB; + AO média 628,4 (+18,0, cerca de 8,7 B/px); + luz 610,4
(+0); + sombras 619,0 (+8,6, cerca de 4,2 B/px, a máscara RGBA8); tudo 636,9. RTX 2160 2 432,0; + AO 2 497,8 (+65,8, ~8,3
B/px); + sombras 2 464,0 (+32,0, 4,0 B/px); tudo 2 529,7. AMD 480 95,5; + AO 97,9; + sombras 96,7.

Leitura (sem meta inventada): na RTX 5080 os três efeitos juntos cabem com folga em 2160. Na AMD integrada, em 480 eles
cabem; em 1080 o jogo já não fecha 60 fps sem efeitos, e o AO e principalmente as sombras (o `soft_shadows.comp` em tela
cheia) dobram o custo. **A estimativa do limite da resolução interna** (`vk_device.cpp`: 96 + 4 B/px) não conta a máscara das
sombras (4 B/px) nem a imagem do AO sob as translúcidas, e o AO medido fica em ~8 B/px. Já era uma subestimativa conhecida
(medido ~268 B/px), adiada para o N9.12, que fica com esses números.

### 13.4 Revisões Codex (X1, F3-7, F4-7, F5-5)

- **`reviews/codex-20261111-014426.md`** (fases 3–5), três achados, todos aceitos:
  - MÉDIA: o L6 aprovava com 0 renders da borda suave (`borda(on6) >= 0`). Corrigido para `> 0`, e a bateria deu 571.
  - MÉDIA: a luz e as sombras não eram conferidas na escala 2 nem no quadro largo. Os três testes
    (`render_vulkan_lighting_emissive`, `render_vulkan_soft_shadows`, `render_vulkan_contact_shadows`) passam a rodar em
    640 escala 1, 640 escala 2 e 854 escala 1. Na escala 1, desligado = software bit a bit. Os três passam nos três arranjos,
    com os mesmos números: luz 1,250, rampa de 4 pixels de 127 a 255, contato 0,800.
  - BAIXA: o menu só era conferido com os recursos juntos. `settings_menu_lighting_options` passa a ligar cada recurso
    sozinho.
- **`reviews/codex-20261111-055403.md`** (o conjunto das fases 0–5 e as correções acima), quatro achados:
  - MÉDIA, aceito: o L7 dava os três efeitos por rodados pelas mensagens da troca. Agora ele exige os contadores de execução:
    AO em 2 333 renders, luz em 571 (até 8 luzes) e borda suave em 571.
  - MÉDIA, mantido com motivo: a máscara de "fora da cena 3D" do L7 vem das execuções com o AO e não cobre um receptor de
    sombra com 1/w constante. O critério é mais estrito que o necessário: ele só pode reprovar a mais, nunca aprovar sem
    efeito. Rodou e passou (0 pixels fora da máscara mudaram). Se um dia reprovar por isso, a máscara passa a ser a união dos
    domínios dos três efeitos, sem afrouxar o critério.
  - BAIXA, aceito: a Original aprovava com o resumo do catálogo ausente (-1). Agora ela exige o resumo e o valor 0 exato, e deu 0.
  - BAIXA, aceito: os arranjos criavam um dispositivo cada. Novo teste `render_vulkan_effects_target_switch`: no mesmo
    dispositivo, com os três efeitos ligados, a sequência 640 s1 → 640 s2 → 854 → 640 s1. Cada quadro sai igual ao de um
    dispositivo novo no mesmo arranjo, os três contadores sobem a cada quadro e nenhum objeto Vulkan sobra. `bs_render_tests`
    101/0.
  - Depois das correções: os três presets; unitários 376/0, render 101/0, ui 25/0, remaster 5/0; ctest 30/30 e 23/23; SPIR-V
    39/39; L7 0 falhas.

### 13.5 Riscos para o N9.12

- A estimativa de VRAM do limite da resolução interna (13.3).
- O custo das sombras aprimoradas em GPU integrada em 1080, que pesa na decisão do preset "Alto" (F5-4).
- Os riscos das seções 10.4 e 11.3 (o espaço de tela da sombra de contato, a penumbra de volumes pequenos, o raio da luz).
