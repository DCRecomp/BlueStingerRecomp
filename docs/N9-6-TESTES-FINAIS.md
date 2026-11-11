# N9.6: a lista dos testes finais

Decisão do mantenedor (2026-11-11, depois da fase 2b): "Vai para as próximas fases e deixa registrado que será testado ao
finalizar / Precisa existir a lista de tudo que vai testar no final".

**Regra:** a partir da fase 3, cada fase do N9.6 leva só o nível 1 (build dos três presets, `bs_unit_tests`,
`bs_render_tests`, ctest debug e release, `gera_spirv.ps1 -Conferir`) e um commit local. Tudo o que está abaixo roda **uma
vez, no fim do N9.6**, sobre o último commit, nos binários compilados dele, uma bateria pesada por vez, com o vigia de
memória e TEMP/TMP em `tmp/`. Nenhum item conta como aprovado antes de rodar: um item interrompido, pulado ou sem evidência
fica reprovado. Nenhum comparador, hash ou critério histórico é afrouxado. Esta lista cresce a cada fase (seção 4).

Legenda: **[P]** produto (`msvc-release`), **[V]** validação (`msvc-validacao`), **[J]** abre janelas do Blue Stinger
(modo espectador, sem entradas globais, sem captura da área de trabalho; avisar o mantenedor antes).

## 1. Antes da bateria

1. Os três presets compilados do mesmo commit; os exes conferidos pela data e pelo commit (`git log -1`).
2. `tools/dev/gera_spirv.ps1 -Conferir` (todos os cabeçalhos SPIR-V iguais aos fontes).
3. `bs_unit_tests`, `bs_render_tests` (inclusive `render_vulkan_ambient_occlusion*`, `material_catalog_lookup` e os testes
   das fases 3–5), ctest debug e release (os pulados sem o disco não contam).
4. Os arquivos particulares (`tools/extractor/known_versions.h`, `docs/DISTRIBUICAO-COMPILADOR.md`) com o mesmo hash.

## 2. Regressão do jogo (as edições e o estado)

| # | Teste | Critério |
|---|---|---|
| R1 | Original × `analysis/n8/referencias_n87`, os 10 casos históricos (`n8_referencias.py conferir --casos …`, layout de `tmp/sessao-20261109/layout-fase4`) [P] | 10 iguais, 0 diferentes |
| R2 | `tools/dev/n95_headless.py` H1–H7 [P][V] | 0 falhas (estado por passo, efeitos, 16:9, pilar, recuo) |
| R3 | `tools/dist/test_remaster.ps1` R1–R4 [V] | 4 casos, 0 falhas |
| R4 | `tools/dev/n95_persistencia.py` P1–P5 [P] (o `graficos.toml` com as chaves novas: `ssao_qualidade` e as das fases 3–5) | 0 falhas |
| R5 | `tools/dist/test_edicao.ps1` [P] | sem falhas |
| R6 | `tools/dev/n95_janela.py` J1–J7 [P][J] | 0 falhas |

## 3. O N9.6 (luz, materiais, sombras)

| # | Teste | Critério |
|---|---|---|
| L1 | `tools/dev/n96_luz.py --qualidade media` L1–L4 [P][V] | 0 falhas |
| L2 | idem `--qualidade baixa` e `--qualidade alta` | 0 falhas |
| L3 | A alta contra a primeira versão: as capturas da L na alta comparadas com as da fase 1 (`pytemp/n96_luz_c2f0h_3r`) | iguais fora das áreas que as fases 2b–5 mudam de propósito (registrar a caixa de cada uma) |
| L4 | `tools/dev/n96_cena.py tmp/sessao-20261109/fase3/c9` [V] | 0 alarmes, rc 0 |
| L5 | O AO e cada efeito novo desligados: a imagem bit a bit igual à do modo fiel (L2 desligado, H) | igual |
| L6 | O catálogo: `catalogo de materiais: N envios` > 0 só na Remastered; nenhum na Original | como no critério |
| C1 | Custo sem janela (`n95_desempenho.py`), corredor, cais e exterior: RTX 5080 16:9@1080 e @2160; AMD integrada 4:3@480 e 16:9@1080; desligado × cada efeito × qualidades [P] | medido e registrado; nenhuma meta inventada |
| C2 | Frametime na janela: AMD 16:9@1080 e RTX 5080 16:9@2160, desligado × efeitos [P][J] | medido e registrado |
| C3 | Memória: `mem_alvos_mib_pico` e `ao_image_bytes` por efeito; a estimativa do limite da resolução interna | medida e registrada |
| V1 | Conferência visual na janela (corredor, quarto, cais, exterior) [J] | com o mantenedor |
| X1 | Revisão Codex de cada fase (3, 4, 5) e a do conjunto no fim | achados tratados e registrados |

## 4. Itens de cada fase

### Fase 3 (emissivos e luzes dinâmicas)

| # | Teste | Critério |
|---|---|---|
| F3-1 | `n96_luz.py --casos L5` [P] (também no L1–L4 completo) | 0 falhas: a Original recusa `--iluminacao ligado`; estado e efeitos iguais; luz só na ligada (renders > 0, ao menos 1 luz); só clareia |
| F3-2 | O corredor (`passo_load_slot1`, VBlank 3800) e o quarto (4640) com a luz: as caixas que mudam (as faixas do teto, as placas, a faixa laranja) | registrar a caixa e a fração de pixels; nada fora da cena 3D muda |
| F3-3 | Luz + AO juntos (`--ssao ligado --iluminacao ligado`) | estado igual; o HUD bit a bit |
| F3-4 | Custo: `n95_desempenho.py` com `+luz` e `+ao-media+luz` (RTX 5080 16:9@1080 e @2160; AMD 4:3@480 e 16:9@1080) [P] | medido e registrado |
| F3-5 | Visual na janela: o raio, a intensidade e a força (400, 1, 0,6) [J] | com o mantenedor |
| F3-6 | `graficos.toml` com `iluminacao = true` na Remastered e na Original (P1–P5; a Original volta a falso pelo `clamp_to`) | 0 falhas |
| F3-7 | Revisão Codex da fase 3 (`gatherLights`, `lights.comp`, `lightObjects`/`writeLightSet`, a ligação no `main_sdl`) | achados tratados |

### Fase 4 (sombras: volumes com borda suave e sombras de contato)

| # | Teste | Critério |
|---|---|---|
| F4-1 | `n96_luz.py --casos L6` [P] | 0 falhas: a Original recusa `--sombras aprimoradas`; estado e efeitos iguais; a borda suave só nas aprimoradas; o HUD do canto igual |
| F4-2 | As rotas com volumes de sombra (a sombra do personagem; o cais e o exterior): a contagem `borda suave dos volumes em N renders` e a caixa do que muda | N > 0 onde houver volume; mudanças só nas bordas das sombras e nos pontos com sombra de contato |
| F4-3 | O caminho fiel dos volumes intacto: Original × `referencias_n87` (R1) e as cenas de fidelidade (`bs_render_tests`) | 10 iguais; 0 falhas |
| F4-4 | Custo: `n95_desempenho.py` com `+sombras` e `+ao-media+luz+sombras` (RTX 5080 @1080 e @2160; AMD @480 e @1080) [P] | medido e registrado |
| F4-5 | Visual na janela: a largura da penumbra, as sombras de contato das faixas do teto [J] | com o mantenedor |
| F4-6 | `graficos.toml` com `sombras = "aprimoradas"` (P1–P5; a Original volta a `original`) | 0 falhas |
| F4-7 | Revisão Codex da fase 4 (`volumes.comp -DMASK`, `soft_shadows.comp`, `softObjects`/`writeSoftSets`, `occluded` no `lights.comp`) | achados tratados |

### Fase 5 (menu, `graficos.toml`, matriz de desempenho)

| # | Teste | Critério |
|---|---|---|
| F5-1 | `n95_janela.py` J1–J7 com as entradas novas do menu [J] (R6) | 0 falhas |
| F5-2 | `n95_persistencia.py` P1–P5 com `ssao`, `ssao_qualidade`, `iluminacao` e `sombras` no `graficos.toml` (R4) | 0 falhas; a Original volta tudo a desligado |
| F5-3 | A matriz completa (C1) com os sufixos combinados | medida e registrada |
| F5-4 | **Decisão do mantenedor:** o preset "Alto" com `sombras = "aprimoradas"` | decidido e registrado |
| F5-5 | Revisão Codex da fase 5 (menu e ligações) | achados tratados |
| F5-6 | Uma Preview nova pelo Builder (`tools/dist/make_preview.ps1 -Disc`, saída e cache em `tmp/`) e `test_edicao.ps1 -Preview` nela (R5) | rc 0; 0 falhas |

## 5. Depois da bateria

1. A auditoria (`docs/N9-6-AUDITORIA.md`) e o `STATUS.md` com os resultados, separados por binário e data.
2. O master avançado preservando a água; o mesmo `git status --short` e o mesmo `--numstat` da árvore principal.
3. O que sobrar como risco vai para o N9.12 (`docs/N9-PLANO.md` seção 5).
