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
