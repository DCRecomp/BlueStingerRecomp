# N9.8: a bateria final (fase 8.5), plano para aprovação

**Situação:** plano proposto em 2026-11-14, no fechamento técnico da fase 8.4. **Nada desta lista roda antes da autorização do
mantenedor.** Formato e critérios seguem `docs/N9-6-TESTES-FINAIS.md`. Legenda: [P] produto (`msvc-release`), [V] validação
(`msvc-validacao`), [J] abre janela do jogo (avisar antes), [D] precisa do CDI do usuário (`ISO/Blue Stinger.cdi`).

Regras da execução: uma tarefa pesada por vez; tudo em `tmp/sessao-<data>/n985/`; nenhuma referência histórica reescrita; um
teste interrompido, pulado ou sem evidência não conta como aprovado; nenhum push, tag ou release.

## 1. Antes da bateria (≈ 40 min, sem janela)

| # | Passo | Critério |
|---|---|---|
| 1.1 | Os três presets compilados do mesmo commit (o da 8.4); exes conferidos pela data e por `git log -1` | iguais ao commit |
| 1.2 | `tools/dev/gera_spirv.ps1 -Conferir` | 53/53 |
| 1.3 | `bs_unit_tests`, `bs_render_tests` sob a camada Khronos (`VK_INSTANCE_LAYERS`: o terminal é elevado) padrão e de sincronização; a inserção da camada conferida uma vez com `VK_LOADER_DEBUG=layer` | 0 falhas, 0 mensagens |
| 1.4 | ctest debug e release sem o disco; ctest debug com `BS_DISC_IMAGE` [D] | 0 falhas; os pulados listados (o `boot_trace` precisa dos snapshots do Flycast, que não existem nesta máquina: não conta) |
| 1.5 | `bs_instalador_tests`, `bs_ui_tests`, `bs_remaster_tests`, `bs_video_tests` | 0 falhas |
| 1.6 | Hashes dos arquivos particulares (`known_versions.h`, `DISTRIBUICAO-COMPILADOR.md`, `_bs-extract.exe`) iguais aos de `tmp/sessao-20261113/n98/hashes-particulares-inicio-84.txt` | iguais |

## 2. Regressão histórica (as edições e o estado, ≈ 3 h)

| # | Teste | Critério |
|---|---|---|
| R1 | Original × `analysis/n8/referencias_n87`, os 10 casos (`n8_referencias.py conferir`, layout `tmp/sessao-20261109/layout-fase4`) [P] | 10 iguais, 0 diferentes |
| R2 | `n95_headless.py` H1–H7 [P][V] | 0 falhas |
| R3 | `tools/dist/test_remaster.ps1` R1–R4 [V] | 0 falhas |
| R4 | `n95_persistencia.py` P1–P5 [P][J] | 0 falhas |
| R5 | `tools/dist/test_edicao.ps1` [P] | 0 falhas |
| R6 | `n95_janela.py` J1–J7 [P][J] | 0 falhas |
| R7 | `n95_proveniencia.py` e `n95_bordas.py` (H6) [V] | 0 falhas |
| R8 | `n96_luz.py` nas três qualidades e `n96_cena.py` [P][V] | 0 falhas, 0 alarmes |
| R9 | `n97_agua.py` W1–W6 [P][V] | 0 falhas |
| R10 | `n9_vulkan_perda.py` (o reprodutor da perda intermitente do `ab28c5a`, com o pós e a máscara ligados) [V][J] | 0 perdas em 30 execuções |

## 3. O N9.8 (≈ 2 h 30)

| # | Teste | Critério |
|---|---|---|
| T1 | `n98_filtragem.py` F1–F4 [P][V] | 0 falhas |
| T2 | `n98_texturas.py` (o pacote sintético local, recuo, limites, a água) [P][V] | 0 falhas |
| T3 | `n98_aa.py` A1–A6 [P][V][J] | 0 falhas |
| T4 | `n98_pos.py` P1–P9 [P][V][J] | 0 falhas |
| T5 | `n96_presets.py` Q1–Q11 [P][J] | 0 falhas |
| T6 | Tudo ligado ao mesmo tempo (16:9 em 1440, anisotrópica, texturas HD sintéticas, área e bordas, AO alto, luz, sombras aprimoradas, água alta, cor, tonemapping, nitidez e bloom), sem janela e na janela [P][J] | estado e som iguais aos sem efeitos; sem erro do Vulkan; nenhuma perda |
| T7 | A Original com todos os efeitos de pós ligados: as opções da Remastered recusadas (saída 2), o pós como na v0.9.7-1 (0 slots com a máscara) [P][J] | como no critério |

## 4. Desempenho e memória (≈ 1 h 30, janela)

| # | Medida | Registro |
|---|---|---|
| C1 | `n95_desempenho.py --janela`, corredor e cais: RTX 5080 16:9@1440 e @2160; AMD integrada 4:3@720 e 16:9@1080; base × cada efeito do N9.8 × tudo [P][J] | fps, frametime p50/p99, GPU por etapa; diferenças dentro do ruído (RTX: ±0,8 ms medido) não contam |
| C2 | `post_cost_4k` nas duas GPUs | ms por passe |
| C3 | Memória: alvos, slots, pós, texturas HD (o pacote sintético nos fatores 2 e 4) | MiB por efeito |

## 5. Edição, instalador e Builder (≈ 1 h 30, [D])

| # | Teste | Critério |
|---|---|---|
| B1 | `tools/dist/test_builder.ps1 -Disc ISO\Blue Stinger.cdi` | 0 falhas |
| B2 | `tools/dist/test_instalador.ps1 -Disc …` (a integração desviada por `BS_INSTALADOR_TESTE_RAIZ`) | 0 falhas |
| B3 | `tools/dist/test_projeto.ps1 -Disc …` (o projeto editável e os mods; um pacote de texturas sintético em `mods`) | 0 falhas |
| B4 | Instalação nova da Remastered e atualização de um `graficos.toml` da v0.9.7-1 (os padrões aplicados uma vez; a decisão da seção 11.3 de `docs/N9-8-AUDITORIA.md` aplicada se aprovada) | como no critério |

## 6. Conferência de publicação (sem publicar, ≈ 15 min)

| # | Passo | Critério |
|---|---|---|
| X1 | `tools/dev/varre_publicacao.py` sobre a árvore e o diff desde `fbc6761` | nenhum arquivo particular, arte do jogo, `analysis/`, `reviews/`, `generated/` ou texto de diálogo |
| X2 | `docs/N9-L-LICENCAS.md` e `third_party/README.md` com o `stb_image` (8.2) e nada novo sem inventário | conferido |
| X3 | Revisão Codex do conjunto 8.1–8.4 (somente leitura) | achados tratados e registrados |

## 7. Recursos

- **Tempo total estimado:** 9 a 11 h de máquina, em sequência (as janelas não rodam juntas com nada pesado).
- **Memória:** as execuções sem janela usam ~1,5–2 GB de RAM cada; as suítes rodam até 3 em paralelo (≈ 6 GB). A VRAM com tudo
  ligado em 2160 é a maior medida (alvos + pós + texturas HD). Nenhum outro trabalho pesado junto (o N9.13, a geração de filmes,
  fica parado durante a bateria: ele já esgotou a RAM duas vezes).
- **Janelas:** R4, R6, R10, T3–T7, C1 (≈ 60 aberturas, uma por vez, ~1–2 min cada, fechadas pelo caminho normal ou só o
  processo do caso).
- **NVIDIA e AMD:** C1 e C2 nas duas; R10 na RTX (onde a perda intermitente apareceu); T6 também na AMD em 4:3@720.
- **CDI e Flycast:** o CDI só em 1.4 e na seção 5; o Flycast não é usado (o `boot_trace` fica pulado e registrado).

## 8. Aprovação

A fase 8.5 fecha com todos os itens das seções 1, 2, 3, 5 e 6 aprovados e os da seção 4 medidos e registrados. Ficam fora da
aprovação técnica, registrados: a conferência visual (mantenedor), o `boot_trace` (Flycast), a validação no Linux/Bazzite
(N9.12) e o C8 (gravações perdidas).

## 9. Limitações conhecidas que a bateria não resolve

- Translúcidas 3D sobre o vazio sem a nitidez, o bloom, o AO e as bordas (o bit da cena é do passe A).
- `kHudInvW` empírico (duas rotas medidas).
- Mipmaps sem alfa pré-multiplicado (8.1); o cache de texturas HD sem descarte por orçamento além do limite residente (8.2).
- A AMD integrada em 1440 abaixo do tempo real com os efeitos.
