# N8.8: fechamento técnico

**Situação em 2026-11-02: fechamento técnico do N8 concluído, com ressalvas. O N8 não está em 100 %.** A campanha completa
(D-N8-4), o H9 e a ressalva do critério 7.1 ficam abertos, cada um com a sua situação (seção 5). Nada aqui foi publicado:
push e release aguardam a aprovação específica do mantenedor.

## 1. Como a regressão nível 3 pós-corte foi comprovada

**A regressão nível 3 pós-corte foi comprovada por composição de execuções, e não por uma única bateria contínua.** O
mantenedor aprovou essa composição em 2026-11-02, depois da suspensão controlada da integral iniciada sobre o `47d476c`.
Não existe uma execução única, do começo ao fim, sobre um mesmo executável.

Cada linha da matriz (seção 3) diz se o resultado foi executado na versão final ou reaproveitado. Reaproveitar exige a
identidade das entradas ou a comparação dos executáveis (seção 2.2). Mudar o hash do commit não bastou para repetir; um
resultado histórico que falhou não foi aproveitado.

A suspensão: às 02:29:33 de 2026-11-02, com `test_ordem` já registrado, foi encerrada só a árvore da regressão, identificada
por PID e linha de comando. `test_tempo` não chegou a começar. O `known_versions.h` particular foi restaurado e o hash
conferido antes e depois de cada prova; logs, identidades e estado de retomada estão preservados.

## 2. Executáveis e equivalência

### 2.1 Identidades

Hashes SHA-256 (12 primeiros dígitos). `BS_VERSION` nos exes de `build\` só muda no configure e não prova procedência; a
procedência abaixo vem de hash registrado. Os exes marcados "guardado" estão em
`%LOCALAPPDATA%\BlueStingerRecomp\n8-8\fechamento\exes_referencia`, junto dos comparadores (`..\comparadores`).

| Papel | Data | Hash | Fonte e procedência |
|---|---|---|---|
| Produto | 30/10 | `4D747956B3D3` | `3f788e5`, fonte `3C55B686D9730863`; copiado por `test_saves`; guardado |
| Validação | 30/10 | `E283B63E526B` | `3f788e5`; hash no log de `test_n8_nativo`; **exe não preservado** |
| Mods | 30/10 | `9740E728B0E2` | `3f788e5`; hash no log e no `test_saves`; guardado |
| Watch | 30/10 | sem registro | `3f788e5`; **nem hash nem exe preservados** |
| Validação | 31/10 | `D06745225A7F` | `688935f`/`d0ee5b9`; `entradas_sha256.txt`; guardado |
| Mods | 31/10 | `62AB76151353` | `688935f`/`d0ee5b9`; `entradas_sha256.txt`; guardado |
| Preview | 31/10 | `5FEC88DC867C` | `688935f`; `entradas_sha256.txt` |
| Preview | 01/11 | `22A4AD9C6F7D` | `2349749` + árvore; `dist\BlueStinger-Preview.anterior` |
| Validação | 01/11 | `2C065900DDAC` | `2349749` + árvore; `*.entradas.txt`; **exe não preservado** |
| **Preview final** | 02/11 | `BD2E9C847AA3` | `47d476c`, fonte `00BFCBC8A2771BE1`, identidade `pe-e825af61` |
| Produto final | 02/11 | `B04D3502C88D` | `build\msvc-release`, mesma identidade da Preview final |
| **Validação final** | 02/11 | `028DE4CB6496` | `build\msvc-validacao`, identidade `pe-0e7b40ae` |
| Mods final | 02/11 | `6C323D79892E` | `build\msvc-release-mods`, identidade `pe-afac7a7f` |
| Watch final | 02/11 | `2BD888433653` | `build\msvc-release-watch`, hash em `provas_codex.entradas.txt` |
| `bs-builder` final | 02/11 | `070574C93742` | `dist\BlueStinger-Builder` |
| Recompiler final | 02/11 | `AEA76AB26B5B` | `dist\BlueStinger-Builder` |
| Developer final | 02/11 | `C9D143C04CD5` | `dist\BlueStinger-Builder` |

### 2.2 Comparações

Comparação função a função pela `.pdata`, nomes pelo `.map` do link. A tolerância é de até 4 bytes contíguos, para os
deslocamentos do linker. Limite conhecido: uma constante de 32 bits alterada passaria, e por isso o diff do fonte entra
junto. O detalhe da identidade ficou igual em todas as etapas de 02/11.

| Par | Funções | Sem par no atual | Outras seções |
|---|---|---|---|
| Preview 01/11 × final | 19.779 | só `main` (`fflush`) | SPIR-V 33 blobs iguais (446.040 bytes); `.rsrc` igual; textos diferem só em versão, data e a literal nova; 2.175 de 2.179 arquivos iguais |
| Produto 30/10 × final | 19.751 × 19.779 | `stall_trace` (novo), `audio_output`, `vk_device`, `vk_present::present`, `GameHost::vblank`, `main`, `texture_heap::touch` (1 byte) | SPIR-V iguais; `.rsrc` difere só por 9 palavras do deslocamento de VA |
| Validação 31/10 × final | | os mesmos, com `time_adapter` e sem `texture_heap` | SPIR-V iguais; `.rsrc` idem |
| Mods 30/10 × final | 20.123 × 20.151 | os mesmos, com `time_adapter` | |
| Mods 31/10 × final | 20.123 × 20.151 | os mesmos, com `time_adapter` e sem `texture_heap` | |

Leitura do fonte de cada resíduo (`3f788e5..47d476c`):

- `stall_trace`, e os trechos novos em `audio_output`, `vk_device`, `vk_present` e `GameHost::vblank`: diagnóstico
  `BS_TRAVAS`, atrás de um `enabled()` que fica desligado sem a variável;
- `time_adapter`: contador e aviso da mutação `lento`, dentro de `#if BS_VALIDACAO`;
- `main`: `fflush` da saída no encerramento (causa do `--version` sem saída);
- `texture_heap::touch`: 1 byte, com o fonte sem mudança de lógica;
- `688935f` é mecânico: arquivos movidos, namespace, `bs_kamui`, comentários e `using`;
- nenhum shader mudou desde o `3f788e5`.

**Limite:** as funções Vulkan alteradas não são exercidas no headless. A evidência delas é o diff do fonte e o H9 no exe
de `2349749` com `BS_TRAVAS` ligado. Com o diagnóstico desligado, as provas são as referências 16/16 nos dois conjuntos.
Elas usam o headless. Na versão final, a janela do `test_preview` abriu, rodou a 59,9 campos/s e fechou no renderizador padrão, mas o log dela não registra qual backend ficou ativo.

## 3. Matriz da regressão composta

Legenda da coluna **Origem**: **Final** = executado na versão final (`47d476c`). **Reaproveitado** = resultado anterior
aceito pela equivalência da seção 2.2. Entradas comuns, iguais em todas as execuções listadas: `disc.manifest` `24FA3CB0`,
exceções de SPC `6FF3660D`, `n8_referencias.py` `bd08d74e`, roteiros e referências. "Mudanças" remete aos resíduos da
seção 2.2.

### 3.1 Executados na versão final

| Teste | Resultado e data | Executável | Entradas | Mudanças depois | Origem |
|---|---|---|---|---|---|
| builds, unitários | 349 aprovados, 02/11 00:17 | build final | fonte `00BFCBC8A2771BE1` | nenhuma | Final |
| `ctest` | 27 aprovados, 4 ignorados (`boot_trace`, `real_disc`, `runtime_data`, `analysis_real`), 02/11 00:18 | build final | idem | nenhuma | Final |
| `ctest_disco` | aprovado, `boot_trace` ignorado, 02/11 00:19 | build final | idem + disco | nenhuma | Final |
| `make_builder`, `make_preview` | aprovados, 02/11 00:20 | `bs-builder` `070574C9`; Preview `BD2E9C84` | idem | nenhuma | Final |
| `test_preview` | 12/0, 02/11 00:21 | Preview `BD2E9C84` | identidade `pe-e825af61` | nenhuma | Final |
| `referencias` | 16/16, 0 diferentes, 179 exceções, 02/11 00:56 | Preview `BD2E9C84` | comuns | nenhuma | Final |
| `referencias_n87` | 16/16, 02/11 01:44 | Preview `BD2E9C84` | comuns | nenhuma | Final |
| `test_gameplay` | 4/0, 02/11 01:59 | Preview `BD2E9C84` | script `604B69A` | nenhuma | Final |
| `test_builder` | 23/0, 02/11 02:09 | Builder final | script `19D38E4` | nenhuma | Final |
| `test_projeto` | 18/0, 02/11 02:17 | Builder final | script `334E6D2` | nenhuma | Final |
| `test_arquivos` | 6/0, 02/11 02:27 | validação `028DE4CB` | script `AA2D792` | nenhuma | Final |
| `test_ordem` | 8/0, 02/11 02:29 | validação `028DE4CB` | script `BBAEE84` | nenhuma | Final |
| `n6_h4` | 17/0, 02/11 03:13 (2.574 s) | validação `028DE4CB`; dados da Preview final | `n6_h.ps1` `D37B3F89` | nenhuma | Final |
| `test_efeito` | 17/0, 02/11 04:41 (659 s); negativo E7 da store queue não exercitado nesta rota (SKIP) | Watch `2BD88843` | script `9639DF4` | nenhuma | Final |
| Monitor C4, C5, C6 e C10 | 15/0, 02/11 04:47 (307 s) | Preview `BD2E9C84` | `campanha.py` `06E97CB9`, `test_campanha.py` `2DE4F21A` | nenhuma | Final |
| Monitor C1, C2, C3, C8 e C9 | 25/0, 02/11 06:47 (7.086 s) | Preview `BD2E9C84`; `.anterior` `22A4AD9C` no C8 | `campanha.py` `06E97CB9`, `test_campanha.py` `2DE4F21A` | nenhuma | Final |
| Monitor C7 | 7/0, 02/11 06:48 (offline) | sem exe | `campanha.py` `06E97CB9`, `test_campanha.py` `2DE4F21A` | nenhuma | Final |

O `n6_h4` aprova pelo critério atual: LOAD com 4.475 passos comuns iguais; heliporto igual até o passo 2.513, o 1º fim
natural de fala, com a 1ª diferença no passo 2.514; key-on da música 3.129 de 3.143 contra o Flycast, 0 inversões. Esse
critério vem do `e9edd66` (decisão do mantenedor, 2026-11-01): o nativo pode ter até 10 passos a menos no fim, porque o
download da música espera o DMA. Ele não afrouxa a igualdade dos passos comuns.

O C1 usa uma campanha de teste e declara que **não vale para a D-N8-4**.

A primeira rodada do Monitor (02/11 04:08) aprovou C1, C4, C5, C6 e C7 antes da correção do selo: 21/0. Ela fica como
histórico: as linhas acima repetem todos os casos com o `campanha.py` corrigido, 47/0 no total. O `test_efeito` foi repetido porque o exe da execução de 30/10 não tinha hash, a
pedido da revisão Codex; o resultado de 30/10 (17/0) fica como histórico.

### 3.2 Reaproveitados

| Teste | Resultado e data | Executável | Entradas | Mudanças depois | Evidência de equivalência | Origem |
|---|---|---|---|---|---|---|
| `test_n2b` | 52/0, 01/11 18:43 (B9: 3.000 VBlanks, média 2,000 ms; lento − sombra média 2,11 ms; positivo 22,91 × 23,08 ms) | validação `2C065900` | script `951077EA`, igual ao final | `bffbeb5` (`main`) | diff do fonte e Preview 01/11 × final; o exe de validação não foi comparado por não estar preservado | Reaproveitado |
| `test_n8_nativo` | 26/0, 31/10 16:20; também 26/0 em 30/10 | validação `D0674522`, mods `62AB7615` | script `98CE3D8` | resíduos | Validação e Mods 31/10 × final | Reaproveitado |
| `rota_combate`, `rota_newgame` | rc=0, 31/10 15:57, sombras em 0 | validação `D0674522` | roteiros iguais | resíduos | Validação 31/10 × final | Reaproveitado |
| `ctest` em validação e release | rc=0, 31/10 03:10 | builds de 31/10 | | resíduos | Comparações de 31/10 | Reaproveitado |
| `n6_h` (H2, H3, H5–H8) | 0 falhas, 01/11 00:52 (22.008 s) | validação `D0674522`, mods `62AB7615` | `n6_h.ps1` `28139643`; o `D37B3F89` mudou só o H4 | resíduos | Validação e Mods 31/10 × final | Reaproveitado |
| `test_saves` | 25/0, 31/10 (dentro do H8); 25/0, 30/10 | validação `D0674522`, produto `4D747956` | | resíduos | Validação 31/10 × final, Produto 30/10 × final | Reaproveitado |
| `n5_e001` | 10/0, 31/10 (dentro do n6_h); 10/0, 30/10 | validação `D0674522` | | resíduos | Validação 31/10 × final | Reaproveitado |
| `test_pd_nativo` | 86/0, 30/10; P1 e P2 31/0, 31/10 | `E283B63E`, `9740E728`; `D0674522` | | resíduos | Mods 30/10 × final, Validação 31/10 × final | Reaproveitado |
| `test_tempo` | 35/0, 30/10 15:11 | validação `E283B63E` | | resíduos, inclusive `time_adapter` só de validação | Mods 30/10 × final (mesma configuração de validação) e diff do fonte | Reaproveitado |
| `test_cri` | 33/0, 30/10 17:15 | mods `9740E728` | | resíduos | Mods 30/10 × final | Reaproveitado |
| `test_n2b2` | 17/0, 30/10 17:58 | `E283B63E`, `9740E728` | | resíduos | Mods 30/10 × final | Reaproveitado |
| `test_assistido` | 28/0, 30/10 18:28 | validação `E283B63E` | | resíduos | Mods 30/10 × final e diff do fonte | Reaproveitado |
| `test_controles` | 39/0, 30/10 19:31 | `E283B63E`, `9740E728` | | resíduos | Mods 30/10 × final | Reaproveitado |

### 3.3 Sem aprovação

| Teste | Resultado | Situação |
|---|---|---|
| `n6_h9` | 30/10 rc=-1073741502 (0xC0000142); 31/10 rc=1 (1 falta, fila em 199 ms) | **Não aprovado.** Não foi repetido e não foi reaproveitado |
| Campanha completa | nunca executada | **Não aprovada.** D-N8-4 adiada ao gate pós-N9 |

### 3.4 Resultados históricos que falharam

Ficam registrados como aconteceram, nenhum foi convertido em aprovação. Todos, salvo o H9, têm prova posterior aprovada, o
que não altera o resultado daquela execução.

| Execução | Resultado | Motivo | Prova posterior |
|---|---|---|---|
| 30/10 `ctest`, `ctest_disco` | rc=8 | catraca `n0_native_limpo` | 02/11, final |
| 30/10 `test_builder` | rc=1, 3 falhas | fixture do caso 8 | 02/11, final |
| 30/10 `test_n2b` | 2 falhas de 52 (B3, B9) | negativos do roteiro de teste | 01/11, 52/0 |
| 30/10 `n6_h` | rc=124 | limite do executor (seção 5.4) | 01/11, 0 falhas |
| 30/10 e 31/10 `n6_h4` | rc=1, 3 falhas | critério desatualizado desde o `e13a123` | 02/11, final |
| 30/10 e 31/10 `n6_h9` | ver 3.3 | H9 | **nenhuma** |
| 31/10 `referencias`, `referencias_n87` | 2 interrupções por memória | proteção de memória; não contam | 31/10 e 02/11, 16/16 |
| 01/11 `test_builder` | 21/22 e 20/22 | erro corrigido no mesmo dia | 01/11, 23/0 ×3; 02/11, 23/0 |

## 4. Critérios da seção 7.1 do plano

| Critério | Situação |
|---|---|
| D-N8-1 e plano-mestre | atendido |
| N2c | atendido |
| Providers nativos, sem opção legado necessária | atendido |
| Nenhum GD-ROM/BIOS/Maple/AICA/PVR/Holly/TA/VRAM/SQ alcançável | **ressalva, seção 5.3** |
| Vulkan e software sobre a IR | atendido |
| Controles, câmeras, menus, saves, filmes, áudio, legendas e pausa | atendido nas suítes; H9 aberto, **seção 5.1** |
| Tempo e gravações antigas | atendido |
| Determinismo e saves nas rotas | atendido |
| Campanha completa reproduzível | **não aprovada**, transferida ao gate pós-N9 |
| Builder/Recompiler, Preview e projeto editável; CDI só na conversão | atendido |
| Mapa, imports, strings, hwscan e instrumentação | **atendido com ressalva, seção 6.1**: mapa, imports e strings concordam; o hwscan mede o código guest (seção 5.3) e os rótulos de provedor estão parados desde o N2a |
| Regressões nível 3 e negativos | atendido por composição (seção 1); negativo E7 do `test_efeito` não exercitado na rota |
| Revisão independente | atendido, seção 7 |
| Núcleo sem fork | atendido |

## 5. Pendências

### 5.1 H9

Falha intermitente **preexistente, não corrigida e não aprovada**. Investigação suspensa. Interferência externa é hipótese
não comprovada, e suspender não é aprovar.

### 5.2 D-N8-4

Campanha completa **obrigatória, adiada por decisão do mantenedor** à validação final depois do N9 (`docs/N9-PLANO.md`
seção 14). Uma gravação valida Original e Remastered. O Monitor fica pronto e testado. Sem ela o N8 não chega a 100 %.

### 5.3 Critério 7.1 e D-K1(a)

O critério foi atendido quanto a eliminar a simulação do hardware do produto. A **redação literal não foi cumprida**: pela
D-K1(a), o Kamui recompilado continua escrevendo nos endereços do PVR, do TA e da VRAM, e serviços nativos atendem essas
escritas. `sh4_sq.cpp` (store queues da CPU) e `pvr_texture.cpp` (leitura do TCW e das texturas) seguem no produto. Tirar
essa fronteira exige mudar o AOT, o que fica fora do N8.

### 5.4 Defeito do executor: limite de `n6_h`

`tools/dev/regressao.ps1` limita a etapa `n6_h` a 3 h (`Max = 10800`), e ela leva 6,1 h (22.008 s). Uma regressão integral
reprova por rc=124 mesmo sem falha. **Corrigir antes de uma futura regressão integral**, ajustando o limite do executor sem
reduzir casos nem enfraquecer os testes. Nesta rodada o defeito não foi corrigido.

### 5.5 Outras

- N7 segue em 95 %: falta o quadro de fog pareado e o encerramento formal pelo mantenedor.
- Uma futura integral não deve repetir o que a identidade cobre; essa coluna só existe a partir de `84f7b63`.
- `config/fronteiras_us.toml` não acompanhou a migração: 262 entradas ainda rotuladas "legado" e 5 "nativo", sem mudança desde
  o N2a (2026-10-15). É metadado da fronteira, sem efeito no produto. Não foi alterado neste fechamento.

## 6. Auditoria, Builder, Preview e rollbacks

Conferido em 2026-11-02, depois das provas.

- **Commits:** `varre_publicacao.py` sobre `origin/master..master`, 0 achados em 12 commits. Nenhum commit do intervalo toca
  `known_versions.h`, `docs/DISTRIBUICAO-COMPILADOR.md`, `_bs-extract.exe` nem `tools/extractor/build/`.
- **Mídia rastreada:** nenhuma imagem de jogo, BIOS, áudio, vídeo ou save. O único binário de mídia é
  `app/res/bluestinger.ico`, o ícone original do projeto.
- **Ignorados:** `dist`, `ISO`, `game-data`, `generated`, `analysis`, `reviews`, `icone-local`.
- **Particulares preservados:** `known_versions.h` com o hash particular confirmado; `ISO`, saves em Documentos,
  `mods-padrao` (legendas-en e legendas-pt-br), stash e worktree intactos.
- **Builder:** 2.071 arquivos, 38,9 MiB, sem imagem, mídia ou dado do jogo. O `known_versions.h` do kit é a versão pública do
  Git. O hash particular não aparece em `bs-builder`, Recompiler nem Developer.
- **Preview final:** `--version` e `--verify-data` com rc=0, `test_preview` 12/0. É distribuição local e não deve ser
  publicada.

### 6.1 Mapa, imports, strings e hwscan na versão final

- **Mapa:** `n8_produto_papeis` aprovado no `ctest` de 02/11 (manifesto completo × mapa do linker).
- **Imports do produto:** só sistema e runtime (Media Foundation, Direct2D/DirectWrite, XInput, Win32, setup, UCRT e MSVC).
- **Strings do produto:** 0 ocorrências de `aica_`, `arm7`, `pvr_render_ta`, `holly_intc`, `sh4_dmac`, `ta_yuv` e
  `bs_runtime_hle`. Aparecem só mensagens de recusa (`--audio legado`, `--video legado`, `--flash`, `BS_RENDER_IR`, modelo
  HLE), rótulos de contadores (`maple_dmas`, `gdrom`) e nomes de controles de terceiros (Mayflash).
- **hwscan** (`bs-hwscan --gerado`, saída em `n8-8echamento\hwscan`): rc=0, catraca sem piora, 4.083 funções, 0 referências
  de hardware no código do jogo, 625 no SDK (235 acessos e 390 constantes), 0 violações de fechamento. Ele mede o **código
  guest**, que continua igual pela D-N8-1 e pela D-K1(a): não prova sozinho a saída do legado, e os rótulos de provedor estão
  defasados (seção 5.5). A prova da retirada são o mapa, as strings e `test_n8_nativo`.

### 6.2 Rollbacks

**Rollbacks** (`--version` e `--verify-data` só leitura, rc=0, 2.112 arquivos de dados, manifesto `24FA3CB0`):

| Pasta em `dist\` | Exe | Origem |
|---|---|---|
| `BlueStinger-Preview.anterior` | `22A4AD9C6F7D` | `2349749` |
| `.n8-6-20261027` | `04E7B7F34B7C` | N8.6 |
| `.n8-5-ced7570c` | `CED7570C9DDC` | N8.5 |
| `.n8-4-ce4a02eb` | `CE4A02EBEBA4` | N8.4 |
| `.n8-3-5d8bd6fe` | `5D8BD6FE2A07` | N8.3 |
| `.n8-2-cc370d56` | `CC370D56E701` | N8.2 |
| `.n8-1-9ecdeb7` | `93BD598DEA5A` | N8.1 |
| `.n8-0-5ce94b5` | `368A850188A1` | N8.0 |

Nos exes de N8.3 e N8.4 o `--version` sai com rc=0 sem texto. É o defeito antigo que o `bffbeb5` corrigiu, e eles não
foram alterados. Não há instalação completa do N8.7 em `dist`; o exe do produto de 30/10 está guardado (seção 2.1). Builders
anteriores: `.n8-0-5ce94b5` e `.old-ed6d6dfc`, com conteúdo local do jogo, nunca publicar.

## 7. Revisão Codex

Revisão `20261102-041748`, sobre as correções depois da `20261101-230720` e estes documentos. Cada achado foi conferido.

| Achado | Decisão | Ação |
|---|---|---|
| [ALTA] etapa aprovada era pulada sem conferir a identidade quando a chave ficava igual | **Aceito** | `regressao.ps1` calcula a identidade antes de pular; casos R43a e R43b. Não afeta esta matriz, que não usou esse salto para aprovar |
| [ALTA] campanha só com controle virtual virava real editando dois campos do manifesto | **Aceito** | roteiro copiado e selado na sessão; a guarda decide pelo arquivo ou pelo selo; caso C10. Limite: o selo fica no manifesto, e refazer os dois de propósito não é detectado |
| [MÉDIA] `test_efeito` reaproveitado sem hash do exe | **Aceito** | executado no Watch final, 17/0 |
| [MÉDIA] revisão independente marcada como atendida antes de existir | **Aceito** | critério separado e seção preenchida |
| [MÉDIA] README afirmava áudio sem faltas | **Aceito** | frase corrigida com o H9 |
| [BAIXA] `test_campanha` saía com rc=0 sem as bases privadas | **Aceito** | C7 e C10 rodam sem elas; caso pedido sem base reprova |

Provas das correções: `test_regressao` 0 falhas; `test_exe_identidade` 9/9; Monitor C10, C4, C5 e C6 15/0.

Revisão `20261102-044801`, sobre essas correções.

| Achado | Decisão | Ação |
|---|---|---|
| [MÉDIA] C1, C2, C3, C8 e C9 continuavam aprovados com o `campanha.py` anterior, e a justificativa "o diff toca só sessões com roteiro de teste" não provava equivalência: esses casos são justamente essas sessões | **Aceito** | justificativa retirada; os cinco casos repetidos com o `campanha.py` corrigido, 25/0, e o C7 também, 7/0 |

Essa revisão não apontou problema nas demais correções. Ela não executou testes nem leu as evidências locais. A repetição
do Monitor é posterior a ela e não muda código.

## 8. Evidências locais

Fora do Git, em `%LOCALAPPDATA%\BlueStingerRecomp`:

- `n8-8\fechamento`: logs de 02/11, `provas_dirigidas.*`, `estado_suspensao.tsv`, identidades, exes e comparadores;
- `n8-8\provas`: 31/10 a 01/11;
- `n8-8\incremental`, `builder_incremental`, `b9_isolado`: 01/11;
- `regressao`: estado do executor e logs da suspensão.

Revisão Codex em `reviews/`, também fora do Git.

Se um exe `build\` for recompilado, esta matriz vale para os hashes listados, não para o novo binário.
