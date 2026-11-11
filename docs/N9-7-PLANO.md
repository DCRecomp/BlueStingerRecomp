# N9.7: a preparação (água e reflexos)

**Situação (2026-11-11): só análise, para aprovação. O N9.7 não começou oficialmente** (mantenedor, 2026-11-11: "Não
autorizo ainda a implementação oficial do N9.7"). Nada do sistema de água foi alterado.

O protótipo do Codex (2026-11-08) está em dois lugares, os dois preservados:
- a branch local `n9-7-agua-prototipo` (`8cf3971`, sobre o `17963bb` do N9.5 fase 4), 32 arquivos;
- a árvore principal, sem commit, junto do N9.6 já integrado: `native/render/water.{h,cpp}`, `tests/unit/test_water.cpp`,
  `docs/N9-7-AGUA.md`, os trechos de água em `vk_device.cpp`, `pvr.frag`/`pvr.vert`, `render_core`, `render_capture.h`,
  `graphics_settings`, `edition`, `settings_menu`, `game_host`, `main_sdl`, `tools/ir_tela/main.cpp` e os cabeçalhos SPIR-V
  regenerados com os dois. A integração com o N9.6 já foi feita a cada avanço do master (binding 5 da água, 6 do catálogo).

## 1. O que o protótipo faz

- **Seleção:** uma lista fechada própria por `TextureId` (`is_water_surface`): o mar do cais RCG00, `32x32-7458e3539d31cd8d`,
  46 desenhos translúcidos. Nada por cor, alfa ou lista.
- **CPU (`render_core`):** com a água ligada e uma textura de água no quadro, copia todos os vértices do quadro, anima as UVs
  dos desenhos de água (`animate_water`, três ondas; a fase é o número do render) e marca a textura nos bits 30 e 31 do
  handle; a IR e o estado do jogo não mudam.
- **Vulkan:** o push constant de todo desenho cresce de 64 para 80 B (`water[4]`: qualidade, tempo, horizonte, fundo); o
  horizonte e o fundo saem dos y dos vértices de água no quadro. Entre os passes A e B (depois do AO, da luz e dos volumes),
  copia a cor opaca para `waterScene_` e a liga no binding 5 do conjunto do quadro. Em `pvr.frag`, `shadeWater` (normais
  procedurais, refração, reflexo espelhado no horizonte da malha, Fresnel, cor de céu fora da tela, especular, cristas), antes
  do clamp e do fog.
- **Configuração:** `[imagem] agua = "original" | "aprimorada" | "alta"`, a linha "Água" no menu (entre Iluminação aprimorada
  e Texturas HD), `Features::water`, `EditionProfile::enhanced_water` (a Original força original), `GameHost::setWaterQuality`.
  **Padrões do protótipo:** a Remastered nova começa em `aprimorada`; o preset Equilibrado traz `aprimorada` e o Alto `alta`.
- **Ferramentas:** `bs-ir-tela materiais <ir> <pasta>` (texturas e `desenhos.tsv`) e `bs-ir-tela agua <ir> <bmp> <q> <quadro>`.
- **Testes:** `test_water.cpp` (o ID exato e os negativos, só a água recebe UV e marca, geometria e profundidade iguais,
  original neutra). A prova Vulkan sintética citada em `docs/N9-7-AGUA.md` não está em `test_render_vulkan.cpp`.

## 2. Análise por ponto

| Ponto | Situação no protótipo | O que o N9.7 precisa |
|---|---|---|
| Catálogo de materiais | a água tem a sua própria lista (`is_water_surface`), fora do `material_catalog` (decisão da 2b: um catálogo só para o N9.6, o N9.7 e o N9.8). A marca nos bits altos do handle faria as consultas do catálogo por handle (AO das translúcidas, emissivos) errarem numa textura que fosse água e material ao mesmo tempo | a água vira um flag do catálogo (`kMaterialWater`, com a qualidade como parâmetro), entregue ao dispositivo por `set_texture_material` como os outros; a marca nos bits do handle sai (ou é retirada antes de toda consulta, com teste) |
| Descritores Vulkan | binding 5 (sampler combinado) no conjunto do quadro; o pool com 2 samplers combinados; `waterScene_` em `SHADER_READ_ONLY` depois da 1ª cópia, `UNDEFINED` antes | sem a imagem, a textura neutra no binding 5 (como o 6 do catálogo); a escrita do conjunto marcada inválida na troca do alvo (o mesmo padrão do `aoSurfBound_`); camadas de validação no teste |
| Shaders e SPIR-V | `shadeWater` dentro do `pvr.frag` fiel, nas 6 variantes; o push constant maior em todo desenho | a água como variante própria (`-DWATER`, como o `ao.comp -DSURF` e o `volumes.comp -DMASK`), com os SPIR-V fiéis iguais aos do N8.7 (prova pelo `gera_spirv -Conferir` e pelos testes de fidelidade); o push constant fiel em 64 B |
| Imagens de reflexo e refração | `waterScene_` (RGBA8, 4 B/px) criada em todo alvo, também na Original e com a água desligada | sob demanda (só com a água ligada e uma superfície no quadro, como `aoSurf_` e `shadowMask_`), liberada ao desligar e na troca do alvo; `stats` com os bytes |
| Orçamento de VRAM | não entra na estimativa do limite da resolução interna | + 4 B/px na estimativa quando a água estiver ligada; a correção da estimativa inteira continua no N9.12 |
| Widescreen e pilar dinâmico | o alvo largo é usado inteiro (o horizonte vem dos y, que o hor+ não muda) | provas no 16:9 (o reflexo perto das bordas e o pilar, critério H6) e na escala 2 |
| SSAO, luz e sombras | a cópia vem depois do AO, da luz e dos volumes: o reflexo já os inclui | registrar a ordem; um teste com os quatro ligados; nenhum AO do catálogo nas superfícies de água |
| Original × Remastered | a Original força `original` no menu e no `main_sdl`; mas `waterScene_` e o push de 80 B também existem nela | o modo fiel sem nenhum recurso da água (nem a imagem nem o push maior); Original × `referencias_n87` (10 casos) igual |
| Conflitos de integração | os mesmos arquivos do N9.6 (`vk_device.cpp`, `pvr.frag`, `graphics_settings`, `settings_menu`, `main_sdl`, `render_core`); a linha "Água" desloca as posições do menu que `tools/dev/n96_presets.py` usa (Efeitos de imagem passa de 12 para 13); o protótipo põe a água no Equilibrado e no Alto | um avanço por vez com backup, como no N9.6; a navegação do `n96_presets.py` ajustada junto; os presets decididos pelo mantenedor (seção 4) |
| Testes existentes | `test_water.cpp`; `bs-ir-tela agua` na IR do cais | seção 3 |
| Configurações gráficas | `agua` no `graficos.toml`, no menu, na Remastered nova em `aprimorada` | seção 4 |

## 3. Plano incremental (cada passo com autorização e um gate)

| Passo | O que | Gate |
|---|---|---|
| A0 | Partir da branch `n9-7-agua-prototipo` numa worktree isolada, rebaseada sobre o master do N9.6; comparar com a árvore principal arquivo a arquivo (o protótipo é um só, nos dois lugares) | diferenças só nos trechos do N9.6; arquivos particulares e backup intactos |
| A1 | O catálogo: a água como `kMaterialWater` no `material_catalog`; sai `is_water_surface`; sem a marca nos bits do handle | `bs_unit_tests` com os casos do `test_water.cpp` movidos; nenhum outro material muda (`material_catalog_lookup`) |
| A2 | O Vulkan isolado: a variante `-DWATER`, o push fiel de 64 B, `waterScene_` sob demanda e a textura neutra no binding 5 | `gera_spirv -Conferir` com os SPIR-V fiéis iguais aos do master; `bs_render_tests` (fidelidade); testes novos `render_vulkan_water` (desligada = software bit a bit; ligada: muda só a superfície; escala 2 e 854; troca do alvo no mesmo dispositivo; nada sobra) |
| A3 | A ligação: `[imagem] agua`, `--agua` na linha de comando (só a Remastered com o Vulkan; a Original recusa com 2, como `--sombras`), o menu, as estatísticas (`agua em N renders`) | `bs_unit_tests` (`graphics_settings`, `settings_menu`, `edition`); `n96_presets.py` com a água |
| A4 | A suíte `n97_agua.py` (como a `n96_luz.py`): a rota `passo_cgate` no cais; Original recusa; estado por passo e efeitos iguais; água em N > 0 renders; nada fora da caixa do mar muda; 16:9 e 1080 | 0 falhas |
| A5 | O inventário das outras áreas (`bs-ir-tela materiais` nas IRs já capturadas: corredor, quarto, cais, exterior); cada superfície nova com a sua prova e a aprovação do mantenedor | a lista por área, com a identificação usada e os negativos |
| A6 | Custo e memória (`n95_desempenho.py +agua`, RTX 5080 em 1080 e 2160, AMD em 480 e 1080) | medido e registrado, sem meta inventada |
| A7 | A bateria de fechamento: R1 (Original × `referencias_n87`), H1–H7, R1–R4, P1–P5, J1–J7, L1–L7 do N9.6 e a A4; revisão Codex; o visual na janela com o mantenedor | 0 falhas; achados tratados |

Superfícies molhadas, SSR por marcha de raio e o que mais o N9.7 prevê ficam para depois do A7, cada um com a sua
autorização.

## 4. Decisões para o mantenedor antes do A0

1. **Os padrões da água.** O protótipo liga a água `aprimorada` na Remastered nova e nos presets Equilibrado (`aprimorada`) e
   Alto (`alta`). Pela decisão de 2026-11-11 (efeitos novos só por escolha manual, os presets seguros), a proposta é a água
   `original` em todos os presets e na instalação nova, com a escolha manual no menu, até o custo e o visual serem medidos
   no A6 e no A7.
2. **Unificar no catálogo (A1)** em vez de manter a lista própria da água.
3. **Isolar o shader (A2)** em uma variante, em vez do ramo dentro do `pvr.frag` fiel.
