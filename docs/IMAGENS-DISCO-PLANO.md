# Imagens de disco: GDI, CUE, CHD e variantes de CDI (plano, 2026-11-18)

Origem: issues #1 (CDI recusado), #2 (CHD), #3 (GDI) e os comentários delas. As imagens do mantenedor ficam em `ISO/` (nunca
versionadas). Toda a análise foi só de leitura, em `tmp/discos/`.

## 1. O que as imagens mostram

| Imagem | Formato | `1ST_READ.BIN` | Arquivos iguais ao CDI |
|---|---|---|---|
| `Blue Stinger.cdi` (a referência do projeto até hoje) | CDI 3.5, 2 sessões | 1 975 556 B, `013301f3…` | — |
| `Blue Stinger (USA).7z` | Redump: `.cue` + 3 `.bin` (faixa 3 na LBA 45000) | 1 975 556 B, `491fd69c…` | 2 075 de 2 092 |
| `Blue Stinger (USA).chd` | CHD v5 (`cdlz`, `cdzl`, `cdfl`) | ainda não lido (precisa do leitor CHD) | — |
| `Blue Stinger … (PAL)(DE)[!].zip` | TOSEC: `.gdi` + `track01.bin`, `track02.raw`, `track03.bin` | 1 979 556 B, `7e811270…` (T13001D) | 1 916 de 2 092 |

- **O CDI do projeto não é o disco original: é uma conversão modificada.**
  - **Executável:** difere do Redump em **7 bytes, em 3 lugares**. São literais lidos da memória (`mov.l @(disp,PC)`), não
    instruções:
    - `8C15AEE4` e `8C15B1C8`: 45150 → 150 (o FAD do início da área de alta densidade);
    - `8C1DAC38`: 45166 → 239521 (o FAD do descritor ISO9660).

    Quem fez o CDI mudou essas posições para o layout de CD-R.
  - **Filmes:** os 16 SFD do CDI foram recomprimidos e são de 9 % a 43 % menores; o `ENDING.SFD` tem 94 MB contra 165 MB
    do original. Por isso a #2 diz que o "CDI corta conteúdo".
  - **Arquivos de texto:** o CDI não tem `ABSTRACT.TXT`, `BIBLIOGR.TXT` nem `COPYRIGH.TXT`.
- **O C++ gerado a partir do executável original é idêntico, byte a byte, ao do CDI:** os 14 arquivos de `generated/` e a
  análise. Foi testado com `bs-recomp --game-data` em `tmp/discos/recomp-gd`.
  - Como os literais são lidos da RAM, o código recompilado serve aos dois discos, e cada imagem fornece os FADs do seu
    próprio layout.
  - O runtime não tem LBA fixo do layout do CDI: as faixas vêm do manifesto, e o FAD é sempre LBA + 150.
- **Causa da #1:** a identidade da versão é o SHA-256 exato do `1ST_READ.BIN`. Cada ferramenta de conversão para CDI
  escreve os FADs de um jeito, então quase toda imagem de outra fonte é recusada, e o próprio disco original (GDI/Redump)
  também.
- **PAL DE:** é outra versão (T13001D):
  - executável com outro tamanho;
  - 176 arquivos diferentes;
  - o bug do 480p citado na #4.

  Fica fora deste plano, assim como o Director's Cut (#4).

### 1.1 Achados da implementação (2026-11-19)

- **I4, o boot a partir do disco original:** o IP.BIN do CDI também foi trocado. É outro código de boot, com zeros no fim.
  - **O problema:** o jogo lê em `0x8C00FFF0`/`FFF4` a informação de boot do sistema (`f_8C159E8E`; zero = primeira
    entrada). Esse endereço fica na pilha do IP.BIN, que o boot do console usa e limpa.
  - **Com o IP.BIN original:** nesse lugar estão o fim do código de boot e o título. O jogo seguia um ponteiro feito de
    espaços (`0x20202020`) e pedia o menu do sistema no 13º VBlank.
  - **A correção:** o boot nativo zera `0x8C00F3B0`–`0x8C00FFFF`, que é o estado medido na entrada. Para o CDI isso não muda
    nada, porque a área já era zerada.
- **I5, os filmes:**
  - os 16 SFD do original têm os mesmos quadros do CDI, e o som é idêntico (correlação 0,99997);
  - mas, depois do som, o ADX traz um **preenchimento constante** (nível DC, sem som) **tão longo quanto o próprio filme**:
    E001 tem 27,4 s de áudio para 13,7 s de vídeo, e NEWGAME tem 905 s para 452 s;
  - a regra de fim é "o que acabar por último", medida com o CDI, onde os dois fins coincidem. Por ela, o filme ficaria o
    dobro do tempo parado.

  **Correção:** a conversão corta no fim do vídeo o áudio que passar dele por 1 s ou mais.
  - O preenchimento não é sempre silêncio: no NEWGAME são 7,5 min de lixo saturado, sem relação com o filme.
  - O arredondamento dos blocos, de milissegundos, fica como está, então os filmes do CDI não mudam.
  - Os rótulos do conversor mudaram, e isso refaz as conversões antigas.
  - Os quadros e o som de cada filme continuam iguais aos do CDI, então os mods widescreen e as legendas valem.
  - Falta confirmar no oráculo (Flycast com o GD) como o console termina esses filmes.
- **I6, o CHD:** nos metadados de GD-ROM do chdman, o campo `PAD` não é o preenchimento guardado. No CHD do Redump ele cobre
  o vão até a LBA 45000. Como no MAME, cada faixa ocupa um múltiplo de 4 quadros.
- **Provas (2026-11-19):**
  - Com o CUE do Redump, a instalação completa pelo Builder passou, e o `test_gameplay` também: NEW GAME, o filme, a área 1
    com o HUD e o movimento.
  - O CHD passou no `real_disc`, e o executável é reconhecido como o do GD original.

## 2. Etapas

| # | Etapa | Custo | Prova |
|---|---|---|---|
| I1 | **Mensagem de recusa com o tamanho e o SHA-256** do executável encontrado, desembaralhado ou não, e o que fazer | pequeno | unitário com imagem sintética |
| I2 | **Leitores GDI (TOSEC) e CUE (Redump)**, como novas subclasses de `DiscImage`, já previstas em `disc_image.h` | médio | unitário sintético; `BS_DISC_IMAGE` com as duas imagens |
| I3 | **Versão `bluestinger_us_v1000_gd`** (`491fd69c…`), com a mesma configuração: `first_read_sha256` vira uma lista | pequeno | o recomp idêntico (seção 1) vira teste |
| I4 | **Dados de execução a partir do GD:** a área de alta densidade como faixa de boot e o TOC do GD-ROM pelo serviço nativo | médio | `bs-extract --runtime` com verificação setor a setor; `test_gameplay` (NEW GAME até a área 1) e `test_assistido` com o mesmo roteiro do CDI, estado por passo |
| I5 | **Filmes do original:** maior qualidade, conversão normal | pequeno | número de quadros e duração de cada SFD original contra o do CDI; os mods widescreen e as legendas dependem disso |
| I6 | **CHD** pela libchdr (BSD-3, com LZMA SDK, zlib e dr_flac, todos permissivos) | médio | entrada no `N9-L-LICENCAS.md`; revisão Codex; o CHD deve dar os mesmos arquivos do Redump |
| I7 | **Variantes de CDI:** identidade com os 3 literais mascarados. Aceita qualquer imagem cujo executável só difira neles; o resto do binário continua exigido byte a byte | pequeno | unitário (literal trocado aceito, instrução trocada recusada); o CDI atual e o Redump continuam aceitos |
| I8 | **Arquivos compactados** (`.zip`, `.7z`), se aprovado (D1) | médio | unitário; as duas imagens do mantenedor |
| I9 | **Instalador e textos:** filtro de arquivos, mensagens pt-BR e en-US, `LEIA-ME`, README, `LEGAL.md` e `docs/MODS-VIDEO.md`; o rótulo "cerca de 1 GB" dos filmes widescreen passa a "cerca de 2,5 GB" | pequeno | `bs_ui_tests`, `bs_instalador_tests`, `test_instalador.ps1` com cada formato |
| I10 | **Fechamento:** `test_builder` com o CDI e com o GDI, revisão Codex, release (v0.9.7-3, se autorizada) e respostas nas issues | — | nível 2 do subsistema de disco |

Ordem: I1 → I2 → I3 → I4 → I5 (o mínimo para a #1 e a #3) → I7 → I6 → I8 → I9 → I10. As etapas I1–I3 e I7 rodam sem a
GPU; I4, I5 e I10 compilam e rodam o jogo, então esperam a fila dos filmes.

## 3. Decisões do mantenedor

**Respondidas em 2026-11-18:**
- **D1: (c)**, `.zip` e `.7z`.
- **D2: aprovada**, só as 3 posições.

- **D1. Arquivos compactados:**
  - (a) não aceitar: o jogador extrai;
  - (b) só `.zip` (miniz, MIT);
  - (c) `.zip` e `.7z` (miniz + o decodificador 7z do LZMA SDK, domínio público).

  Recomendo (c): é como as imagens costumam ser guardadas. A extração vai para a pasta de trabalho da instalação e é
  apagada no fim.
- **D2. Variantes de CDI (I7):** a máscara aceita só as 3 posições conhecidas. Uma variante que mexa em outra posição continua
  recusada, e a mensagem do I1 mostra o hash para análise. Recomendo aprovar.
- **D3. Referências:** as referências da edição Original (N8) foram feitas com o CDI, e continuam valendo. O GD ganha uma
  suíte própria, de boot, gameplay e filmes, sem trocar as referências.
- **D4 (mantenedor, 2026-11-19): o PAL entra depois da v0.9.7-3**, como a próxima etapa, com plano próprio. O Director's
  Cut continua fora.
- **D4 (texto original). PAL e Director's Cut:** ficam fora deste plano. Cada um é uma recompilação nova (#4).

## 4. Riscos

- **TOC e sessões:**
  - no GD-ROM, a área de alta densidade não é uma "segunda sessão" como no CDI;
  - o `bootTrack()` e a resposta do TOC precisam do modelo de áreas;
  - o I4 prova isso pelo boot e pelo gameplay.
- **Filmes:** se um SFD original tiver outro número de quadros, o mod widescreen correspondente (feito a partir do CDI) e as
  legendas precisam de revisão. O I5 confere isso antes.
- **CHD com outro dump:** o CHD do mantenedor pode não vir do mesmo Redump. Vale o hash do executável, como em qualquer
  formato.
