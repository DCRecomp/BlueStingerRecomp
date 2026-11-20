# Versão PAL: plano (2026-11-19, pedido do mantenedor)

Decisão D4 do `docs/IMAGENS-DISCO-PLANO.md`: o PAL vem depois da v0.9.7-3, já publicada. A issue #4 cita um bug do PAL em
480p/VGA: o jogo dessincroniza e trava na primeira cena com a Janine. Um ex-speedrunner se ofereceu para testar.

## 1. O que foi medido (só leitura, `tmp/discos/`)

A imagem do mantenedor é `Blue Stinger v1.000 (1999)(Activision)(PAL)(DE)[!]`, um GDI da TOSEC. O IP.BIN diz
`T13001D 18`, `V1.000`, de 03/08/1999, área `E`. A americana é de 05/07/1999.

- **O executável é o mesmo jogo, deslocado:**
  - o `1ST_READ.BIN` tem 1 979 556 B, 4 000 a mais que o americano;
  - das 4 080 funções do americano, 3 475 aparecem no PAL com o mesmo início, em 16 faixas de deslocamento constante:
    0, +32, −32, −64, +64, +96, +128, +256, +384 e +4000 na biblioteca do SDK, a partir de `8C157F6C`;
  - as 605 que não aparecem por esse critério são funções com início repetido ou com mudanças pequenas. Ficam para o
    passo P1;
  - os corpos idênticos byte a byte são só 1 570, porque citam endereços absolutos que se deslocaram: a mesma lógica, com
    outros literais;
  - **conclusão:** é essencialmente o mesmo código relocado, e não um jogo diferente. Mapa em `tmp/discos/pal_mapa.tsv`.
- **O que depende de endereço fora do C++ gerado:**
  - 511 endereços distintos de código e dados do executável:
    - `config/fronteiras_us.toml`: 267;
    - `config/bluestinger_us.toml`: 107;
    - o código nativo: cerca de 140, em `kamui/hud_table.h`, `transitional/*_adapter`, `app/game_host.cpp`,
      `system/system_area.h`, `boot/native_boot.cpp`, `free_camera` e outros;
    - `boot/bios_entry_state.inc`: 62, a maioria da área do sistema (`8C000000`–`8C00FFFF`), que não muda;
  - 36 endereços de variáveis em RAM (`8C2…`–`8CF…`).
- **Dados do disco:** 176 dos 2 092 arquivos diferem, e 137 deles têm outro tamanho:
  - 126 `.BIN`, salas como `RCG`/`RCS`/`RDE`;
  - 34 `.NT`, texturas: `EMD00*` e `OST*` devem ser textos e menus em alemão, e `PORTABLE.NT`;
  - os 16 filmes.
- **Filmes:**
  - vídeo a **25 fps** (o americano tem 30) e um ADX com o dobro da duração do filme (corrigido no P5, seção 10: a
    primeira medida dizia 50 fps e duas trilhas);
  - o mesmo preenchimento de áudio do disco original americano;
  - os mods widescreen e as legendas, feitos para os filmes de 30 fps, não servem como estão.
- **Modo de vídeo:** o executável não tem textos "50Hz"/"60Hz"/"PAL" (os menus devem ser texturas). Falta descobrir se o
  jogo oferece 60 Hz e como escolhe o modo; ver P4.

## 2. Arquitetura proposta (decisão do mantenedor)

**A (recomendada), uma versão a mais sobre o mesmo núcleo:**
- `config/bluestinger_pal_de.toml` e `config/fronteiras_pal_de.toml`, gerados do americano pelo mapa de endereços e
  revisados;
- uma **tabela de endereços por versão** no runtime, escolhida pelo `versionId` no boot: o código nativo passa a ler o
  endereço da tabela, e não um literal;
- o Builder identifica a versão (`boot_identity`) e recompila com a configuração dela.

Cumpre a regra de "um projeto, um núcleo, sem fork", e prepara outras versões (outros PAL, a japonesa).

**B, um segundo projeto ou branch do PAL:** recusada pelas regras do projeto ("nada de fork, branch por edição").

## 3. Etapas

| # | Etapa | Prova |
|---|---|---|
| P0 | Decisões D-P1 a D-P4 e as imagens (dumps) dos discos PAL do mantenedor | — |
| P1 | **Mapa de endereços** (`tools/dev/versao_mapa.py`): funções pelo corpo normalizado (literais mascarados, como no I7), dados pela comparação da região `8C0A0000`–`8C14FFFF` e da BSS; os 547 endereços traduzidos, cada um conferido pelo disassembly | relatório 100 % mapeado; os ambíguos revisados à mão |
| P2 | **Tabela de endereços no runtime**: troca dos literais por `addr::X` da versão ativa. Para o americano, nada muda | **prova de equivalência do americano:** C8, referências 16/16, `test_gameplay`, `test_assistido` e o traço por passo idêntico ao de antes; revisão Codex |
| P3 | **Configuração e recompilação do PAL**: `bs-recomp` com 0 órfãs e 0 avisos; o C++ compila | relatório da análise; build |
| P4 | **Boot e vídeo PAL:** o estado de entrada do BIOS para a área `E` (registradores do PVR e do SPG a 50 Hz), a escolha 50/60 Hz do jogo e a cadência do VBlank no runtime nativo | boot sem janela até o título; quadros e tempo por passo coerentes com 50 Hz |
| P5 | **Filmes PAL:** conversão a 50 fps (o Builder já lê a taxa do SFD), a 2ª trilha de áudio e o corte do preenchimento | `movie_manifest`; o fim do filme no oráculo |
| P6 | **Idioma e saves:** textos alemães (texturas, nada a fazer além de carregar), nome e região dos saves do VMU, as legendas pt-BR/en (tempos diferentes?) | saves criados e carregados; legendas conferidas |
| P7 | **Validação:** o oráculo do Flycast com o PAL (resolver a entrada do jogo com o IP.BIN da Sega, o mesmo problema do GD), referências próprias, a rota NEW GAME → área 1, o caso da issue #4 (480p e a cena da Janine) e o teste do speedrunner | suíte própria; a cena da #4 |
| P8 | **Builder e release:** a versão PAL reconhecida, os textos e o pacote | `test_builder` com o PAL |

Ordem: P0 → P1 → P2 (a parte mais delicada: mexe no americano e exige a prova de equivalência) → P3 → P4 → P5 → P6 → P7
→ P8.

## 4. Decisões do mantenedor

**Respondidas em 2026-11-19:**
- **D-P1:** só a versão alemã, por enquanto.
- **D-P2 e D-P3:** primeiro a Original PAL a 50 Hz. A Remastered e um possível 60 Hz ficam para depois.
- **D-P4:** os filmes widescreen não aparecem no PAL por ora.
- **A arquitetura (seção 2)** espera a aprovação. A recomendação é a A.

- **D-P1, quais discos PAL.** Houve várias versões regionais (alemã, inglesa, francesa…), cada uma com outro executável
  provável. Só a alemã (`T13001D 18`) foi medida. Recomendo começar pela que você tiver em dump do próprio disco e medir as
  outras com o mesmo mapa (P1).
- **D-P2, 50 Hz ou 60 Hz.** O PAL roda a 50 Hz no console, e alguns jogos PAL oferecem 60 Hz. Recomendo reproduzir o
  original (50 Hz e o que o jogo oferecer) e deixar o 60 Hz para a Remastered, se o jogo permitir sem mudar a lógica.
- **D-P3, a Remastered no PAL.** Os recursos da Remastered (16:9, câmera livre, HUD) dependem de endereços (P2). Recomendo
  primeiro a Original PAL e depois a Remastered pelo mesmo mapa.
- **D-P4, os filmes widescreen no PAL.** Os mods foram feitos dos filmes de 30 fps. Recomendo que, no PAL, o instalador
  não ofereça o mod até haver uma versão de 50 fps (ou uma conversão).

## 5. Riscos

- **P2 mexe no americano:** a troca dos literais pela tabela precisa ser provada idêntica, pela bateria de nível 2 e pelos
  traços.
- **As 605 funções não mapeadas:** algumas podem ter lógica diferente de verdade, por exemplo o tempo a 50 Hz e o bug da #4.
  O P1 separa o que é relocação do que é código novo.
- **O oráculo com o IP.BIN da Sega:** sem ele, a validação do PAL fica sem referência independente. Resolver isso também
  fecha o pendente do fim dos filmes do GD.
- **Esforço:** é a maior etapa desde o N8. O P1 e o P3 levam dias; o P2, com a prova, mais; o P4 a P7 dependem do que o
  P1 revelar.

## 6. P1, primeira passada (2026-11-19)

Ferramentas: `tools/dev/versao_mapa.py` e `tools/dev/versao_traduz.py`, mais o casamento tolerante em
`tmp/discos/pal_tolerante.py`. São só leitura, e a saída fica em `tmp/discos/mapa-pal-de/`.

- **Funções:** 4 036 das 4 080 casam pelo corpo normalizado (literais de endereço e bra/bsr mascarados).
  - O casamento é único, sem ambíguas e sem inversão de ordem, em 14 faixas de deslocamento.
- **Verificação cruzada:**
  - literais de código: 9 394 coerentes, 0 divergentes;
  - bra/bsr: 12 882 coerentes, 1 divergente (`8C04A362`, que salta para a região alterada `8C0498C0`).
- **Dados:** 2 850 endereços citados pelo código mapeado, nenhum conflito, 51 faixas.
  - A RAM e a BSS de `8C1DA220` a `8C32BBC0` estão todas a +4000.
- **Endereços fixos fora do C++ gerado:** 545 distintos. 538 foram traduzidos automaticamente:
  - sistema: 79;
  - funções: 363;
  - interior de função: 44;
  - dados: 42;
  - faixa interpolada: 10.

  Os outros 7: 5 foram resolvidos pelo casamento tolerante, dentro de blocos idênticos de 33 a 870 instruções, e 2
  são os limites `8CDE0000`/`8CFFFFFF` do boot nativo, ainda a conferir.
- **Código realmente diferente:** 23 regiões, quase todas pequenas.
  - As de menor semelhança, onde provavelmente estão o tempo a 50 Hz, o modo de vídeo e o menu de idioma:
    - `8C07D1E0` (0,42);
    - `8C08AEDC` (0,57);
    - `8C02DBC0` (0,66);
    - `8C0498C0` (0,71);
    - `8C082B86` (0,72);
    - `8C07C1DC` (0,72).
  - `8C0788A0`–`8C07ABE0` é o título e as opções (0,89–0,92).
  - `8C15AE64` e `8C15B0F4` são as FADs do GD (0,99).

**Conferências (2026-11-19, mesmo dia):**
- **Conteúdo dos 545 endereços** (`versao_traduz.py`, coluna `conteudo`):
  - 423 têm a mesma instrução ou o mesmo dado nas duas versões (literal de endereço mascarado), e nenhum diverge;
  - os outros 122 não têm conteúdo a comparar: 79 de sistema, BSS/RAM fora do executável e os 7 pendentes.
  - **Os 79 endereços de sistema:** o próprio PAL cita 10 endereços de `8C00007E`–`8C00FFF4` com deslocamento 0, e
    nenhum muda. O estado deixado pelo BIOS da área `E` é do P4.
  - **A RAM e a BSS:** um deslocamento só (+4000) de `8C1DA220` a `8C32BBC0`, sem conflito. Por isso as faixas
    interpoladas ali (`8C240000`–`8C25FFFF`, `8C24D680`, `8C2DA97C`) são confiáveis.
- **Diff do disassembly das 22 regiões** (`tmp/discos/pal_regioes_diff.py`, `mapa-pal-de/regioes/*.diff`) e das
  constantes novas (`pal_constantes.py`):
  - **Compensação de 50 Hz:** o PAL multiplica valores por 0,8333… (5/6) em `8C02E0C0`, `8C035E5A`, `8C037126` e
    `8C0498C0`, e por 1,2 (6/5) em `8C0389C0`. Em `8C0498C0`, por exemplo, um byte vira float, vai a ×5/6 e volta a
    inteiro. É lógica de jogo realmente diferente, e o C++ do PAL a recompila como ela é. Não há o que traduzir.
  - **`8C08AF3C`, função nova** (98 → 242 instruções), com uma seleção por casos 0–5: provavelmente o idioma ou o
    layout por idioma. Fica para o P6.
  - `8C0788A0`–`8C07ABE0`, título e opções: 1,1× e posições diferentes, provavelmente o layout dos textos alemães.
  - `8C15AE64` e `8C15B0F4`: só as FADs do GD.
- **A região de dados do executável** (`8C09BA04`–`8C157F60`, 771 420 B; `pal_dados_conteudo.py`): entre 982
  âncoras:
  - 707 372 B (92 %) iguais byte a byte;
  - 3 040 B diferentes, que são textos em alemão (`LADE`, `KEINE DATEN`, `Passcode eingeben`), tabelas de caracteres
    e a fonte com os acentos;
  - 60 980 B entre âncoras de deslocamentos diferentes: os pontos onde os textos mudam de tamanho.

**Conclusão do P1:**
- O endereçamento do PAL alemão está mapeado. Todo endereço fixo do nosso código tem tradução conferida, ou cai em
  BSS de deslocamento único.
- As diferenças reais são:
  - a compensação de 50 Hz em 5 funções, já recompilada como é;
  - uma função nova de idioma;
  - os textos alemães.
- Nada disso pede tradução de endereço.

**Revisão Codex (`reviews/codex-20261119-132421.md`), 4 achados, todos aceitos e corrigidos:**
1. **MÉDIA, unicidade:** o casamento aceitava o deslocamento previsto sem procurar outros candidatos.
   - Agora as duas imagens são normalizadas uma vez e todos os candidatos de -512 a +4096 são enumerados.
   - Uma função ambígua só é aceita por um destes critérios:
     - (a) é o único candidato coerente: os alvos de bra/bsr e os literais conferem pelo mapa, com pelo menos um
       conferido e nenhum divergente;
     - (b) as referências que chegam a ela pelo código mapeado concordam;
     - (c) fica entre duas vizinhas de mesmo deslocamento, que é candidato de todas.
   - Resultado: 127 ambíguas, das quais 103 resolvidas por (a)/(b) e 44 por (c). Sobra 1 sem casamento, `8C04FAD0`
     (2 bytes, colada à região alterada `8C04E440`).
   - O mapa ficou com 4 035 funções e as mesmas contagens da verificação cruzada: literais 9 394/0; bra/bsr 12 882/1.
2. **MÉDIA, interpolação sem limite:** a classe `faixa` agora exige âncoras a no máximo 4 KB uma da outra.
   - Os limites `8C240000`/`8C25FFFF`, a região do hash de diagnóstico, viraram exceções com evidência.
3. **MÉDIA, as 5 traduções do casamento tolerante:** passaram ao `mapa-pal-de/excecoes.tsv`, que o `versao_traduz.py`
   lê.
   - Cada uma vem de um bloco igual que é único em ±16 KB do PAL, conferido por `tmp/discos/pal_tolerante_unico.py`.
   - Blocos de 66 B a 1 740 B.
4. **BAIXA, cabeçalho do TSV:** corrigido e regenerado.

**Lacuna achada durante a correção:** o código nativo também cita a RAM por deslocamento (`ram_ + 0x24CCB8`,
`ram_[0x240000]`), e a varredura só pegava a forma `0x8C……`.
- Agora a varredura pega:
  - deslocamentos numa linha que indexa a RAM;
  - deslocamentos soltos na faixa da BSS que não sejam números redondos.
- Ficam de fora as máscaras e as cores do Builder; a cor `kBorda` está listada como não endereço.
- Isso trouxe 6 endereços a mais, todos do `game_host.cpp`: largura e centro da tela da Remastered, câmera, vida e
  posição do Eliot. Todos na BSS a +4000.

**Inventário final:** 552 endereços distintos.

| Classe | Endereços |
|---|---|
| sistema | 79 |
| função | 363 |
| interior | 44 |
| dado | 44 |
| faixa (≤ 4 KB) | 11 |
| exceção com evidência | 10 |
| não endereço | 1 |
| pendente | 0 |

Por conteúdo: 428 iguais e 0 diferentes. Os outros 124 não têm conteúdo a comparar: sistema, BSS, limites e
exceções.

**Resta no P1:** nada que bloqueie o P2. `8CDE0000` (restos do boot do IP.BIN) é reconferido no P4, e a função de
2 bytes `8C04FAD0` fica sem par (nenhum endereço fixo aponta para ela).

## 7. P2, tabela de endereços (2026-11-19)

**O desenho, dentro da arquitetura A:**
- **Escolha na compilação:** cada instalação é compilada pelo Builder a partir do disco do usuário, e o C++ gerado já
  é o da versão. Por isso a versão vem da compilação (`BS_VERSAO_JOGO`), e não do boot.
- **`bs::jogo(0x8C……)`:** o código nativo passa a citar o endereço **americano** dentro dessa função `consteval`
  (`runtime/versao/include/versao/enderecos.h`). Os comentários continuam no endereço americano.
  - **Americano** (padrão, sem nenhuma definição no CMake): a função devolve o próprio endereço.
  - **PAL alemão** (`-DBS_VERSAO_JOGO=1`): ela procura o endereço em `enderecos_pal_de.inc`, gerado por
    `tools/dev/versao_traduz.py --inc` (551 pares). Um endereço sem par não compila.
- **`bs::jogo_ram(...)`:** a mesma tradução, como deslocamento na RAM (`ram_ + …`).
- **A pasta `runtime/versao/include`:** visível a todos os alvos, porque alguns, como o `bs_video_mw`, não enxergam
  `runtime/`.
- **A troca:** feita por script (`tmp/pal-p2/troca_enderecos.py`): 278 literais em 23 arquivos.
  - O script pula comentários, textos, a área do sistema, `bios_entry_state.inc` e `system_area.h`.
  - Os 22 deslocamentos de RAM do `game_host.cpp` foram trocados à mão.

**A prova de equivalência do americano:**
- O projeto foi exportado pelo Builder (CDI), com `--somente-exportar`, e compilado pelo `--de-projeto` antes e depois
  da troca, no mesmo caminho.
- Os dois `BlueStinger.exe` têm o mesmo tamanho (20 902 912 B). São idênticos byte a byte em `.text` (17 MB),
  `.data`, `.pdata`, `.rsrc` e `.reloc`. Na `.rdata`, só diferem os 2 bytes da hora de compilação embutida (17:38 e
  17:41).
- Comparador: `tmp/pal-p2/compara_pe.py`, que mascara só o carimbo do COFF, o checksum e o diretório de depuração.
- Os arquivos alterados foram recompilados nesta passada; o log foi conferido. Os três só de validação foram
  compilados no build PAL de validação.
- **Conclusão:** o programa americano é o mesmo, instrução por instrução. Por isso o C8, as referências 16/16 e os
  traços não precisam rodar de novo, porque mediriam o mesmo binário.

**O PAL:**
- O build de validação com `BS_VERSAO_JOGO=1` (`tmp/pal-p2/compila-pal.cmd`) compilou todas as bibliotecas, incluindo
  os arquivos só de validação (`cri_diag`, `gfx_probe`, `draw_class`). Ou seja, todo endereço citado tem tradução.
- O executável do PAL depende do P3, o C++ gerado do PAL.

**Outros testes:**
- a worktree completa compilou (`msvc-release`, 519 alvos);
- `ctest` com o CDI: 19 aprovados, 1 pulado (`movie_manifest`, sem a variável dele).

**Revisão Codex (`reviews/codex-20261119-174611.md`), 3 achados, todos aceitos e corrigidos:**
1. **MÉDIA, mods no PAL:** a API de mods recebia o endereço sem tradução.
   - **Primeira correção, desfeita no P4:** a API traduzia na entrada, mas os adaptadores nativos já passam o
     endereço da versão (o mesmo que usam em `sh4_lookup_generated`). O primeiro boot do PAL acusou a tradução dupla
     (`8C15D486`, o servidor do controle).
   - **Contrato final:** a API de mods usa os endereços da versão compilada, os do código gerado.
     - Num mod, o autor escreve o endereço americano em `bs::jogo(0x8C…)`, como o código nativo. Isso vale para o
       exemplo do `mod_api.h` e o do projeto exportado (`builder/project_export.cpp`).
     - Só as entradas de texto (`BS_DESLIGAR_FUNCOES`, `BS_TELA_LOGICA43`, `BS_CALL_TRACE`) traduzem o americano na
       leitura, por `mods::endereco_da_versao`. No americano, é inline e devolve o próprio valor. No PAL, endereço fora
       da tabela é `runtime_error`.
2. **BAIXA, `BS_TELA_LOGICA43=1`:** o texto, inclusive o atalho `1`, é lido como americano e traduzido. A tabela e o
   modelo `tela43<…>` usam `bs::jogo`.
3. **BAIXA, testes:** `tests/unit/test_versao.cpp`, compilado duas vezes, gera `bs_versao_us_tests` e
   `bs_versao_pal_tests` (ctest `versao_us`, `versao_pal`). Os dois aprovados. Cobrem:
   - traduções conhecidas: função, espera, biblioteca, exceção, sistema e RAM;
   - limites de faixa;
   - endereço ausente em tempo de execução;
   - a tabela ordenada e sem chave repetida (`static_assert`).

**As provas, refeitas depois das correções:**
- **Executável americano:** de novo idêntico em `.text`, `.data`, `.pdata`, `.rsrc` e `.reloc`. Na `.rdata`, só a
  hora de compilação (17:38 e 17:53). `mod_api.cpp`, `call_trace.cpp` e `game_host.cpp` foram recompilados.
- **ctest:** 21 aprovados, 1 pulado (`movie_manifest`).
- **Build PAL de validação:** tudo compila, exceto o link do `bs-extract` (abaixo).

**Pendências anotadas:**
- **Mods no PAL:** a tabela cobre só os 551 endereços que o código nativo cita. Um mod que mire outra função do
  americano dá erro claro no PAL. A cobertura completa, com as 4 035 funções do mapa, fica para a Remastered PAL (D-P3)
  e para o Builder do PAL (P8): `mapa_funcoes.tsv` do projeto exportado e o texto do exemplo.
- **Pré-existente, da v0.9.7-3, sem relação com o PAL:** o `bs-extract` não linka com `BS_BUILD_TOOLS=OFF`, porque o
  `main.cpp` usa os pacotes `.zip`/`.7z`, que só existem com as ferramentas.
  - Não afeta o Builder nem o `compilar.cmd`, que compilam só o alvo `bluestinger`.
  - Só um "compilar tudo" pela IDE no projeto exportado falha.

## 8. P3, configuração e recompilação do PAL (2026-11-19)

- **A configuração:** `tools/dev/versao_config.py` gera `config/bluestinger_pal_de.toml` a partir do americano.
  - Traduz os 105 endereços de código pela mesma tabela do `bs::jogo`, e um endereço sem par é erro.
  - Os comentários continuam com os endereços americanos.
  - `[game]` e `[input]` levam os dados da versão: `T13001D`, `19990803`, área `E` e o executável de 1 979 556 B
    (`7e811270…`).
- **A análise** (`bs-recomp --analyze-only`, saída em `tmp/pal-p3/analise-pal`):
  - 4 084 funções, 444 046 instruções, 18 tabelas de salto, 0 órfãs, 0 avisos e 0 fins mortos;
  - o americano, na mesma ferramenta, dá 4 083 funções, também com 0 órfãs e 0 avisos.
- **O C++:** 12 arquivos, 4 084 funções, 981 352 linhas (`tmp/pal-p3/gen-pal`).
- **O executável:** o projeto exportado, com o `generated/` e a config do PAL e `-DBS_VERSAO_JOGO=1`
  (`tmp/pal-p3/compila-jogo-pal.cmd`), compila sem erro. Resultado: `bluestinger.exe` de 20 908 032 B.

## 9. P4, boot (2026-11-19, em andamento)

- **Reconhecimento:** o PAL alemão entrou em `kExtraVersions` (`bluestinger_pal_de_v1000`, hash exato).
  - `game_version_of(id)` dá a versão, na numeração de `BS_VERSAO_JOGO`.
  - O boot nativo recusa dados de outra versão, como um executável americano com dados do PAL e vice-versa.
  - O Builder recusa o PAL com uma mensagem clara até o P8.
  - Teste: `boot_identity_versao_do_jogo`; `bs_unit_tests` 373/0.
- **Os dados:** `bs-extract --runtime` da imagem GDI alemã gerou 2 095 arquivos, 940 MB, conferidos setor a setor
  contra a imagem.
- **Primeiro boot, sem janela** (`--headless 1500`, software): roda os 1 500 VBlanks e faz 733 renders, sem erro.
  - **Telas:** logos da Activision e da Climax, a abertura e a tela de título **"DRÜCKEN SIE START"**, com o copyright
    em alemão. Capturas em `tmp/pal-p3/cap1`.
  - **Subsistemas nativos:** os arquivos (30 aberturas), os saves (mount, exists), os efeitos e a música funcionaram.
  - **Quadro:** 640×476.
- **O americano depois destas mudanças:** a checagem de versão no boot mudou o binário, então a prova é de
  comportamento.
  - Os dois executáveis rodaram 3 000 VBlanks sem janela, com os mesmos dados e `BS_RELOGIO_FIXO`.
  - Os 30 quadros são idênticos byte a byte, e o resumo final também. Só muda a pasta temporária de saves (pid).
- **O modo de vídeo e a cadência:**
  - **O que o jogo programa:** `BS_PVR_DEBUG` mostra o jogo alemão escrevendo `SPG_LOAD = 0x0270035F` (625 linhas,
    864 ciclos por linha) e `SPG_CONTROL = 0x190` (PAL, entrelaçado). Ele escolhe o PAL sozinho; nenhuma consulta de
    região passou pelo serviço de syscalls.
  - **A compensação de 50 Hz é fixa:** por exemplo, ×5/6 em `8C0498C0`, sem condição. O jogo assume 50 campos por
    segundo.
  - **A cadência nativa:** `kFieldRateNum/Den` em `runtime/cpu/time_hooks.h` dá 50/1 no PAL e 60000/1001 no
    americano, com a mesma conta de antes.
    - Usam essa taxa: `kCompatTicksPerField`, o ritmo do host (`kField`, `kFieldMs`), o relógio das falas no
      `game_host`, as amostras por campo do `sound_adapter` e o passo da câmera livre.
    - O modelo do PVR já tirava as linhas por campo do `SPG_LOAD` escrito pelo jogo.
  - **Prova:** em 1 500 campos sem janela, o relógio da música dá 7 501 ticks, os 7 500 esperados a 50 Hz (eram
    6 257 a 59,94 Hz). As telas seguem a mesma sequência por campo, até o título.
  - **O americano:** depois de todas as mudanças, 3 000 VBlanks sem janela dão 30 quadros idênticos ao executável de
    referência, e o resumo final também. `ctest`: 21 aprovados, 1 pulado.
- **Revisão Codex (`reviews/codex-20261119-190013.md`), 4 achados, todos aceitos e corrigidos:**
  1. **MÉDIA, o estado inicial do PVR era o medido no NTSC.**
     - No PAL, o boot agora carrega no SPG os valores PAL que o próprio jogo programa:
       - `SPG_HBLANK_INT`, `SPG_VBLANK_INT`, `SPG_CONTROL`, `SPG_HBLANK`, `SPG_LOAD`, `SPG_VBLANK`, `SPG_WIDTH`,
         `VO_STARTX` e `VO_STARTY`.
     - Com isso, o campo de 20 ms e as 625 linhas ficam coerentes desde o início.
     - **É provisório:** o estado real do BIOS PAL depende do oráculo PAL (P7).
     - **Prova:** as 15 telas do boot ficaram idênticas às de antes da mudança.
  2. **MÉDIA, os pacotes de mods e legendas filtravam pela identidade americana.**
     - `bs::kJogoIdentidade` (`versao/enderecos.h`) dá `T13001D V1.000` no PAL.
     - Ela é usada nas 4 chamadas de `ModPackages::discover` (`game_host`, `main_sdl`).
  3. **BAIXA, o exemplo de `mods::wait`:** agora usa `bs::jogo(...)`.
  4. **BAIXA, a cadência:** `versao_cadencia_de_campos`, nas duas versões, cobre:
     - a taxa;
     - os ticks por campo (3 336 666 no NTSC; 4 000 000 no PAL);
     - as 882 amostras por campo e um segundo de amostras em 50 campos;
     - a identidade do jogo.
- **Provas, refeitas depois da revisão:**
  - `ctest` com o CDI: 21 aprovados, 1 pulado (`movie_manifest`); `versao_us` e `versao_pal` aprovados;
  - o PAL chega ao título, com 7 501 ticks de música em 1 500 campos;
  - o americano dá 30 quadros e o resumo idênticos ao executável de referência.
- **Falta no P4, sem bloquear o P5:**
  - o estado real do BIOS PAL, que depende do oráculo (P7);
  - a janela a 50 Hz, ainda não vista;
  - o quadro de 640×476.

## 10. P5, filmes PAL (2026-11-19)

- **O que os SFD do disco alemão têm** (medido em `tmp/pal-p5`, `sfd_ids.py`, `sfd_adx.py`, `compara_us.py`):
  - vídeo MPEG-1 320×240 a **25 fps** (código 3 no cabeçalho de sequência; E001: 342 quadros, 13,68 s; no americano,
    411 quadros, 13,70 s). A medida do P0 ("50 fps e duas trilhas") estava errada;
  - **um** stream ADX (0xC0), 44,1 kHz estéreo, com o dobro da duração do vídeo nos 16 filmes;
  - a primeira metade é o áudio americano (inglês) recodificado: envoltória de 1/4 s com correlação 1,000 em E001, E016 e
    E179; amostras diferentes. O E200B difere nos primeiros 20 s (0,03–0,09), igual depois (0,79–0,97);
  - a segunda metade não tem relação com o filme nem com o áudio americano (correlação ≈ 0): o preenchimento do I5, alto
    em 8 dos 16.
- **Conversão:** o Builder já serve sem mudança: a taxa sai do SFD (25000/1000) e o corte do I5 tira a segunda metade
  (`fim_audio_100ns == fim_video_100ns` nos 16). O relógio dos filmes é o de 200 MHz (`video_adapter.cpp`), que no PAL
  anda 4 000 000 ticks por campo: o filme toca em tempo real, sem conta de 60 Hz.
- **Medida corrigida** (`verify_conversion`, `builder/video/sfd_convert.cpp`): o SNR do áudio ia até o fim do AAC, que
  passa do corte por menos de um quadro do AAC (892 amostras no E016), e comparava esse resto com o preenchimento
  descartado. No E016 dava 24,1 dB; até o corte, 45,4 dB (o americano: 45,0). A medida agora para no mesmo corte da
  conversão. Os MP4 não mudam; o americano do CDI (sem preenchimento) não muda.
- **Prova:** `movie_manifest` com `BS_DATA_DIR` nos dados PAL: 16 filmes iguais ao manifesto do Windows, `OPENING`
  ausente, 0 falhas. PSNR e SSIM no nível do americano (PSNR médio 40,3–48,0 dB; SSIM médio ≥ 0,982).
- **Nota:** o primeiro lote foi feito por engano com o `bs-video-probe` do checkout principal (v0.9.7-1, sem o I5) e
  saiu sem o corte; está guardado em `tmp/pal-p5/video-sem-corte-v0971` como evidência.
- **Falta:** o fim de cada filme no jogo PAL (passo de jogo) contra o oráculo, no P7; as legendas (P6).
