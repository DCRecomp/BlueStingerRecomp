# N9.8: texturas HD, antialiasing e pós-processamento: auditoria e plano (fase 0)

**Situação:** o mantenedor pediu o início do N9.8 em 2026-11-12 ("Inicia o 9.8"), depois da publicação da `v0.9.7`. Esta
é a **fase 0, só investigação**, como no N9.6: nenhum código do produto muda. As decisões da seção 5 são do mantenedor e
nenhuma fase de implementação começa antes delas. Escopo do plano (`docs/N9-PLANO.md`, N9.8): substituição de texturas sem
alterar os dados originais, antialiasing escolhido por viabilidade e o refinamento do pós-processamento, com o HUD e as
legendas fora dos filtros. Recomendações herdadas do N9.7 (`docs/N9-7-AGUA.md` seção 8): as substituições do mar mantêm as
oito identidades originais; o MSAA leva em conta a cópia da cena da água e as imagens do N9.6; variantes de shader
separadas do fiel, recursos sob demanda, testes Vulkan sob a camada de validação.

## 1. Método

- Leitura do código no master `000e222` (a `v0.9.7`): `native/render/texture_replacement`, `transitional/render_core`
  (`Renderer::texture_handle`, `texture_argb`), `native/render/vulkan` (`vk_device`, `vk_post`, `vk_present`, os shaders),
  `native/render/soft`, `native/platform/graphics_settings`, `app/main_sdl.cpp` (as capacidades) e os documentos do N7
  (`docs/N7-PLANO.md` "Texturas HD futuras: contratos" e 25.2).
- Inventário das texturas pelas 27 cenas guardadas do N9.6/N9.7 (`tmp/sessao-20261111/n97/inv/*/desenhos.tsv`, o
  `bs-ir-tela materiais`): identidades distintas, tamanhos e filtro de cada desenho.
- Nenhuma execução nova do jogo, nenhuma janela.

## 2. O que já existe

### 2.1 Texturas substituíveis (contrato do N7, sem provedor)

| Peça | Onde | Estado |
|---|---|---|
| Identidade pelo conteúdo | `texture_replacement.h` (`TextureId`: dimensões + FNV-1a de 64 bits dos texels ARGB8888 do maior nível; texto `<l>x<a>-<16 hex>`) | pronta; é a mesma do catálogo de materiais do N9.6/N9.7 |
| Regra de compatibilidade | `replacement_compatible`: mesma proporção, fator inteiro de 1 a 16 | pronta, com unitários |
| Ponto de troca | `Renderer::texture_handle` (`render_core.cpp`): com `hd`, calcula o `TextureId` e pede `ITextureReplacements::find`; compatível, envia a substituição (`tex_replaced`), senão o original (`tex_replace_rejected`) | pronto e testado (`test_render_capture.cpp`) |
| O material continua o do original | o `material_flags(id)` e a cor emissiva usam o `TextureId` e os texels **originais** | já correto: a água, os emissivos e o AO sob as translúcidas seguem a textura original |
| Amostragem de uma textura maior | `pvr.frag` `wrapc`: o tamanho vem de `pc.texSize`, com fator qualquer | pronto (o comentário já prevê a substituição) |
| **Provedor** (carregador de pacotes, manifesto, decodificação, cache, orçamento de VRAM) | — | **não existe**: `set_texture_replacements` só é chamado pelos testes |
| Decodificador de imagem no produto | — | **não existe**. O Windows tem o WIC (já usado pelo instalador); o Linux não tem nada equivalente no sistema |
| Menu e `graficos.toml` | `hd_textures` no modelo e a linha "Texturas HD" no menu | "em breve" (`Features::hd_textures` nunca liga) |

### 2.2 Amostragem e mipmaps

- **Os dois renderizadores amostram só o maior nível.** O `pvr.frag` faz a amostragem à mão (`texelFetch` no nível 0, ponto
  ou bilinear com as regras do PVR: wrap, clamp, flip, meio texel); o sampler do Vulkan é `NEAREST` de propósito. O
  rasterizador em software faz o mesmo. Uma textura com mipmap no TCW (bit 31) é lida e convertida, mas só o maior nível
  chega ao dispositivo (`render.h`: `mip_levels = 1`). O `mip_bias` do TSP é guardado e não usado.
- **Consequência:** com a resolução interna acima de 480, a minificação não tem filtro (cintilação em chão e paredes à
  distância). Com uma textura HD de fator 4 ou 8 sem mipmaps, o efeito piora: mais texels pulados por pixel. **Os mipmaps são
  pré-requisito das texturas HD.**
- **Filtro dos desenhos:** nas 27 cenas, 15 723 dos 15 726 desenhos texturizados usam o bilinear do PVR; 3 usam ponto.
- **Viabilidade da amostragem pelo hardware (só na Remastered):** os modos do PVR têm equivalente direto no sampler do
  Vulkan: wrap = `REPEAT`, clamp = `CLAMP_TO_EDGE`, flip = `MIRRORED_REPEAT` (a mesma conta de `wrapc`). Isso permite uma
  variante `-DFILTERED` do fragmento, com `textureGrad`/`texture` sobre uma cadeia de mipmaps gerada no envio, trilinear e
  anisotrópica, sem tocar o fiel. O ponto (3 desenhos) e o meio texel do PVR ficariam na variante como no fiel.

### 2.3 Antialiasing

- **O que existe de fato é supersampling:** a resolução interna vai até 2160 (`Resolution::P2160`, limitada pela GPU) e a
  apresentação reduz a imagem à janela com **um** `texture()` bilinear (`present.frag`, sampler `LINEAR`). Até 2:1 isso é
  uma média de 2×2. Acima de 2:1 (2160 numa janela de 720) pixels são pulados e o serrilhado volta. Um filtro de redução
  melhor é barato e não toca a cena.
- **MSAA (pedido do N7 25.2):** tudo é de uma amostra (`VK_SAMPLE_COUNT_1_BIT` nos alvos e no pipeline). Para o MSAA:
  - os alvos de cor e de profundidade multiamostra e os resolves;
  - o **autosort** (listas de fragmentos por pixel, `resolve.comp`) por amostra, ou a cobertura guardada no fragmento: a
    memória das listas multiplica por N;
  - os **volumes modificadores** (`volumes.comp`, por pixel, em compute) por amostra;
  - o **AO, a luz, as sombras aprimoradas** (N9.6) e a **cópia da cena da água** (N9.7) leem a profundidade e a cor de uma
    amostra: precisam de um resolve de profundidade (mínimo ou máximo, uma escolha visível) ou rodar por amostra;
  - o modo fiel (480) decide a cobertura no shader: o MSAA não vale nele (só nas resoluções maiores).

  É a mudança mais funda do N9.8, com risco nos efeitos do N9.6/N9.7 e na memória de vídeo.
- **Pós-processamento (FXAA/SMAA):** a cadeia do N7c (`vk_post.cpp`) já roda sobre o slot antes da apresentação. Um passe a
  mais custa pouco e cobre tudo (inclusive o autosort e os volumes), mas age também sobre o **HUD do jogo**, que está dentro
  da imagem. O menu nativo e as legendas não passam por ele: são camadas da apresentação, compostas depois.
- **TAA:** o jogo não fornece vetores de movimento, e o HUD está na mesma imagem. Rastros e borrão no HUD sem uma separação
  confiável. Não recomendado.

### 2.4 Pós-processamento existente (N7c)

| Efeito | Estado | Observação |
|---|---|---|
| correção de cor (brilho, contraste, saturação, gama) | no menu e no Alto | aplicada ao slot inteiro, HUD incluído |
| tonemapping, bloom, nitidez, CRT | no menu (Remastered e Original com o Vulkan) | o bloom e a nitidez também agem sobre o HUD |
| shaders de mod (`mods\shaders`) | no menu | laços recusados; perda do dispositivo desativa o shader |

Menu e legendas já ficam fora de todos eles. O HUD do jogo não. A marca cena/HUD do N9.5 (a proveniência, `cpu/hud_scope`)
identifica os desenhos do HUD do canto e pode gerar uma máscara para o pós. As mensagens e os menus do próprio jogo
(pausa, inventário) não são marcados hoje.

## 3. Medidas (inventário das 27 cenas)

| | Valor |
|---|---|
| Identidades distintas (`TextureId`) | 265 |
| Tamanhos mais comuns | 64×64 (62), 128×128 (60), 32×32 (35), 16×16 (13), 256×256 (11) |
| Texels originais | 2 940 992 (11,2 MiB em RGBA8) |
| Fator 2 (RGBA8, com mipmaps) | ~60 MiB |
| Fator 4 | ~239 MiB |
| Fator 8 | ~957 MiB |

- O inventário cobre só as cenas das rotas guardadas (título, C-GATE, cais, heliporto, combate, load). O jogo inteiro não
  foi medido (as gravações do C8 se perderam). Por cena, o conjunto residente é menor que o total, mas o cache atual não
  descarta texturas por orçamento de memória.
- Um pacote de fator 4 em RGBA8 já fica no tamanho do orçamento que o N9.12 ainda não fechou (96 + 4 + 4 B/px por pixel do
  alvo, mais as texturas). A compressão de GPU (BC7, ~1 B/texel) divide o custo por 4.

## 4. Opções

### 4.1 Texturas

| | Opção | Prós | Contras |
|---|---|---|---|
| T1 | **PNG** por identidade: `mods\texturas\<pacote>\<l>x<a>-<hex>.png`, um manifesto TOML opcional (nome, autor, versão, fator); mipmaps gerados no envio | simples para quem faz pacotes; sem perda | a decodificação custa CPU no primeiro uso; sem compressão de GPU |
| T2 | **KTX2** com BC7 (mipmaps prontos) | VRAM ÷ 4, sem custo de decodificação | ferramenta de conversão para quem faz pacotes; BC7 no Linux depende do driver (comum no desktop) |
| T3 | T1 com conversão para KTX2/BC7 num cache local (como os filmes do Builder) | o melhor dos dois para o jogador | mais código; o cache em pasta do usuário |

- **Decodificador de PNG:** não existe nenhum no produto. Candidatos permissivos e sem dependência de sistema: `stb_image`
  (domínio público ou MIT), `lodepng` (zlib), `spng` (BSD-2, exige zlib/miniz), `wuffs` (Apache-2.0). Toda dependência nova
  passa pela regra de licenças (`docs/N9-L-LICENCAS.md`).
- **Carregamento:** no primeiro uso da textura (pode travar o quadro) ou por pré-carga do pacote inteiro no início (memória e
  tempo de boot). A pré-carga em thread separada, com o original até a substituição ficar pronta, evita os dois problemas,
  mas é a mais complexa.
- **Pacote de teste:** a arte do jogo nunca vai para o repositório. Um pacote de teste **sintético** (padrões com a
  identidade e o tamanho de texturas reais, gerados pela ferramenta a partir de uma IR do usuário, local, em `tmp/`) prova o
  carregador, os fatores e o recuo, sem redistribuir nada.

### 4.2 Filtragem e mipmaps (pré-requisito das texturas HD)

| | Opção |
|---|---|
| F1 | **Variante `-DFILTERED`** na Remastered: mipmaps gerados no envio (`vkCmdBlitImage` ou compute), sampler por modo do PVR (repeat, clamp, mirrored), trilinear e anisotrópica (`Filter::Anisotropic`, 2 a 16). O fiel não muda. Libera a linha "Filtragem" do menu. |
| F2 | Mipmaps escolhidos à mão no shader (derivadas, `texelFetch` por nível) | mantém o bilinear do PVR por nível; não oferece a anisotrópica do hardware |
| F3 | Nada (as texturas HD sem mipmaps) | não recomendado (seção 2.2) |

### 4.3 Antialiasing

| | Opção | Custo | Risco |
|---|---|---|---|
| A1 | **Redução melhor na apresentação** (média de N×N ou filtro de área quando a resolução interna passa de 2:1 da janela) | baixo | baixo: só a apresentação |
| A2 | **SMAA 1x** (ou FXAA) na cadeia de pós, com a máscara do HUD do canto (N9.5) para não borrá-lo | baixo a médio | médio: as mensagens e menus do jogo sem marca passam pelo filtro |
| A3 | **MSAA 2×/4×** nas resoluções maiores | alto (seção 2.3) | alto: autosort, volumes, N9.6, N9.7 e VRAM |
| A4 | TAA | — | não recomendado |

### 4.4 Pós-processamento

- P1: a máscara do HUD (A2) também para a nitidez e o bloom.
- P2: os padrões dos presets revistos com as medidas (sem mudar a Original).
- Os efeitos já existem: o N9.8 só os refina.

## 5. Decisões para o mantenedor

| | Pergunta | Recomendação |
|---|---|---|
| D8-1 | Formato dos pacotes: T1 (PNG), T2 (KTX2/BC7) ou T3 (PNG com cache BC7)? | **T1 agora** (com mipmaps gerados), **T3 depois** se a VRAM medida pedir |
| D8-2 | Decodificador de PNG: `stb_image`, `lodepng`, `spng` ou `wuffs`? | **`stb_image`** (domínio público/MIT, um arquivo, Windows e Linux iguais), só para PNG, com limites de tamanho |
| D8-3 | Filtragem: F1 (variante filtrada com mipmaps e anisotrópica) antes das texturas HD? | **F1, como primeira fase** |
| D8-4 | Antialiasing: A1 + A2 agora e o MSAA (A3) só depois de uma medida (ou nunca)? | **A1 + A2**; o A3 fica registrado como avaliado e não feito, com o motivo |
| D8-5 | Padrões: tudo desligado nos presets (como o N9.6/N9.7) ou a filtragem no Alto? | **a filtragem e o A1 no Alto** (não mudam a cena); texturas HD e SMAA só por escolha manual |
| D8-6 | Carregamento: no primeiro uso, pré-carga, ou pré-carga em thread com o original até ficar pronta? | **no primeiro uso, com um limite por quadro** na primeira entrega; a thread depois, se a medida pedir |

## 6. Fases propostas (cada uma com autorização)

| Fase | Conteúdo | Aceitação |
|---|---|---|
| 8.1 | Filtragem: mipmaps no envio, variante `-DFILTERED`, trilinear e anisotrópica; a linha "Filtragem" do menu | o fiel com o SPIR-V igual; a Original × `referencias_n87`; unitários e render sob a camada; medida de custo |
| 8.2 | Texturas HD: o provedor (pacotes em `mods\texturas`, manifesto, PNG, cache por `TextureId`, recuo ao original), a linha "Texturas HD", o pacote de teste sintético local e a VRAM por área | pacote de teste carregado; recuo por textura ausente ou incompatível; os 8 quadros do mar continuam água; VRAM medida |
| 8.3 | Antialiasing: A1 e A2 com a máscara do HUD | comparação de custo e qualidade documentada; o HUD do canto, o menu e as legendas fora do filtro |
| 8.4 | Pós: a máscara do HUD na nitidez e no bloom, os presets revistos | os presets sem regressão (`n96_presets`) |
| 8.5 | Bateria final e documentação | as baterias do N9.5 em diante sem falhas; Codex |

## 7. Riscos e pendências

- **VRAM:** as texturas HD entram no orçamento ainda aberto do N9.12. O cache atual não descarta por orçamento.
- **Linux:** o N9.6 e o N9.7 ainda não foram compilados nem testados no Linux; o N9.8 soma o decodificador e os mipmaps.
- **C8 bloqueado:** sem as gravações reais, o inventário e as medidas cobrem só as rotas guardadas.
- **A conferência visual** do N9.6 e do N9.7 continua pendente com o mantenedor; o N9.8 soma a dele.
- **Arte do jogo:** nenhum pacote HD real no repositório nem na release; só o carregador e o pacote de teste sintético local.

## 8. Decisões e fase 8.1: a filtragem (2026-11-12)

**Decisões:** à apresentação das recomendações da seção 5, o mantenedor respondeu "Pode começar" (2026-11-12). Entendido como
aprovação de D8-1 a D8-6 como recomendados: PNG com mipmaps gerados (KTX2/BC7 depois, se a VRAM pedir); `stb_image`; a
filtragem como primeira fase; a redução melhor na apresentação e o SMAA com a máscara do HUD (o MSAA avaliado e não feito); a
filtragem no Alto, as texturas HD e o SMAA só por escolha manual; o carregamento no primeiro uso com limite por quadro.

### 8.1 O que mudou

| Parte | Onde | O que faz |
|---|---|---|
| Variante `-DFILTERED` | `pvr.frag` | os desenhos **bilineares** de uma textura com mipmaps: o ponto de amostra do PVR (com ou sem o meio texel) vira a coordenada do hardware; `textureGrad` com o gradiente de um pixel do alvo calculado pelas equações do triângulo (`uvAt`, as de `exact()`: o modo fiel desenha o retângulo envolvente, então as derivadas da GPU não servem); nove amostradores (repetição, clamp ou espelho em u e em v) por índice constante. O ponto continua o do fiel. Seis cabeçalhos SPIR-V novos; os 45 de antes, idênticos |
| Mipmaps | `vk_device.cpp` | com a filtragem ligada, toda textura nasce com a cadeia até 1×1, gerada pela GPU a cada envio (blit linear nível a nível). Ao ligar, as texturas já enviadas ganham a cadeia de uma vez (uma imagem nova, a cópia do maior nível e os blits, num envio fora do quadro esperado pela cerca). Desligada, o desenho volta ao fiel (a cadeia fica) |
| Amostradores | `vk_device.cpp` (`applyFiltering`) | trilinear; com a anisotrópica, `maxAnisotropy` até o limite do dispositivo (`samplerAnisotropy` habilitado quando existe); recriados quando o nível muda, com o binding 1 de cada textura reescrito |
| Capacidades | `VulkanStats::mipmaps`, `max_anisotropy`; `EditionProfile::texture_filtering` (só a Remastered) | a linha "Filtragem de texturas" do menu deixa de ser "em breve" na Remastered com o Vulkan; `clamp_to` limita a anisotrópica |
| Configuração | `graficos.toml` (`[imagem] filtragem`/`anisotropia`, já existiam), `--filtragem original\|bilinear\|anisotropica` (só nesta execução) | os presets já traziam bilinear no Equilibrado e anisotrópica 16× no Alto: na Remastered com o Vulkan eles passam a valer. A Original e o software continuam com a do fiel |
| Estatísticas | resumo `[render-vk]` | `filtragem: N desenhos, texturas com mipmaps ate M (K promovidas)` |

### 8.2 Provas (nível 1)

- **Unitário Vulkan** (`render_vulkan_texture_filtering`):
  - num chão em perspectiva com um xadrez de 1 texel, o desvio do verde longe cai de 13,3 (fiel) para 0,0 (trilinear) e 0,2 (anisotrópica), na média do xadrez (127);
  - perto, o filtrado difere do fiel em até 2 níveis;
  - o ponto fica igual;
  - desligada, a imagem do fiel bit a bit, mesmo com a textura já com mipmaps;
  - a promoção conta 1, e uma textura criada depois já nasce com a cadeia;
  - nas 9 combinações de repetição, clamp e espelho, ampliado 4×, a maior diferença para o fiel é 1.
- **Camada Khronos:** `bs_render_tests` 108/0 com a validação padrão e com a de sincronização (`VALIDATE_SYNC`, também no envio): 0 erros. Ela achou um erro, já corrigido: a textura de origem da promoção sem `TRANSFER_SRC`. Agora toda textura é criada com esse uso.
- **Testes e builds:** unitários 379/0, ui 25/0, remaster 5/0; os três presets compilam; `gera_spirv -Conferir` 51/51.
- **No jogo, sem janela** (`tools/dev/n98_filtragem.py`, rota `passo_cgate` até o cais, 34 300 VBlanks, Remastered em 1440):
  - **F1:** a Original e o software recusam a opção (saída 2);
  - **F2:**
    - o estado por passo (3 137 passos) e os efeitos sonoros ficam idênticos sem a opção, com a original, a trilinear e a anisotrópica (produto e validação);
    - a variante filtrada em 233 213 desenhos, com até 186 texturas com mipmaps;
    - a `original` fica igual bit a bit à execução sem a opção;
    - a trilinear muda a imagem, e a anisotrópica difere dela;
    - o produto fica igual à validação;
    - a alta frequência da imagem em 1440 (média do módulo do laplaciano do verde) cai de 1,81 para 1,65 (VBlank 30 700) e de 3,19 para 2,81 (34 200); com a anisotrópica, 1,73 e 3,00 (mais nítida);
  - **F3:** com o AO, a luz, as sombras aprimoradas e a água alta em 16:9 e 1080, o estado e o som ficam idênticos, e os cinco efeitos rodam;
  - **F4:** com a falha transitória injetada no VBlank 32 000, o dispositivo é recriado, e no 34 200 o quadro é o da anisotrópica sem a falha (o jogo e a imagem larga).
  - **Defeito da própria suíte, corrigido:** a primeira versão pedia a imagem larga nos VBlanks 4 000 e 12 000, que são telas desenhadas pela CPU, sem slot. Ela foi corrigida e o F2 rodou de novo.
- **A Original × `referencias_n87`** (2026-11-13, o produto da worktree já integrada ao master `fbc6761`, `n8_referencias.py conferir`, 10 casos: newgame_area1, cgate, heliporto, combate e load_opcoes, nos dois modos de tempo): **10 iguais, 0 diferentes**. Uma primeira execução foi interrompida em 2/10 (para a v0.9.7-1) e uma segunda em 4/10 (falta de memória com outro processo pesado junto); a terceira rodou sozinha.
- **Unitários depois da integração** (msvc-debug): unit 380/0 (com o teste do Codex da atualização depois da promoção), render 108/0, instalador 14/0, ui 25/0, remaster 5/0; ctest 30/30 (6 sem o disco).
- **`n96_presets` Q2:** agora espera `filtragem = "anisotropica"` no arquivo gravado pelo menu no Alto da Remastered. A filtragem passou a existir; MSAA e texturas HD continuam voltando ao original.

### 8.3 Limitações

- **Mipmaps sem alfa pré-multiplicado:** a média do blit não pré-multiplica o alfa. Texturas recortadas (punch-through) podem perder cobertura à distância e ganhar uma borda escura nas translúcidas. A medida e a correção (cobertura preservada, ou a média ponderada pelo alfa num compute) ficam para depois da conferência visual.
- **A água fica na sua variante:** ela não é filtrada.
- **Sem mipmaps de mod:** os mipmaps do jogo (TCW) continuam sem uso; a cadeia é gerada do maior nível.

## 9. Fase 8.2: as texturas HD (2026-11-13)

Pedido do mantenedor: "Continua o 9.8" (2026-11-13), com as decisões D8-1, D8-2, D8-5 e D8-6 já aprovadas (seção 8).

### 9.1 O que mudou

| Parte | Onde | O que faz |
|---|---|---|
| Pacotes | `native/render/texture_packs` (`TexturePacks`, o `ITextureReplacements` do N7) | os pacotes são os de `mods\<pacote>\mod.toml` (o mesmo contrato das legendas e dublagens: `id`, `nome`, `versao`, `prioridade`, `jogo`) com uma pasta `texturas\`, um PNG por textura, de nome `<largura>x<altura>-<16 hex>.png` (o `TextureId` da original). O pacote de prioridade maior vence; um nome que não é um `TextureId` canônico é ignorado e avisado |
| Desvio do texto da D8-1 | — | a D8-1 falava em `mods\texturas\<pacote>\`. Os pacotes ficam em `mods\<pacote>\texturas\` para usar um só manifesto e a prioridade que já existem (um pacote pode trazer legendas e texturas juntos) |
| Decodificador | `third_party/stb` (stb_image 2.30, commit `f58f558`, domínio público; D8-2) | só o PNG, da memória; as dimensões conferidas antes de decodificar; inventário em `docs/N9-L-LICENCAS.md` e `third_party/README.md`; o texto vai em `licencas/` dos dois Builders e a pasta entra nos dois kits |
| Carregamento (D8-6) | `TexturePacks::find`, `begin_frame`, `deferred` | no primeiro uso, até 8 texturas ou 64 MiB de texels por render (a primeira do render sempre passa; o tamanho decodificado é conhecido pelo cabeçalho antes de decodificar, e a que passaria da folga do render espera). O resto fica para os renders seguintes: o renderizador usa o original e pergunta de novo (`VkTex::waiting` em `render_core`), mesmo sem os bytes da textura mudarem |
| Recuo por textura | idem | PNG acima de 64 MiB, ilegível ou cortado, lado acima de 8192, ou dimensões que não são um fator inteiro de 1 a 16 da original na mesma proporção: o original, com o motivo guardado e listado no fim (`[texturas-hd] recusada ...`). Também recua a que passaria do orçamento da sessão (1 GiB de texels decodificados guardados) e a que falta memória para decodificar (`std::bad_alloc` recusa só ela; o jogo segue). Uma textura recusada não é tentada de novo |
| Material | `render_core` (já era assim no N7) | a água, os emissivos e o AO continuam pelo `TextureId` e pelos texels da **original** |
| Edição e opção | `EditionProfile::hd_textures` (só a Remastered); `graficos.toml` `[imagem] texturas_hd` (já existia); `--texturas-hd ligado\|desligado` (só nesta execução; a Original e o software recusam o `ligado`, saída 2) | a linha "Texturas HD" do menu deixa de ser "em breve" na Remastered com o Vulkan **quando algum pacote tem texturas**. Sem pacote nesta execução, a escolha do arquivo fica guardada |
| Presets (D8-5) | `graphics_settings.cpp` | o Equilibrado e o Alto deixam de ligar as texturas HD: só pela escolha manual |
| Exportação (só validação) | `BS_TEXTURAS_EXPORTAR=<pasta>` | grava cada textura original enviada ao Vulkan como `<TextureId>.png` (codificador próprio, `native/render/png_write`, PNG sem compressão): a base local de um pacote. É arte do jogo: fica em `tmp/` e nunca é publicada. Um exportador para quem faz pacotes, no produto, fica para o N10 |
| Resumo | `[render-vk]` (`HD adiadas`, `exportadas`) e `[texturas-hd]` no fim | os pacotes e o índice; carregadas (MiB e o maior fator), recusadas e adiamentos |

### 9.2 Provas (nível 1, 2026-11-13)

- **Builds:** `msvc-debug`, `msvc-validacao` e `msvc-release` compilam. O primeiro build falhou por literais de texto quebrados
  na escrita do código (em `game_host.cpp` e `main_sdl.cpp`); corrigidos antes de qualquer teste.
- **Unitários:**
  - `test_texture_packs` (render): o PNG escrito e lido de volta igual, com o alfa; a prioridade, os nomes ignorados e os
    pacotes; as recusas com o motivo e sem nova tentativa; o limite por quantidade, por bytes, pela soma que cruza o limite
    e o orçamento da sessão;
  - `render_capture_hd_replacement_deferred` (unit): a adiada chega no render seguinte, mesmo sem os bytes da original
    mudarem;
  - unit 381/0, render 112/0; ctest 30/30 (6 sem o disco).
- **No jogo, sem janela** (`tools/dev/n98_texturas.py`, rota `passo_cgate` até o cais, Remastered em 1440; os pacotes de teste
  montados das texturas exportadas desta execução, apagados no fim; 29 verificações, 0 falhas, 727 s, rodada de novo depois
  das correções da revisão):
  - **exportação:** 112 texturas da rota; a validação (que exporta) e o produto com o mesmo estado por passo e o mesmo som; o
    produto com `BS_TEXTURAS_EXPORTAR` não exporta nada;
  - **T1:** a Original e o software recusam `--texturas-hd ligado` (saída 2);
  - **T2 (identidade, fator 1):** estado e som idênticos; 203 substituições, 462 adiamentos pelo limite do quadro, 112
    carregadas (5,3 MiB, os texels); a imagem do jogo e a de 1440 **iguais bit a bit** à sem pacote;
  - **T3 (×4, 16:9 em 1080, anisotrópica e água alta):** estado e som idênticos; fator 4 em todas (84,9 MiB); a água nos
    mesmos desenhos (2 095 e 2 003); a imagem muda; GPU por quadro 1,56 ms sem pacote e 1,48 ms com o ×4;
  - **T4 (recuo):** as 56 ruins recusadas, todas e só elas, cada uma com o motivo certo; as 56 válidas carregadas; os nomes
    ignorados avisados; a imagem igual bit a bit à sem pacote.
- **`n96_presets` (janela, Q1–Q10):** 32 verificações, 0 falhas (2 595 s). Sem pacote, as texturas HD continuam "em breve" e
  voltam ao original no arquivo gravado pelo menu.

### 9.3 Revisão Codex (2026-11-13, `reviews/codex-20261113-112036.md`)

Três achados, todos aceitos e corrigidos:
- **[média] o limite de bytes do render podia ser ultrapassado pela textura seguinte:** só se olhava o que já tinha sido
  carregado. Agora o tamanho decodificado vem do cabeçalho (`stbi_info`) e a textura que passaria da folga espera; as
  seguintes do mesmo render também, sem ler o arquivo. Teste novo: 16 KiB carregados com limite de 20 000, a próxima de 16 KiB
  espera (o código antigo carregava).
- **[média] sem limite de memória da sessão:** um pacote com muitas imagens grandes podia esgotar a RAM e terminar o jogo.
  Agora há `Limits::max_resident` (1 GiB de texels guardados): a que passaria dele é recusada com o motivo; a falta de
  memória ao decodificar recusa só aquela textura. Teste novo do orçamento (recusa determinística, sem nova tentativa).
- **[baixa] a suíte não provava que o produto ignora `BS_TEXTURAS_EXPORTAR`:** a corrida do produto recebe a variável e a
  suíte exige a pasta ausente e `exportadas 0`.

Pontos conferidos sem problema pelo revisor: a Original não descobre nem liga pacotes; o recuo do PNG cortado; o tamanho do
arquivo e as dimensões antes de decodificar; o adiamento sem perder a textura; a vida útil dos pacotes em relação ao
`GameHost`; os presets sem as texturas HD.

### 9.4 Limitações

- **Sem mipmaps próprios do pacote:** a cadeia é gerada da textura HD como na fase 8.1 (o mesmo limite do alfa).
- **Sem descarte:** as texturas carregadas ficam na RAM até o fim (o orçamento limita); um LRU fica para quando houver um
  pacote real que precise.
- **O decodificador não passou por fuzzing:** o `stb_image` confere a aritmética das alocações; um fuzzing com ASan fica
  para o N9.12.
- **A conferência visual** de um pacote de verdade fica com o mantenedor: nenhuma arte vem com o projeto.

## 10. Fase 8.3: o antialiasing (2026-11-13)

Pedido do mantenedor: "Pode salvar o 8.2 e iniciar o 8.3" (2026-11-13; o 8.1 e o 8.2 no commit local `710183d`). D8-4 e D8-5
já aprovadas (seção 8): A1 + A2, o MSAA avaliado e não feito; a redução no Alto e o antialiasing das bordas só por escolha
manual.

### 10.1 O que mudou

| Parte | Onde | O que faz |
|---|---|---|
| Opção | `GraphicsSettings::antialiasing` (`original`, `area`, `bordas`; `[imagem] antialiasing` no `graficos.toml`); `--antialiasing` (só nesta execução; a Original e o software recusam `area` e `bordas`, saída 2) | a linha "Antialiasing (MSAA)" do menu, sempre "em breve", virou "Antialiasing" (original, redução por área, área e bordas), só na Remastered com o Vulkan e a apresentação direta (`EditionProfile::antialiasing`, `Features::antialiasing`). A chave `msaa` continua no arquivo, sem efeito. Um arquivo antigo sem a chave fica com a do preset |
| Preset (D8-5) | `graphics_settings.cpp` | o Alto com `area`; o Equilibrado e o Original com `original`; as bordas só pela escolha manual |
| A1: redução por área | `present.frag` (bit 1 das flags), `vk_present.cpp` (`kAreaFilter`), `PresentRequest::area_filter`, `PostSpec::areaFilter` | quando a imagem do jogo é mais de 2× maior que a área dela na janela (texels por pixel pela derivada da coordenada), a média de N×N amostras bilineares (cada uma a média de 2×2 texels) cobre a área do pixel, até 16:1. Até 2:1 o bilinear de sempre (a mesma imagem de antes) |
| A2: bordas da cena 3D | `shaders/aa.comp`; `VulkanDevice::edgeAntialiasing` | um compute **depois dos volumes e antes do passe B** (as translúcidas). Lê uma cópia da cor (`aaSrc_`) e o bit 1 do anexo de sombra (a cena 3D: 1/w variando no desenho, o mesmo critério do AO) e escreve na cor só os pixels misturados. O método é o conhecido antialiasing por pós-processamento (borda pela luminância, direção pelo gradiente, procura das pontas e mistura com o vizinho), escrito de forma independente. Só no render do framebuffer |
| Desvio do texto da D8-4 | — | a D8-4 falava em SMAA com a máscara do HUD do N9.5. A marca do N9.5 só cobre o HUD do canto e só no 16:9. O bit da cena 3D, antes das translúcidas, deixa de fora todo o HUD, os textos, as telas 2D do jogo, as translúcidas, a água e os efeitos, no 4:3 e no 16:9, sem uma marca nova. O nome da opção é "bordas" (não é o SMAA) |
| Diagnóstico (só validação) | `BS_AA_DIAGNOSTICO=1` | os pixels misturados em magenta; o produto ignora a variável |
| Resumo | `[render-vk] ... antialiasing das bordas em N renders`; `[render] antialiasing das bordas: ligado/desligado` | |

### 10.2 Provas (nível 1, 2026-11-13)

- **Builds:** `msvc-debug`, `msvc-validacao` e `msvc-release` compilam; `gera_spirv` (SDK em `C:\VulkanSDK\1.4.363.0`) mudou só
  `present_frag.h` e criou `aa_comp.h`; os outros cabeçalhos ficaram idênticos.
- **Unitários:** unit 382/0, render 113/0.
  - `render_vulkan_edge_antialiasing`: um triângulo projetado e um 2D, os dois claros com bordas inclinadas, e um desenho da lista
    translúcida sobre a borda do projetado. Desligado, a imagem do software. Ligado: 504 pixels misturados, nenhum no 2D, nenhum
    longe do projetado, todos entre o menor e o maior luma da vizinhança 3×3 (nenhuma cor nova); o desenho translúcido (14 590
    pixels) igual bit a bit; determinístico; o render de 256×256 igual ao fiel; desligado de novo, o fiel e a imagem auxiliar
    devolvida;
  - `render_capture`: uma perda transitória do dispositivo com as bordas ligadas: recriado, o render sai pelo Vulkan com as
    bordas;
  - `graphics_antialiasing_fase83`, `edition` e o menu: o preset, o arquivo, o valor desconhecido, o recuo sem o recurso, a
    aplicação no quadro seguinte.
- **No jogo, sem janela** (`tools/dev/n98_aa.py`, rota `passo_cgate` até o cais, Remastered em 1440):
  - **A1:** a Original com `area` e com `bordas` e a Remastered no software com `bordas` saem com 2;
  - **A2:** estado por passo e efeitos sonoros idênticos com e sem as bordas; as bordas em 17 136 renders; em 1440, 0,37 % e
    1,17 % dos pixels mudam nos quadros da cena (VBlanks 30 700 e 34 200), a alta frequência cai de 1,81 para 1,74 e de 3,19
    para 3,12; as telas da CPU (4 000 e 12 000) iguais bit a bit;
  - **A3:** a validação igual bit a bit ao produto (imagem do jogo e 1440); o diagnóstico pinta 55 762 e 196 583 pixels sem
    mudar o estado; o produto com a variável não pinta nenhum;
  - **A4:** com o 16:9 em 1080, a anisotrópica, o AO, a luz, as sombras aprimoradas e a água alta: estado e som idênticos aos
    mesmos sem as bordas; AO, borda suave, água, filtragem e bordas rodam. A rota não tem fontes do catálogo: a luz liga e
    fica em 0 renders, igual sem as bordas (a primeira versão do critério pedia luz > 0 e falhou por isso; corrigido).
- **Na janela** (`n98_aa.py`, rota `passo_load_slot1`, Remastered 4:3, interna 1440):
  - **A5:** janela 640×480 (3:1): a área difere do bilinear e tem menos alta frequência (3,38 → 2,97, 6,04 → 5,45, 10,03 →
    8,69); janela 1280×960 (1,5:1): igual bit a bit ao bilinear. A primeira execução falhou: a flag não chegava ao shader
    (`vk_present.cpp`), corrigido;
  - **A6:** `--antialiasing bordas` com o Alto no arquivo e, no menu, a linha duas vezes para a esquerda: as bordas ligam e
    desligam e o arquivo grava `original`; sem mexer na linha, o arquivo fica como estava.
- **ctest** 30/30 (6 sem o disco); **`n96_presets`** (janela, Q1–Q11) 32 verificações, 0 falhas: a linha do menu manteve a
  posição da do MSAA, e o Alto gravado pelo menu leva `antialiasing = "area"`.

### 10.3 Revisão Codex (2026-11-13, `reviews/codex-20261113-135423.md`)

Dois achados, aceitos e corrigidos:
- **[média] a mudança no menu depois do `--antialiasing` não ia para o arquivo:** a marca da linha de comando não era limpa.
  Corrigido para o antialiasing e, pelo mesmo defeito, para a `--filtragem` (8.1) e a `--texturas-hd` (8.2); provado pelo A6.
  As opções do N9.6/N9.7 (`--agua`, `--sombras`, `--iluminacao`, `--ssao`) seguem o mesmo padrão antigo e ficam como estão
  (registrado para o N9.12).
- **[baixa] faltavam testes da translúcida sobre a borda e da perda do dispositivo com as bordas:** os dois foram escritos
  (seção 10.2).

Pontos conferidos sem problema pelo revisor: as barreiras e os layouts (a cor em `GENERAL`, o anexo de sombra em
`SHADER_READ_ONLY_OPTIMAL`, a cópia antes do compute); nenhuma corrida (lê só a cópia, escreve só a cor); o passe B depois;
a recriação do alvo e a destruição; a Original bloqueada; os arquivos antigos compatíveis; a área só acima de 2:1; os SPIR-V
válidos (`spirv-val`); o comentário de `edition.h` corrigido.

### 10.4 Limitações

- **As bordas só pegam a cena opaca e o punch-through:** as translúcidas (vidros, efeitos, a água) e a parede exatamente de
  frente para a câmera (1/w constante, o mesmo limite do AO) ficam sem o antialiasing.
- **As bordas não são temporais:** o serrilhado em movimento (cintilação de linhas finas) diminui pouco; a resolução interna
  continua sendo o antialiasing principal.
- **A área só vale na janela:** as capturas e a telemetria sem janela não a usam (a imagem do jogo continua a do slot).
- **A conferência visual** na janela fica com o mantenedor.

## 11. Fase 8.4: o pós-processamento e os presets (2026-11-14)

Pedido do mantenedor (2026-11-13): fechar a 8.3 (commit local `f0f56ab`) e executar a 8.4 sem iniciar a 8.5. Requisito oficial
(seção 6): "a máscara do HUD na nitidez e no bloom, os presets revistos", aceitação "os presets sem regressão (`n96_presets`)".

### 11.1 Escopo efetivo

| | O que é | Decisão |
|---|---|---|
| Já existia (N7c) | correção de cor, tonemapping, nitidez, bloom, CRT e shaders de mod na cadeia da apresentação (`vk_post.cpp`), no menu, no `graficos.toml` e nos presets | não muda; nenhum efeito novo |
| Previsto (P1) | a nitidez e o bloom fora do HUD | **feito** (11.2), só na Remastered com o Vulkan |
| Previsto (P2) | os presets revistos | **revistos sem mudança de política** (11.3); uma inconsistência anterior fica para a decisão do mantenedor |
| Não previsto | a correção de cor e o tonemapping fora do HUD | não feito: valem na imagem toda, como antes (o HUD acompanha o tom da cena) |
| Correções de persistência | a escolha do menu depois da linha de comando | já corrigida na 8.3 para `--filtragem`, `--texturas-hd` e `--antialiasing`; a 8.4 não acrescenta opção nem linha de menu (a máscara é automática) |

**Por que não a marca do N9.5 nem o bit da 8.3:** a marca do N9.5 cobre só o HUD do canto e só no 16:9. O bit da cena 3D (o do
AO e das bordas da 8.3) é gravado só no passe A. O HUD translúcido é desenhado depois, no passe B, e um pixel de HUD sobre a
cena continuaria marcado como cena. A nitidez e o bloom rodam na apresentação, depois de tudo, e precisam de uma máscara que
inclua o passe B.

### 11.2 O que mudou

| Parte | Onde | O que faz |
|---|---|---|
| Máscara do HUD | `shaders/hudmask.comp`; `VulkanDevice::set_hud_mask`; `hudPrepare`, `hudApply` (`vk_device.cpp`) | depois da cópia do alvo para o slot da apresentação, grava no **alfa do slot** 1 na cena 3D (bit 1 do anexo de sombra) fora do HUD e 0 no resto. O HUD são os desenhos translúcidos 2D colados na câmera (1/w constante e ≥ `kHudInvW` = 0,05, ou o teste de profundidade "sempre"), pelo retângulo envolvente em blocos de 8×8 pixels do alvo, dilatado em um bloco, com a cobertura interpolada entre os centros dos blocos (sem degrau). O RGB do slot, o alvo e a imagem que volta para a CPU (o framebuffer emulado) não mudam |
| Os passes no modo máscara | `post_sharpen`, `post_bloom_h`, `post_bloom_v`, `post_color`, `post_tonemap` (`p[3].w = 1`); `PostPass::hud_mask` | a nitidez age na proporção da máscara (as pontas exatas: 1 = a mesma conta de antes, 0 = a entrada); o bloom só extrai o brilho da cena; a cor e o tonemapping valem em tudo e repassam o alfa. Sem o modo, cada passe é o de antes (alfa 1) |
| Quando liga | `window_present.cpp` (`build`), `PostSpec::hudMask`, `EditionProfile::post_hud_mask` | só na Remastered com o Vulkan, só com a nitidez ou o bloom ligados e sem shader de mod antes dos internos (ele não repassa o alfa). `build` roda também depois da recriação do dispositivo. Sem efeitos, nenhum compute e nenhuma conta na CPU |
| A apresentação | `vk_present.cpp`, `SlotSource::slot_masked` | o modo só vale com um slot que tem a máscara; a imagem da CPU (filmes, telas da CPU) e um slot sem ela passam pelos efeitos como antes |
| Descritores | um conjunto por slot | reescrito quando muda a **geração** (a versão da imagem do slot, a do alvo, a do buffer dos blocos), nunca pela comparação de handles (a lição do `ab28c5a`: o driver reaproveita valores de handle). Os slots ganham `STORAGE` só quando a máscara liga (recriados uma vez) |
| Medida | `[apresentacao] pos, GPU medio por passe` na saída; `mascara do HUD em N slots` no resumo `[render-vk]` | |
| Só validação | `BS_HUD_MASCARA=1` (a máscara também sem janela, gravada em `vNNNNNN_mascara.bmp`), `BS_HUD_MASCARA=0` (desliga na janela, para comparar), `BS_JANELA_SAIR_VBLANK=<n>` (fecha a janela pelo caminho normal, para o resumo sair) | o produto não lê as variáveis (P1) |

**O limite `kHudInvW`, medido:** a primeira versão marcava todo translúcido 2D. No cais (VBlank 30 700) ela tirou dos efeitos a
explosão e a fumaça, que são sprites da cena voltados para a câmera (1/w ≈ 6,6e-05). A segunda usava o maior 1/w da cena do
quadro como limite e perdeu a barra de vida do HUD: vértices recortados no plano próximo levam esse máximo a 1. Nas rotas
`passo_cgate` e `passo_load_slot1` o HUD fica entre 0,1 e 1 (z de 1,1 a 10) e os sprites da cena entre 6,6e-05 e 1,3e-03;
0,05 fica entre as duas faixas. É um limite empírico deste jogo: um sprite da cena colado na câmera (z < 20) fica fora da
nitidez e do bloom, o lado seguro.

### 11.3 Presets (revistos, sem mudança)

| | Original | Equilibrado | Alto | Padrões da Remastered (`remastered_best`, v0.9.7-1) |
|---|---|---|---|---|
| Resolução interna | 480 (fiel) | 1080 | 1440 | a do Alto |
| Filtragem | a do fiel | bilinear | anisotrópica 16× | a do Alto |
| Antialiasing | original | original | área | a do Alto (área) |
| Correção de cor | — | contraste 1,05, saturação 1,10 | contraste 1,10, saturação 1,15 | a do Alto |
| Nitidez | 0 | 0 | 0,25 (fora do HUD desde a 8.4) | a do Alto |
| Proporção | 4:3 | 4:3 | 4:3 | **16:9** |
| Sombras, luz, SSAO, água | originais, desligados | originais, desligados | originais, desligados | **aprimoradas, ligada, alto, alta** |
| Texturas HD, bordas, bloom | desligados | desligados | desligados | desligados |

- **O Alto não é o conjunto da primeira execução:** o Alto é o preset do menu (seguro: as melhorias do N9.6/N9.7 só por escolha
  manual, decisão de 2026-11-11). Os padrões da Remastered (v0.9.7-1, "o melhor possível ao instalar") são o Alto mais o 16:9,
  as sombras aprimoradas, a luz, o SSAO alto e a água alta, aplicados **uma vez** numa instalação nova (ou num `graficos.toml`
  gravado pelo menu de uma versão anterior), e o arquivo fica "Personalizado". Escolher "Alto" no menu depois volta ao preset.
- **Nada muda na 8.4:** o Alto continua com a nitidez 0,25; na Remastered ela passa a poupar o HUD (o único efeito visível da 8.4
  no preset). A Original continua com os efeitos como antes (P8).
- **A área da 8.3 e o resto do preset:** a redução por área só age acima de 2:1. O Alto (1440) numa janela de 720 linhas é 2:1:
  a mesma imagem. Ela só aparece com o Alto numa janela menor que 720 linhas, ou com 2160 numa de 1080 ou menos. Não interfere
  nos efeitos de pós (eles rodam antes, no slot).
- **Inconsistência anterior (a decidir, sem mudança):** a chave `antialiasing` nasceu na 8.3. Uma instalação nova recebe
  `area` (do Alto). Quem atualizar da v0.9.7-1 já tem a marca `[padroes] remastered = 1` e um arquivo "Personalizado" sem a
  chave: fica com `original`. Impacto: só na janela menor que 720 linhas com o 1440 (ou 2160 em 1080 ou menos); nenhum efeito
  no jogo, no som ou nos saves. Alternativas: (a) deixar como está (a escolha manual resolve); (b) migração pontual: um arquivo
  com a marca 1 e sem a chave recebe `area` uma vez (não sobrescreve nada escolhido, porque a chave não existia); (c) subir
  `kRemasterDefaults` para 2 (reaplicaria todos os padrões e **sobrescreveria** escolhas: não recomendado). Recomendação: (b),
  só com a aprovação do mantenedor.

### 11.4 Provas (2026-11-13 e 14)

- **Builds:** `msvc-debug`, `msvc-validacao` e `msvc-release` compilam. `gera_spirv` mudou só os 5 cabeçalhos dos passes de cor e
  criou `hudmask_comp.h`; `gera_spirv -Conferir` 53/53. Os SPIR-V do fiel (`pvr*`) não mudaram. **Erro de processo achado pela
  conferência:** uma regeneração tinha falhado em silêncio e `post_sharpen_frag.h` ficou atrasado em relação ao shader (as pontas
  exatas da mistura); os binários foram refeitos e toda a bateria desta seção rodou de novo com os cabeçalhos conferidos.
- **Unitários:** unit 382/0 (a conferência do perfil da edição entrou num teste existente), render 115/0 (+2):
  - `render_vulkan_hud_mask` (escalas 2 e 1,5): o RGB do slot e a imagem da CPU iguais com e sem a máscara; o alfa 255 na cena,
    0 no HUD sobre a cena, no 2D opaco e no fundo; a rampa só perto do HUD; determinística; um sprite translúcido 2D dentro da
    faixa da cena continua na cena com a sua cor; 1/w NaN, infinito, zero, negativo ou abaixo do limite não marca nenhum bloco
    (a revisão Codex; a condição antiga marcava 2 809, provado por mutação); desligada, o alfa volta ao do alvo;
  - `post_hud_mask_gpu`: a nitidez no modo deixa o HUD bit a bit e dá na cena o mesmo que sem a máscara; um ponto claro no HUD
    não brilha e um na cena brilha; a cor e o tonemapping iguais nos dois modos, com o alfa repassado; sem o modo, alfa 255.
- **Camada Khronos:** `bs_render_tests` 115/0 com a validação padrão e com a de sincronização (`VK_KHRONOS_VALIDATION_VALIDATE_SYNC`),
  **0 mensagens**. A camada foi ligada por `VK_INSTANCE_LAYERS`: o terminal é elevado e ignora `VK_LOADER_LAYERS_ENABLE`. Uma
  primeira execução com essa variável rodou sem a camada (a inserção conferida pelo `VK_LOADER_DEBUG`) e não foi contada.
- **ctest** 30/30 (6 pulados, sem o disco: `boot_trace`, `real_disc`, `runtime_data`, `analysis_real`, `movie_manifest`,
  `n0_fechamento`).
- **No jogo** (`tools/dev/n98_pos.py`):
  - **sem janela** (rota `passo_cgate`, 1440): **P1** validação com a máscara × produto: estado por passo e som idênticos, a
    imagem do jogo e o RGB em 1440 iguais bit a bit, a máscara em 17 134 slots; no 030 700 a explosão e a fumaça na cena (100 %),
    no 034 200 o HUD do canto fora (2,0 %) e o resto dentro; o produto ignora a variável. **P2** com o 16:9 em 1080, a
    anisotrópica, o AO, a luz, as sombras aprimoradas, a água alta e as bordas: estado, som e RGB idênticos, os efeitos rodam.
    **P3** com a perda do dispositivo injetada no 32 000: recriado, o 34 200 (imagem e máscara) igual ao sem a falha;
  - **na janela** (rota `passo_load_slot1`, interna 720 numa janela de 960×720, o exe de validação): **P4** sem efeitos e com só a
    cor, 0 slots (nada a mais); **P5** nitidez 1,0: 1 316 887 pixels realçados, 401 987 protegidos (realçados só sem a máscara),
    111 mudados só com a máscara, todos na rampa (0 longe); o verde do HUD do canto intacto com a máscara (0 de 4 201, 4 176 e
    4 078) e realçado sem ela (2 995, 2 796, 1 337); no VBlank coincidente (4 335, correlação 0,59) 0 protegidos fora da máscara;
    **P6** o bloom roda com a máscara e difere do sem ela onde o brilho vinha do HUD; **P7** tudo junto (cor, tonemapping,
    nitidez, bloom, área) sem erro do Vulkan; **P8** a Original: 0 slots e a imagem igual à com `BS_HUD_MASCARA=0`; **P9** a perda
    na janela no 4 400: dispositivo e apresentação recriados, a máscara continua e as capturas depois iguais às sem a falha.
  - **Critério do P5 corrigido (investigação registrada):** a primeira versão exigia que todo pixel mudado com a máscara também
    mudasse sem ela; 121 de 1,44 milhão não cumpriram. Prova da causa: com a nitidez 0,001 as imagens com e sem a máscara são
    iguais à sem efeitos (o mesmo render apresentado); a imagem do jogo tem 714 linhas e a janela 720, a apresentação mistura
    duas linhas, e mudanças de sinal oposto da nitidez se cancelam sem a máscara mas não na fração dela, na rampa. O critério
    novo: nenhum pixel mudado só com a máscara a mais de 16 px (a rampa) de um pixel protegido, e no máximo 0,01 % (121 de 144
    permitidos; 0 longe). Negativo: 50 pixels "vazados" longe do HUD são todos reprovados.
- **Bateria de fechamento (2026-11-14, `tmp/sessao-20261113/n98/gate84`, em sequência, com os binários do código final):**
  `n98_pos` P1–P9 0 falhas; `n98_aa` A1–A6 0 falhas; `n96_presets` Q1–Q11 0 falhas; Original × `analysis/n8/referencias_n87`
  10 casos iguais, 0 diferentes; a matriz de desempenho nas duas GPUs completa (11.5). Antes dela, com os mesmos binários:
  `gera_spirv -Conferir` 53/53, unit 382/0, render 115/0 sob as duas camadas com 0 mensagens, ctest 30/30.

### 11.5 Desempenho (janela 1280×720, 16:9 em 1440, trecho do corredor, `n95_desempenho.py`, produto)

| Configuração | RTX 5080: fps | GPU do render (ms) | AMD integrada: fps | GPU do render (ms) | saída (ms) |
|---|---|---|---|---|---|
| sem efeitos | 59,8 | 1,17 | 50,3 | 44,9 | 1,44 |
| + cor | 59,8 | 0,87 | 50,0 | 44,9 | 1,46 |
| + nitidez (máscara) | 59,9 | 1,46 | 50,0 | 46,1 | 2,58 |
| + bloom (máscara) | 59,8 | 1,00 | 46,7 | 44,7 | 2,36 |
| + cor, tonemapping, nitidez, bloom | 59,8 | 1,21 | 28,6 | 44,2 | 2,31 |
| + tudo e as bordas | 59,9 | 1,63 | 26,5 | 45,5 | 2,36 |

Números da bateria de fechamento (`gate84/desempenho/<GPU>/matriz.tsv`), com os binários finais.
- **Ruído:** na RTX 5080 o total do render oscila entre execuções iguais (a base deu 1,17 ms aqui e 1,67–2,32 ms em rodadas
  anteriores; a cor sozinha mediu menos que a base): o custo da máscara fica dentro dele. Na AMD, a etapa de saída (a cópia
  para o slot, onde roda a máscara) sobe de 1,44 para 2,3–2,6 ms com a máscara e não sobe com só a cor: **~1,0 ms em
  2560×1440**. A queda de fps na AMD com o pós inteiro vem da apresentação (CPU da janela 37–46 %, a leitura dos passes), não
  do render.
- **Os passes do pós em 4K** (`post_cost_4k`): RTX 5080 de 0,025 (cor) a 0,069 ms (bloom vertical); AMD integrada 1,3 (cor), 2,7
  (tonemapping), 2,2 (nitidez), 3,6 + 3,6 (bloom), 1,4 (CRT). Na AMD em 1440 o render já custa ~45 ms (o jogo anda abaixo do
  tempo real nessa resolução); com o pós inteiro o fps cai a 28. É uma limitação da GPU integrada nessa resolução; os presets
  não mudam e as escolhas manuais ficam disponíveis.
- **Memória:** imagens intermediárias do pós 45,3 MiB (RTX) e 47,5 MiB (AMD) em 2560×1440, só com algum efeito ligado; o buffer
  dos blocos ~57 KB; as bordas da 8.3 +15 MiB. Nenhuma imagem nova para a máscara (ela usa o alfa do slot).

### 11.6 Revisão Codex (2026-11-14, `reviews/codex-20261114-010850.md`)

Dois achados, aceitos e corrigidos:
- **[média] 1/w inválido ou negativo contava como HUD:** a condição só recusava `0 < 1/w < 0,05`; NaN, infinito, zero e negativos
  passavam. Agora: 1/w não finito nunca é HUD; sem o teste "sempre", só 1/w ≥ 0,05. Teste unitário com os cinco casos; a
  condição antiga, por mutação, marca 2 809 blocos e o teste falha.
- **[baixa] o P5 não provava que os protegidos eram o HUD:** acrescentados (a) os pixels do HUD do canto (o verde do nome, da
  barra e do canto) intactos com a máscara e realçados sem ela, e (b) nos VBlanks em que a janela e o jogo sem janela mostram o
  mesmo render (correlação das bordas ≥ 0,5), todo protegido dentro da máscara gravada sem janela. Na investigação: no VBlank
  4 505 a captura da janela mostra outro render (o personagem em outra posição, correlação 0,36) e fica fora da comparação; nos
  outros, 0 protegidos fora da máscara.

Pontos conferidos sem problema pelo revisor: o cache de descritores pela geração; as barreiras cópia → compute →
fragmento/transferência e a do anexo de sombra; o buffer do host com o quadro anterior terminado; a recriação do dispositivo
reaplicando a máscara; o slot sobrescrito e `masked` limpo ao desligar; o RGB do slot e o framebuffer da CPU intactos; a
Original sem a máscara; os shaders de mod antes desligam a máscara.

### 11.7 Limitações

- **Translúcidas 3D sobre o vazio ficam fora da nitidez e do bloom:** o bit da cena vem do passe A. Uma superfície 3D translúcida
  sem nada opaco atrás (a grade do chão do elevador na rota `passo_load_slot1`) fica com a máscara 0, como no AO e nas bordas da
  8.3. O lado seguro (nada é realçado por engano), registrado para a conferência visual.
- **`kHudInvW` é empírico:** medido em duas rotas. Um sprite da cena colado na câmera (z < 20) fica fora dos efeitos; um HUD com
  1/w abaixo de 0,05 (não visto) receberia os efeitos.
- **Blocos de 8×8:** a máscara do HUD é retangular por desenho, com uma rampa de até 16 px do alvo: a cena logo em volta do HUD
  recebe a nitidez e o bloom aos poucos.
- **Só na janela:** a nitidez e o bloom são da apresentação; as capturas sem janela não os mostram (a máscara pode ser vista
  sem janela só na validação, `BS_HUD_MASCARA=1`).
- **AMD integrada em 1440:** o pós inteiro leva o corredor de 50 a 29 fps; os presets não mudam, as escolhas manuais ficam.
- **A conferência visual** na janela fica com o mantenedor.
