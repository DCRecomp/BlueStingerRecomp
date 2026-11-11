# N9.7: água e reflexos

**Autorizado pelo mantenedor em 2026-11-11** ("autorizo iniciar oficialmente o N9.7"), com as decisões D7-1 (a água
original por padrão, inclusive no Alto; a aprimorada só por escolha manual, na Remastered), D7-2 (a identificação pelo
catálogo de materiais do N9.6, sem lista própria) e D7-3 (um caminho de shader próprio, o fiel intacto). Plano e gates em
`docs/N9-7-PLANO.md`. Ponto de partida: o protótipo do Codex de 2026-11-08 (branch `n9-7-agua-prototipo`, `8cf3971`, e a
árvore principal), aproveitado e corrigido.

## 1. Arquitetura

| Parte | Onde | O que faz |
|---|---|---|
| Identificação | `native/render/material_catalog` | `kMaterialWater` por `TextureId` da textura **original** (dimensões + FNV-1a dos texels ARGB8888). Uma linha: o mar do cais RCG00, `32x32-7458e3539d31cd8d`. Sem lista própria, sem marca no handle, sem cor, alfa ou lista de desenho. O catálogo só chega ao dispositivo na Remastered (`render_capture::set_materials`), então a Original nunca tem um desenho de água. Uma substituição HD (N9.8) continua com o material da original: a identidade é calculada antes da troca (`render_core`, `texture_handle`). |
| Qualidade e fase | `native/render/water.h` | `WaterQuality` (original, aprimorada, alta); a fase das ondas é o número do render que o `Renderer` conta (`water_phase`, módulo 240 000): é do jogo, não do relógio nem do dispositivo, e sobrevive à recriação do Vulkan e ao recuo para o software (sem salto nas ondas). `water_extent`: o horizonte e o fundo dos desenhos de água do quadro. |
| Ligação | `transitional/render_core` | `render_capture::set_water` guarda a qualidade; a cada render do framebuffer (640 × 480, só no nativo) o Vulkan recebe `set_water(qualidade, render)`. Nada na IR muda: nem vértices, nem UVs, nem handles (o protótipo copiava todos os vértices do quadro e animava as UVs na CPU). |
| Vulkan | `native/render/vulkan/vk_device.cpp` | Um desenho é de água quando é translúcido e a textura tem `kMaterialWater`. Num quadro com um deles e a qualidade ligada: a imagem da cena (`waterScene_`, RGBA8 no alvo) nasce sob demanda, a cor opaca é copiada para ela entre os passes A e B (depois do AO, da luz e dos volumes do N9.6), e só esses desenhos vão pela variante `-DWATER` do fragmento, com os 16 bytes a mais do push constant (qualidade, fase, horizonte, fundo). Os outros desenhos recebem os 64 bytes de antes e o shader fiel. A imagem sai com a água desligada e na troca do alvo; sem memória para ela, a água fica original até o alvo mudar. O binding 5 aponta para a textura neutra quando a imagem não existe. |
| Shader | `native/render/vulkan/shaders/pvr.frag` (`#ifdef WATER`) | O movimento da textura (as três ondas do protótipo sobre a UV, agora por pixel: no modo fiel a UV vem dos dados do triângulo, calculados na CPU, e um deslocamento no vértice não chegaria lá), as normais procedurais, a refração e o reflexo da cena opaca em espaço de tela (espelhado no horizonte da malha), o tom de céu e maresia fora da imagem, Fresnel, especular e cristas. Seis variantes novas (ordem e autosort × os três ambientes FP). **Os sete cabeçalhos SPIR-V fiéis (`pvr_frag*.h` e `pvr_vert.h`) são byte a byte os do `f690ecb`.** |
| Configuração | `graphics_settings`, `settings_menu`, `main_sdl`, `game_host` | `[imagem] agua = "original" \| "aprimorada" \| "alta"`; a linha "Água" / "Water" no menu Gráficos (original / aprimorada / alta; en-US: original / enhanced / high), só com `Features::water` (a Remastered com o Vulkan; a Original e o software: "em breve" e inativa). Em todos os presets e na instalação nova, original (D7-1); mudar a água torna o arquivo "personalizado". `--agua original\|aprimorada\|alta` vale só na execução (não vai para o arquivo); sem janela, a Original e o software recusam com 2. Troca durante a execução pelo menu (`GameHost::setWaterQuality`). |
| Diagnóstico (só validação) | `vk_device`, `game_host` | `BS_AGUA_DIAGNOSTICO=1` / `=2` pintam os desenhos de água de magenta / verde (o pixel diferente entre os dois é o domínio da água); `BS_AGUA_TROCA=<vblank>:<qualidade>[,...]` troca a qualidade pelo caminho do menu. O produto ignora as duas. |
| Estatísticas | `VulkanStats`, resumo `[render-vk]` | `agua em N renders (M desenhos, imagem da cena ate K KiB)`. |
| Ferramenta | `bs-ir-tela agua <ir> <bmp> <q> <quadro>` | O material do jogo numa IR guardada, pelo catálogo e pela API do dispositivo. O `bs-ir-tela` continua só no build de validação (o protótipo o levava ao Release). |

## 2. O que mudou em relação ao protótipo

| Protótipo (2026-11-08) | Agora |
|---|---|
| Lista própria (`is_water_surface`) | o catálogo de materiais (`kMaterialWater`) |
| A marca nos bits 30 e 31 do handle (as consultas do catálogo por handle errariam numa textura que fosse água e outro material) | nada no handle; o dispositivo consulta o material da textura |
| `shadeWater` dentro do `pvr.frag` fiel, push constant de 80 B em todo desenho | a variante `-DWATER`; o fiel com o SPIR-V e os 64 B de antes |
| `waterScene_` em todo alvo, inclusive na Original e com a água desligada (+4 B/px sempre) | só num quadro com água e a qualidade ligada; liberada ao desligar |
| A cópia de todos os vértices do quadro e as ondas nas UVs, na CPU | as ondas no fragmento; nenhuma cópia |
| Duas fases: o número de quadros do dispositivo (zerado na recriação) e o render do `Renderer` | uma só: o render do `Renderer` |
| A água aprimorada na Remastered nova e nos presets Equilibrado e Alto | original em tudo (D7-1) |
| A estimativa do limite da resolução interna sem a imagem da cena | + 4 B/px (96 + 4 + 4) |
