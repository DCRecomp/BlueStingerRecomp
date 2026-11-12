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

## 3. Inventário das superfícies de água (A5, 2026-11-11)

Método: `bs-ir-tela materiais` nas 26 IRs guardadas (as 22 cenas da fase 3B do N9.5 em `tmp/sessao-20261109/fase3/c9`:
título, C-GATE, heliporto, combate e load; e as 4 do N9.6 em `tmp/sessao-20261110/n96`: RCG00, RCG00A, RCG02 e RCG03), mais
209 IRs guardadas de 10 em 10 renders no trecho do cais da rota `passo_cgate` (renders 15 100 a 17 200, `BS_RENDER_IR_GUARDAR`
com o intervalo novo). Scripts em `tmp/sessao-20261111/n97` (`inventario.py`, `animacao.py`).

| Área e cena | TextureId | Desenhos | Material e transparência | Animação original | Reflexo e refração | Limites |
|---|---|---|---|---|---|---|
| Cais RCG00 (rota `passo_cgate`, VBlanks ~30 600 a ~34 300; também nas cenas do C-GATE p4000, do combate p10000, do heliporto p7900 e p11240 e na RCG03 r24000) | oito texturas 32×32: `cdbab87a1d8d396d`, `7458e3539d31cd8d`, `261eb2b953a38814`, `2b81e35cd4ec3972`, `6737c065625ed694`, `8fdc859527593d4b`, `0de910958b4effaf`, `5ffc9e41aef13e20` | 24 a 30 por render (46 na IR do protótipo) | translúcida, `SrcAlpha/InvSrcAlpha`, `ModulateAlpha`, com fog por tabela no horizonte | **o jogo troca a textura**: as oito se revezam no mesmo papel, uma por render, sempre com o mesmo número de desenhos daquele render | sim: a cena opaca acima do horizonte da malha (espelhada) e por baixo (refração) | o horizonte e o fundo vêm dos vértices da água no quadro (um plano só por quadro); o reflexo é de espaço de tela (o que está fora da imagem vira o tom de céu) |

- **O defeito que o inventário achou:** o catálogo (e o protótipo) tinha só `7458e3539d31cd8d`. A água aprimorada aparecia
  num render em cada oito e piscava. Com as oito, a água está em 2 095 renders da rota (antes, 286), e o n97_agua exige a
  água em todo quadro com o mar visível.
- **Negativos:** três texturas 32×32 aparecem junto com o mar no cais (`e19fbff0ce31792a`, um círculo cinza;
  `525a701d686a76a5`, vermelho escuro; `2606c337ad632f8d`, um brilho vermelho). Não são água e ficam fora (testadas).
- **Candidatas sem evidência, fora do catálogo:** as texturas translúcidas que cobrem mais de 3 % do quadro nas cenas (115
  linhas, 40 texturas distintas; a folha `tmp/sessao-20261111/n97/inv/candidatas.png`, conferida a olho): além dos quadros do
  mar, nenhuma é água pela textura (paredes, fumaça, letreiros, a faixa
  laranja já catalogada como emissiva). Não há superfície molhada (chão molhado, poças) identificada nessas cenas.
- **Não verificado:** as áreas que as rotas e saves disponíveis não alcançam (a campanha além do heliporto e do combate). O
  inventário não se estende a elas.

## 4. Custo e memória (A6, 2026-11-11)

`n95_desempenho.py` no trecho "mar" (rota `passo_cgate`, VBlanks 30 500 a 34 300), produto do commit `efbc49d`. GPU por quadro
em ms (média dos renders com timestamps), sem janela:

| GPU e configuração | Água original | Aprimorada | Alta |
|---|---|---|---|
| RTX 5080, 4:3@1080 | 2,10 | — | 2,07 |
| RTX 5080, 16:9@1080 | 2,29 | 2,42 | 2,51 |
| RTX 5080, 16:9@2160 | 3,83 | 4,03 | 3,92 |
| AMD Radeon integrada, 4:3@480 | 9,72 | 10,09 | 10,06 |
| AMD Radeon integrada, 16:9@1080 | 56,1 | 58,0 | 58,2 |

Na janela (o modo espectador; quadros apresentados):

| GPU e configuração | fps | p50 | p95 | p99 (ms) | GPU (ms) |
|---|---|---|---|---|---|
| RTX 5080 16:9@2160, original | 59,98 | 16,49 | 18,23 | 19,10 | 4,73 |
| RTX 5080 16:9@2160, alta | 59,96 | 16,56 | 18,23 | 19,22 | 4,82 |
| AMD 16:9@1080, original | 46,5 | 14,0 | 63,8 | 67,6 | 56,2 |
| AMD 16:9@1080, alta | 45,3 | 13,7 | 66,4 | 69,9 | 58,4 |

- **Memória:** a imagem da cena só existe com a água ligada e o mar no quadro, do tamanho do alvo. Na matriz (binário
  `efbc49d`, que ainda informava o tamanho lógico): 1 200 KiB em 640 × 480, 6 075 KiB no 4:3@1080, 8 108 KiB no 16:9@1080 e
  32 425 KiB no 16:9@2160. Desde o `a6fbf40` o resumo informa o alocado pelo driver, ~7 % acima por alinhamento na RTX 5080
  (1 310 720 bytes para os 1 228 800 lógicos do 640 × 480, no teste unitário). Os alvos
  (`mem_alvos_mib_pico`) sobem o mesmo tanto (RTX 16:9@1080 610,8 → 619,3 MiB; @2160 2 433,4 → 2 465,4; AMD @480 95,6 → 96,8).
  Com a água original ou na Original: 0. A estimativa do limite da resolução interna conta + 4 B/px.
- **CPU:** a thread do jogo igual (82–83 % sem janela na RTX; 20–21 % na AMD em 1080), sem a cópia de vértices do protótipo.
- **Leitura (sem meta inventada):** a água custa ~0,1–0,2 ms na RTX 5080 e ~0,3 ms na AMD em 480; na AMD em 1080 ~2 ms, sobre
  um jogo que já não fecha 60 fps ali sem ela. Nada liga a água sozinho (D7-1): sem detecção de GPU nem redução automática.
- **Execução incompleta registrada:** na primeira matriz, a AMD 16:9@1080 com a água na janela bateu no limite de tempo
  (o jogo mais lento que o tempo real) e não contou; o limite ganhou folga e o par foi repetido completo (a tabela acima).

## 5. Testes e regressões (A4 e A7, 2026-11-11)

Bateria final `tmp/sessao-20261111/n97/bateria_a7.ps1` nos binários do `cec9842`, uma bateria pesada por vez, com o vigia
de memória (pico 5 154 MB, commit livre mínimo 19,44 GB).

| Item | Resultado |
|---|---|
| SPIR-V | `gera_spirv -Conferir` 45/45; os sete cabeçalhos fiéis byte a byte os do `f690ecb` (0 diferentes) |
| Unitários | `bs_unit_tests` 379/0, `bs_render_tests` 107/0 (cinco testes da água), `bs_ui_tests` 25/0, `bs_remaster_tests` 5/0 |
| Camada de validação | `bs_render_tests` inteiro sob `VK_LAYER_KHRONOS_validation`: 0 erros (controle positivo: com os avisos ligados, a camada reporta). A primeira passada achou um erro antigo do N9.6 (o `aoSurf_` em `UNDEFINED` no passe A), corrigido no `17e1105` |
| ctest | debug 30/30, release 23/23; com o disco e a Preview: `real_disc`, `runtime_data`, `analysis_real`, `movie_manifest` aprovados; **`boot_trace` sem aprovação** (os snapshots do Flycast não existem nesta máquina) |
| `n97_agua.py` W1–W6 | 89 ok, 0 falhas: a Original e o software recusam; estado e efeitos idênticos (3 137 passos); só o domínio da água muda (4:3, 16:9 em 1080, com o AO, a luz e as sombras); a água em todo quadro com o mar; trocas de qualidade com os quadros exatos; a recuperação do Vulkan sem salto nas ondas |
| N9.6 L1–L7 | 0 falhas (água desligada) |
| H1–H7, R1–R4 | 0 falhas; R4 Remastered = Original sem a câmera, 1 030 passos |
| Original × `referencias_n87` | 10 casos, 0 diferentes |
| Preview nova pelo Builder e `test_edicao` | rc 0; 11 ok, 0 falhas (a primeira tentativa falhou por faltarem as fontes do SDL3 e do FreeType no build da worktree; copiadas do build principal) |
| P1–P5, J1–J7 | 0 falhas |
| `n96_presets.py` Q1–Q11 | Q8–Q11 (a água: menu, arquivo, reabertura, `--agua` sem gravar, a Original sem água, o Vulkan aplicando no cais em 325 renders) ok. **Q4 e Q5 sem aprovação nesta rodada**: o SSAO não foi desfeito pelo menu porque o dispositivo Vulkan foi perdido e recriado no meio da sequência, e as entradas do controle virtual se perderam no travamento. Os mesmos Q4/Q5 passaram na rodada do `817f86d` e no N9.6. Ver o item seguinte |
| Varredura de publicação | `f690ecb..n9-7`: 7 commits, 0 achados |
| Revisão Codex | `reviews/codex-20261111-121354.md`: quatro achados, todos aceitos e corrigidos (`a6fbf40`: os contadores só de renders concluídos, a prova de que a cena copiada é usada, o tamanho alocado; e a camada de validação) |

**Perda do dispositivo na janela (antiga, fora da água):** com o preset Alto (1440p interno, escala 3), o Vulkan perde o
dispositivo (`vkWaitForFences`, `VK_ERROR_DEVICE_LOST`) perto do render 680, logo depois do menu inicial nativo da
Remastered, e se recupera. Reproduzido em 2026-11-11 com o binário do N9.6 (`f690ecb`, 3 de 4 execuções) e com o do N9.7
(3 de 3), sem a água; sob a camada de validação não houve perda nem erro (dependente do tempo). Nas sessões do N9.6 de
2026-11-10/11 não tinha aparecido. Não é do N9.7; registrado para o N9.12 e para a decisão do mantenedor
(`tmp/sessao-20261111/n97/repro_perda.py`).

## 6. Limitações e problemas conhecidos

- **Reflexo em espaço de tela:** só reflete o que está na imagem; acima da borda de cima, o tom de céu e maresia. O plano do
  espelho é o horizonte dos vértices da água no quadro (um plano por quadro); com dois corpos d'água em alturas diferentes no
  mesmo quadro, o mais alto define o plano. SSR por marcha de raio não foi feito (o plano do N9.7 o deixou para depois do A7).
- **Uma superfície:** só o mar do cais. Superfícies molhadas e outras águas ficam até haver captura e prova (seção 3).
- **Translúcida por ordem × software (antiga, fora do N9.7):** numa cena sintética, a translúcida na ordem de submissão difere
  do software no Vulkan já no master `f690ecb` (8 000 pixels, alfa cheio). O jogo usa o autosort, que é bit a bit; registrado
  para o N9.12.
- **Visual:** a conferência na janela (o movimento, o reflexo, o horizonte, a fog, o 16:9) é do mantenedor e está pendente.
- **Desempenho na GPU integrada em 1080:** a água soma ~2 ms a um quadro que já passa de 16,7 ms (seção 4).

## 7. Pendências (Linux e N9.12)

- **Linux:** a água não foi compilada nem testada no Linux (nem o resto do N9.6). Os shaders são os mesmos SPIR-V; a bateria
  do Linux continua com o N9-L e o mantenedor.
- **N9.12:** a estimativa de VRAM do limite da resolução interna (agora 96 + 4 + 4 B/px; sem a máscara das sombras e sem a
  imagem do AO das translúcidas, e a constante de 96 B/px contra ~268 medidos); a translúcida por ordem × software; o
  `boot_trace` sem os snapshots do Flycast; a perda do dispositivo na janela com o Alto (seção 5).

## 8. Recomendação para o N9.8

O catálogo de materiais por `TextureId` já serve às três frentes: a identidade é a da textura original e a substituição HD
continua com o material dela. Para o N9.8 (texturas HD, antialiasing e pós-processamento): (1) as substituições HD dos oito
quadros do mar têm de manter as oito identidades originais no manifesto (senão a água volta a piscar); (2) o MSAA precisa
levar em conta a cópia da cena da água (a imagem é de uma amostra) e as imagens do AO, da luz e das sombras do N9.6; (3)
manter o mesmo padrão: variantes de shader separadas do fiel, recursos sob demanda, testes Vulkan sob a camada de validação.

## 9. Integração no master (2026-11-11, autorizada pelo mantenedor)

- **Estado inicial:** master `f690ecb`; `n9-7` `9bd13ac`; `n9-7-agua-prototipo` `8cf3971`; a árvore principal com 30
  arquivos modificados e 6 não rastreados (o protótipo, os dois arquivos particulares, `_bs-extract.exe` e
  `tools/extractor/build/`).
- **Backup fora da árvore de trabalho** (pedido do mantenedor; exceção registrada à regra de não criar nada fora da pasta de
  trabalho): `D:\BlueStinger-backup\n97-integracao-20261111`. Contém os 35 arquivos modificados e não rastreados (sem os
  artefatos de `tools/extractor/build/`), o diff completo, um bundle Git com as três branches (verificado pelo `git bundle
  verify`) e o manifesto SHA-256, conferido com `sha256sum -c` (37 entradas íntegras). Também o `generated/` antigo, com o
  manifesto dele.
- **Comparação:** os 32 arquivos do protótipo na árvore principal eram idênticos ao A0 `c9a6367` (0 diferentes). O
  protótipo está preservado no `c9a6367`, na branch `8cf3971` e no backup.
- **Método:** `f690ecb` é ancestral de `9bd13ac`, então a integração é um avanço rápido. Os arquivos foram gravados com o
  conteúdo do `9bd13ac` e o fim de linha do `.gitattributes` (script com ensaio,
  `tmp/sessao-20261111/n97/integra_n97.py`): 50 arquivos (32 do protótipo, 11 limpos e 7 novos), conferidos contra o commit.
  Depois, `update-ref` do master de `f690ecb` para `9bd13ac` e `git reset -q` (só o índice). Os arquivos particulares, o
  `_bs-extract.exe` e `tools/extractor/build/` ficaram intocados. Sem conflitos. Depois disso, o `git status` mostra só os
  dois particulares e os dois não rastreados.
- **O `generated/` da árvore principal estava velho** (22/10, sem os ganchos `BS_HUD_SCOPE` da fase 3B do N9.5). Ele foi
  regenerado pelo `bs-recomp` do `9bd13ac` e ficou igual ao `generated-f3` que as worktrees usam (0 diferenças); o antigo
  ficou no backup.
- **Gate I2 bloqueado na árvore principal:** o arquivo particular `tools/extractor/known_versions.h` tem uma modificação
  local (duas entradas `hedhunter_v1006_*`) sem o `};` que fecha a lista. Com isso, o extrator (`bs_disc_data`, ligado ao
  jogo) não compila. O arquivo é protegido e não foi alterado. Os arquivos rastreados da árvore principal são exatamente os do
  `9bd13ac`, e a worktree `tmp/wt-n97` compila esse commit. Os testes abaixo usam os binários dela (o mesmo código: entre o
  `cec9842` da bateria A7 e o `9bd13ac` só mudou documentação). A compilação da árvore principal espera a decisão do
  mantenedor sobre o arquivo particular.

## 10. A perda do dispositivo Vulkan (investigação delimitada, 2026-11-11)

- **Sintoma:** na janela, `vkWaitForFences` do render devolve `VK_ERROR_DEVICE_LOST` (−4) nos renders 678–681 (a tela de
  título), em seguida falha a cerca da apresentação, e a recuperação recria o dispositivo com sucesso. Acontece na Remastered
  e na Original. GPU: RTX 5080, driver 621.01.
- **Fatores** (`tmp/sessao-20261111/n97/repro_fatores.py`, 3 execuções de ~40 s por caso, a mesma cena):

| Caso | Resolução | Correção de cor | Nitidez | Perdas |
|---|---|---|---|---|
| Alto | 1440p | sim | sim | 2/3 |
| Alto sem nitidez | 1440p | sim | não | 1/3 |
| Alto sem correção de cor | 1440p | não | sim | 0/3 |
| Alto sem pós-processamento | 1440p | não | não | 0/3 |
| Equilibrado com nitidez | 1080p | sim | sim | 1/3 |
| Original em 1440p | 1440p | não | não | 0/3 |
| Alto em 1080p | 1080p | sim | sim | 2/3 |

- **Leitura:** a resolução não decide. Sem o pós-processamento da apresentação, 0 em 6; com a correção de cor e a nitidez
  (dois passes), 5 em 9. É o mesmo comportamento no binário do N9.6 (`f690ecb`, 3 de 4) e no do N9.7 (3 de 3): não é da água.
- **Camada Khronos:** com a validação padrão e com a de sincronização (`VALIDATE_SYNC`, também no envio), 0 erros e 0 perdas
  (a camada muda o tempo).
- **Descartado na leitura do código:** duas threads na mesma fila (a apresentação usa a segunda fila da família, com trava
  própria; com uma fila só, a mesma trava); `vkDeviceWaitIdle` fora das duas travas (só na destruição); o `post_run` do
  renderizador (só nos testes); o shader de cor (sem laço, uma leitura).
- **Recuperação e configurações:** o estado aplicado sobrevive (depois de recriado, o AO continuou em 210 renders, a escolha
  feita antes). As entradas do controle virtual, que são por tempo, se perdem enquanto a janela trava. Foi isso que reprovou
  o Q4/Q5 da rodada A7.
- **Causa não demonstrada nesta investigação** (2026-11-11): nenhuma correção. **Demonstrada e corrigida na estabilização de
  2026-11-12 (seção 11).**
- **Próximos experimentos recomendados:**
  1. pedir `VK_EXT_device_fault` (o endereço e o motivo da falha, se o driver informar);
  2. um modo de validação com uma fila só (a apresentação na fila do renderizador), para isolar a sincronização entre filas
     (o slot escrito numa e lido na outra) e as imagens da cadeia de pós-processamento;
  3. 10 execuções por caso, para separar a correção de cor da nitidez com confiança;
  4. as mesmas execuções com o driver anterior, se o mantenedor quiser.
- **Q4/Q5 repetidos** (`n96_presets.py`, binários do `9bd13ac`): 0 falhas. No Q4 o renderizador recebeu os três efeitos
  desligados (o AO tinha rodado em 1 444 renders) e não houve recriação nessa execução. A perda continuou em 13 outras
  execuções da suíte (inclusive em duas do Q2), sempre antes das sequências do menu, e a suíte passou porque elas terminaram.
  O defeito ficou aberto até a seção 11.

## 11. Estabilização após o N9.7: a compilação do master e a perda do dispositivo (2026-11-12, autorizada)

### 11.1 A compilação da árvore principal

- **`tools/extractor/known_versions.h` (particular, fora dos commits):** autorizado pelo mantenedor só o fechamento `};` que
  faltava depois das entradas `hedhunter_v1006_*`. Backup antes da edição em `D:\BlueStinger-backup\estabilizacao-20261112` e
  `tmp/sessao-20261112`. SHA-256 antes `c4ad9e1a87788ed448c596813d1cf79a1cbe394d3349bb8f2e9bb673cd1f4fc8`, depois
  `bfee43ca0e8379ab093081bec84225a667a34920f46ac96e9086f9d87d3bbff1`. Diff: uma linha (`};` depois da linha 22); as entradas
  `hedhunter_*` intactas.
- **Gate C1 (binários novos da árvore principal, master `7b0728a`):** os três presets compilam (o extrator e o jogo); SPIR-V
  45/45; unitários 379/0, render 107/0 (sob a camada Khronos, 0 erros), ui 25/0, remaster 5/0; ctest 30/30 e 23/23, com
  `real_disc`, `runtime_data`, `analysis_real` e `movie_manifest` aprovados (o `boot_trace` sem os snapshots do Flycast); a
  Original igual às 10 referências do N8.7; `n97_agua.py` W1–W6 sem falhas; uma Preview pelo Builder da árvore principal e
  `test_edicao` 11/0.

### 11.2 A causa da perda do dispositivo

- **Reprodutor controlado** (`tools/dev/n9_vulkan_perda.py`): o início até o menu inicial na janela (~40 s), o preset Alto
  (1440p interno), o mesmo exe; só muda o pós-processamento da apresentação: A sem cor e sem nitidez; B só a cor; C só a
  nitidez; D as duas. As configurações se alternam (A, B, C, D, A, …). RTX 5080, driver 621.01.

| Exe | A | B | C | D |
|---|---|---|---|---|
| validação `n9-estab` antes da correção, sem diagnóstico | 0/10 | 3/10 | 4/10 | 4/10 |
| o mesmo com `BS_VK_DIAG=1` | 0/5 | 0/5 | 0/5 | 0/5 |
| com a correção (`ab28c5a`), sem diagnóstico | 0/10 | 0/10 | 0/10 | 0/10 |

  As perdas, nos renders 677–687, aconteciam com qualquer passe de pós-processamento (11 em 30) e nunca sem ele (0 em 10).
  Com a probabilidade observada antes (~0,37 por execução com pós), 30 execuções seguidas sem perda teriam chance da ordem de
  10⁻⁶.
- **Diagnóstico** (`BS_VK_DIAG=1`, só o build de validação; `3ec3b81`): `VK_EXT_device_fault` e
  `VK_NV_device_diagnostic_checkpoints` existem no driver e foram habilitados, com marcadores por etapa e um registro
  circular. Com ele ligado a perda não aconteceu (0 em 20: os marcadores mudam o tempo, como a camada Khronos), então não há
  dados de fault do driver. O modo `BS_VK_FILA_UNICA=1` ficou disponível para comparar as filas.
- **O que o diagnóstico mostrou:** perto do título (quadros apresentados 1 377 a 1 387) a fonte da imagem alterna a cada quadro
  entre a imagem da CPU (640×476) e o slot (1 920×1 440), e o pós-processamento recria as três imagens intermediárias a cada
  troca (gerações 4 a 11). O driver reaproveita os valores de handle das vistas destruídas: a saída da geração 9 tem o mesmo
  handle da geração 5, e a geração 6 reusa vistas da geração 4.
- **Causa:** o cache de conjuntos de descritores da apresentação é chaveado por `(VkImageView, VkSampler)` e não era
  descartado quando o pós recriava as suas imagens (era descartado para a imagem da CPU, em `stage()`, e para o slot, pela
  versão). Uma vista nova com o handle de uma destruída recebia o conjunto escrito para a antiga: a GPU lia um descritor
  inválido e o dispositivo era perdido. É o mesmo defeito que a revisão Codex 20261028-030202 corrigiu para os slots.
- **Hipóteses descartadas:** a resolução interna (o Original em 1440p sem pós não perdia); a correção de cor em particular (a
  nitidez sozinha também perde); duas threads na mesma fila (a apresentação usa a segunda fila, com trava própria);
  `vkDeviceWaitIdle` fora das travas (só na destruição); o `post_run` (só nos testes); o shader de cor; a água (o binário do
  N9.6 perdia igual). A camada Khronos, inclusive a validação de sincronização, não acusou nada: ela muda o tempo e não
  enxerga a reutilização do handle.
- **Correção mínima** (`ab28c5a`, commit separado): `PostChain::generation()` (a geração das imagens intermediárias) e, na
  apresentação, `forgetSets()` quando ela muda, antes de qualquer quad do quadro (o quadro anterior terminou: a cerca). O shader
  fiel e os recursos do N9.5–N9.7 não mudam; a Original não passa pelo pós (sem efeitos).
- **Revisão Codex `20261112-163647`:** a correção está no lugar certo (o pool resetado é o da apresentação, depois da cerca;
  as outras fontes já invalidavam o cache). Três achados aceitos: o fault e os marcadores só com `VK_ERROR_DEVICE_LOST`; o
  diagnóstico fora do produto (`diag_` vira uma constante falsa sem `BS_VALIDACAO`); o reprodutor versionado e esta seção. Um
  teste determinístico do cache da apresentação exigiria uma janela (a apresentação precisa de uma superfície): fica a
  reprodução na janela.

### 11.3 A recuperação e os comandos

- O estado aplicado sobrevive à recriação do dispositivo (o AO continuou em 210 renders no dispositivo novo).
- **A perda de comandos é do controle virtual dos testes:** `VirtualPad::step()` roda no laço da janela e aplica de uma vez os
  eventos já vencidos; com a janela travada, o aperto e a soltura (100 ms depois) do mesmo botão caem no mesmo passo, e o SDL
  não vê mudança de estado. No jogo real, o teclado chega pela fila de mensagens do Windows e não se perde; um controle lido
  por estado (XInput) pode perder um toque mais curto que o travamento, como qualquer jogo nessa situação.
- **Proposta (não implementada):** o controle virtual aplicar no máximo uma mudança de estado por botão a cada ciclo do SDL.
  Mudaria a temporização de todas as suítes da janela e esconderia o efeito de um travamento; fica para decisão.
