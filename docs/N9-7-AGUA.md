# N9.7 — água: primeira entrega (2026-11-08)

Esta entrega cobre **somente a água** e é deliberadamente parcial. Ela não antecipa iluminação geral, sombras,
materiais das demais superfícies, texturas HD, antialiasing ou SSAO.

## Superfície comprovada

A rota `passo_cgate`, parada no passo 2800 no cais RCG00, foi executada no gameplay assistido do build de validação.
O comando `ir cais` guardou a IR real; `bs-ir-tela materiais` exportou 66 texturas e um TSV com os 1 012 desenhos,
estados e retângulos.

O mar usa a identidade estável **`32x32-7458e3539d31cd8d`** (dimensões + FNV-1a dos texels ARGB8888): 46 desenhos
translúcidos, `ModulateAlpha`, mistura `SrcAlpha/InvSrcAlpha`. A textura e a geometria cobrem o mar junto ao cais e o
horizonte. Nenhuma outra textura ou desenho foi classificado como água nesta entrega.

`bs-ir-tela materiais <ir> <pasta>` é a ferramenta reproduzível para ampliar o inventário por área. Ela grava as
texturas em BMP e `desenhos.tsv`; a seleção não usa endereço de VRAM, cor, alfa nem a lista translúcida como heurística.

## Implementação

- `native/render/water`: catálogo fechado por `TextureId` e movimento de UV por três ondas de direções, frequências e
  velocidades diferentes. Só a cópia da IR enviada ao Vulkan recebe a marca transitória; posição, profundidade, blend,
  IR original e estado lógico permanecem iguais.
- O shader Vulkan calcula normais procedurais por pixel, distorção/refração da cena opaca, brilho especular e cristas
  suaves. O movimento fino não fica preso aos vértices da malha original.
- Entre os passes opaco e translúcido, o Vulkan copia a cena opaca. Só os desenhos marcados como água amostram essa
  cópia: reflexo espelhado em espaço de tela, Fresnel mais forte no horizonte e alternativa azul de céu/maresia quando
  o ponto refletido cai fora da imagem. Não existe filtro genérico sobre a cena inteira.
- Qualidade em `graficos.toml`, `[imagem] agua = "original" | "aprimorada" | "alta"`, também disponível no menu.
- Instalação nova da Remastered começa em `aprimorada`; a Original força `original`. O rasterizador em software é o
  recuo original.
- O efeito usa o número do render como fase, sem relógio de parede e sem tocar a simulação.

## Provas dirigidas

- Unitários: ID exato e negativos por hash/dimensão; só a água recebe UV e marca transitória; geometria e profundidade
  não mudam; qualidade Original é neutra; faixas inválidas são recusadas.
- Prova Vulkan sintética: o reflexo muda mais de 200 pixels dentro da superfície marcada e zero pixel acima dela.
- Na IR real do cais, `bs-ir-tela agua` alterou exatamente os 46 desenhos cadastrados. Original × Alta: 35 189 pixels,
  diferença máxima de 65 níveis por canal. Quadros 120 × 150: 26 242 pixels, máximo de 8 níveis. Nos dois casos, a
  caixa das diferenças é `x=0..639, y=118..479`, a superfície do mar/horizonte.
- `bs_render_tests`: 85/85; `bs_unit_tests`: 341/341.

## Limites abertos do N9.7

Ainda faltam o inventário das demais áreas e superfícies molhadas, avaliar SSR por ray marching onde a profundidade for
confiável, medir o custo na rota e validar visualmente o movimento durante a campanha. A entrega atual já tem reflexo
planar em espaço de tela, alternativa ambiental fora da tela, refração, transparência original e especular. Até uma
superfície entrar no inventário por `TextureId`, ela fica original.
