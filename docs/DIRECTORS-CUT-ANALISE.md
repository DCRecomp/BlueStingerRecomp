# Blue Stinger Director's Cut: análise do patcher (2026-11-19)

Pedido do mantenedor: dar suporte ao patch `espouaisso/Blue-Stinger-Directors-Cut-Patcher` (GitHub, commit `747b4cc`,
2026-11-01). Só leitura: o código foi lido numa cópia em `tmp/dc-patcher/src` e nada dele entra no repositório.

## 1. O que o patch é

- **Licença GPL-3.0.** O projeto é MIT (CLAUDE.md): o código do patcher não pode ser copiado nem portado para o
  repositório. Ele serve só como referência de comportamento.
- **Entrada:** o disco japonês v1.006 (`1ST_READ.BIN` md5 `c46d2bdc…`) mais o americano v1.000 GDI (`25521308…`, versão
  EN) ou o francês PAL v1.000 (`e7b7484c…`, versão FR). A saída é um GDI novo. O patcher não inclui dados do jogo.
- **A base é o executável japonês.** As câmeras, os scripts e a jogabilidade são do JP, que tem as câmeras originais da
  Climax. O ocidental foi alterado a pedido da Activision para a câmera em terceira pessoa. O EN e o FR só trazem os
  textos, as texturas e os filmes.

## 2. O que muda no disco japonês

| Parte | Mudança | Efeito para o recompilador |
|---|---|---|
| `1ST_READ.BIN` | Os textos do executável (itens, lugares, menus, 193 miniscripts com texto) vão para uma região nova em `0x8C329600`, depois do fim do programa; todo ponteiro alinhado para a área antiga (`+0x8ECB4..+0x957B6`) é reescrito; o literal do fim do programa (`+0x7A84C`, usado pela inicialização do heap, era `0x8C1D75D4`) passa a apontar para a região, e o heap começa depois dela | o código não muda, mas o executável fica maior (cerca de 3,2 MB, com zeros até a região) e o heap começa em outro endereço; literais do código trocados são lidos pela análise estática |
| `1ST_READ.BIN` | Tabela da fonte (`+0x95DC8`) refeita, espaçamento dos estilos de diálogo 0, 1, 3 e 5 (`+0x8EB68`) copiado do ocidental, nome do save do VMU (`+0x8A6B4`) em inglês | só dados |
| `BSDATA/R*.BIN` | O bloco de texto de cada sala troca pelo ocidental; o script japonês fica (no FR, 5 salas usam o script do PAL FR, com o tempo das legendas corrigido) | dados de execução |
| `TPOMMON.NT` | Glifos ocidentais no lugar de kanji não usados | dados de execução |
| Texturas `.NT` | Placas e avisos traduzidos de cerca de 60 pacotes | dados de execução |
| Filmes | 8 SFD com texto na imagem vêm do ocidental; o `OPENING.SFD` (trailer só do JP) vira 1 s de preto sem som | o ID 0 (`OPENING.SFD`), ausente no US e no PAL, existe aqui |
| Trilha 3 | Refeita com os arquivos nos novos tamanhos e EDC/ECC novos | os setores mudam; o hash do disco não é fixo |

## 3. O que "suportar" exige

O jogo do Director's Cut é o **executável japonês v1.006**, não o americano. Para o recompilador, é uma terceira versão
do jogo, como a alemã (arquitetura A, docs/PAL-PLANO.md seção 2):

1. **Mapa JP → US** (`tools/dev/versao_mapa.py`) e tabela `enderecos_jp_dc.inc` para os cerca de 550 endereços do código
   nativo. O alemão era o mesmo programa relocado (4.035 de 4.080 funções casaram). O japonês é outra compilação (v1.006,
   lançado antes), com a câmera diferente; a taxa de casamento é desconhecida até medir.
2. **Configuração e análise** do `bs-recomp` sobre o executável já patcheado (os literais trocados entram sozinhos no
   C++ gerado). O tamanho e o hash do `1ST_READ.BIN` dependem da versão do patcher e dos discos de entrada (EN ou FR).
3. **Código nativo sensível à câmera:** a câmera Livre (obrigatória na Remastered), o HUD (`kamui/hud_table.h`), os
   widescreen do N9.5 e as esperas do tempo foram feitos sobre o código americano. No JP, a câmera é exatamente o que muda.
4. **Builder:** reconhecer o GDI patcheado pelo `1ST_READ.BIN` (lista de hashes conhecidos do patcher, EN e FR) e
   aceitar a trilha 3 refeita (o hash do disco inteiro não serve).
5. **Validação:** o oráculo do Flycast com o disco patcheado, a rota NEW GAME → área 1, saves (nome do VMU diferente) e
   filmes (17 IDs com o OPENING).

## 4. Opções

- **A (recomendada):** o usuário roda o patcher dele (GPL, fora do nosso pacote) e o nosso Builder aceita o GDI
  resultante como mais uma versão. Nada de GPL no repositório; o suporte segue o caminho do PAL.
- **B:** o Builder aplicaria o patch sozinho, a partir dos discos JP e US. Exigiria uma reimplementação própria; como o
  código já foi lido, não seria uma implementação limpa. Só com permissão do autor (relicenciar para MIT ou autorização
  escrita).
- **C:** pedir ao autor que publique os deslocamentos e formatos como documentação ou sob licença permissiva, e decidir
  depois.

## 5. O que falta para começar

- **Um dump próprio do disco japonês v1.006** (TOSEC `Blue Stinger v1.006 (1999)(Sega)(JP)[!]`, md5 da trilha 3
  `ca82462f…`). Sem ele nada pode ser medido. Imagens baixadas não servem (regra do projeto).
- O hash do `1ST_READ.BIN` americano que o patcher espera (`25521308…`) é o do GDI; o do CDI do projeto é outro
  (`db902f1b…`). A versão EN precisa do GDI americano.
- A decisão do mantenedor: a opção (seção 4), e a ordem em relação ao PAL (P5 a P8 em andamento).

## 6. Pedido do mantenedor e medidas (2026-11-19, continuação)

Pedido: uma opção "Director's Cut" na instalação; o instalador baixa o patcher do repositório original e aplica o patch
antes de recompilar (se o disco já vier patcheado, não aplica), com confirmação; o projeto deve aceitar todas as versões
da pasta `ISO`.

Discos da pasta `ISO`, identificados pelo `1ST_READ.BIN` (md5, o mesmo da lista do patcher):

| Imagem | Versão | Executável | Observação |
|---|---|---|---|
| `Blue Stinger.cdi` | US v1.000 (CDI do projeto) | `db902f1b…`, 1 975 556 B | 7 bytes diferentes do Redump (posição dos dados) |
| `Blue Stinger (USA).chd/.7z` | US v1.000 (Redump) | `25521308…` | a entrada EN do patcher |
| `…(PAL)(DE)[!].zip` | PAL alemão `T13001D 18` | `c55b052c…` | P1–P4 feitos |
| `Blue Stinger (Europe).chd` | PAL inglês `T13001D-05` | `be66765c…`, 1 975 524 B | o patcher não usa |
| `Blue Stinger (Japan).chd` | JP v1.006 `HDR-0003` | `c46d2bdc…`, 1 963 652 B | a base do Director's Cut; tem o `OPENING.SFD` |

Mapa de funções contra o americano (`versao_mapa.py`; no JP com `BS_MAPA_JANELA=262144,262144`):

| Versão | Funções casadas | Sem par | Literais/bra divergentes | Endereços nativos pendentes (`versao_traduz.py`) |
|---|---|---|---|---|
| PAL DE | 4 035 / 4 080 | 44 | 0 / 1 | 0 (10 exceções conferidas) |
| PAL EN | 4 059 / 4 080 | 20 | 0 / 1 | 6 (os mesmos casos das exceções do DE) |
| JP v1.006 | 3 974 / 4 080 | 106 | 0 / 1 | 25: câmera Livre (`8C0528xx`, `8C2D0CC0`), HUD (`hud_table.h`), espera do VBlank `8C064A2C`, vozes `8C159394/CA`, saves `8C0792CC`, título da Remastered, faixas da RAM |

O Director's Cut usa o código do JP; o patch só muda dados do executável (seção 2). A tabela do JP serve para ele, com o
tamanho, o hash e o início do heap próprios.

## 7. Decisões do mantenedor (2026-11-19)

- **Python:** o instalador usa o Python 3 do sistema; no Windows sem Python, baixa o Python "embeddable" oficial de
  python.org para a pasta do instalador (versão e hash fixos), sem instalar nada no sistema.
- **Ordem:** PAL DE (P5–P8, generalizado para N versões) → PAL EN → JP → Director's Cut.
- **Discos do Director's Cut:** ao marcar a opção, o instalador pede o segundo disco (americano Redump, ou PAL francês
  para a versão FR) e confirma o download do patcher antes de começar. O patcher é baixado do repositório original num
  commit fixo, com o hash conferido; um disco já patcheado (reconhecido pelo `1ST_READ.BIN`) não é patcheado de novo.
