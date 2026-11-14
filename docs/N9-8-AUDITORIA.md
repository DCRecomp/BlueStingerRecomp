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
