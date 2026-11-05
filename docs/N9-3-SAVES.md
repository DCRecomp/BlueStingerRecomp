# N9.3: saves de PC (edição Remastered) — CANCELADO

> **Cancelado em 2026-11-05 por decisão do mantenedor** (`docs/N9-PLANO.md`, seção 15): o sistema de saves original
> fica como está. Este documento é só histórico: a pesquisa feita antes do cancelamento, sem nenhuma implementação.
> Fica como referência técnica do formato original (imagem VMS, campos, fluxo do ponto de save).

Autorizado pelo mantenedor em 2026-11-05 (`docs/N9-PLANO.md`, N9.3), com as decisões 12.4 (formato) e 12.5 (autosave)
definidas por ele. Contrato da campanha (14.2): os saves de PC são uma **camada de cima** do cartão nativo do N4. O que a
reprodução compara (cartão, envelope, conteúdo) não muda.

## 1. O que já existe (N4, `docs/N4-PREPARACAO.md`)

- **Cartão nativo:** `<raiz dos saves>/card0/BLUESTIN.00n.bsv`, n de 0 a 5. Cada arquivo é um envelope `.bsv` v1, com
  "BSSV", versão, tamanho, CRC-32 da imagem e data, em volta da **imagem VMS exata** que o jogo monta. A gravação é atômica,
  com `.bak`, `.tmp` descartado e `.corrompido` preservado (`native/platform/saves_store.cpp`).
- **Imagem VMS do Blue Stinger:** 2048 B, ou seja, 4 blocos de 512. Tem o cabeçalho VMS ("BlueStinger", "BLUESTINGER SAVE
  DATA"), 1 ícone e 1200 B de dados, com CRC `8C08CDE2`. **O estado do jogo vai de `VMS[0x200]` a `0x8C24F8BC`**: o LOAD lê do
  bloco 1 em diante para a área de estado (N9.2, rastreio de `8C01C4AE`).
- **Raiz dos saves:** `Documentos\DC-Recomp\Blue Stinger\saves` por padrão, `saves\` ao lado do jogo no modo portátil,
  `--saves-pasta` e, no headless sem pasta, um cartão temporário.
- **Fluxo de SAVE do jogo:**
  1. O ponto de save é um ator kind 98. Com o A, `8C0407C0` chama `8C04FB80`, que devolve 4, e o jogo grava 7 em
     `[8C24D2CC+0x25B2]`.
  2. Ele pergunta "Do you want to Save here?", confirma e passa a 8.
  3. O laço `8C052240` chama o painel `8C01D200(0, 8)`: soquete, bloco e confirmação. Ele grava por
     `8C01CCE0 -> 8C01C520 -> 8C08C818` e mostra "Save completed.".
- **Prova do N4 (`BS_SAVES_H2`):** só a presença do ponto de save é simulada. Um SAVE no corredor RCG02 carregou no Windows
  e no Flycast na mesma posição, e a partida continuou jogável.
- **LOAD nativo do N9.2:** `8C01CE00(drive 0, arquivo n, 0x22)`, sem o painel.

## 2. Pesquisa do N9.3 (interrompida pelo cancelamento)

### 2.1 Metadados na imagem VMS (2026-11-05)

Comparei três imagens distintas das sessões de desenvolvimento, só lendo os arquivos (`g1` = 1644836c, "ELIOT 00:03:58 $0").
Em 0x200–0x800, só 42 bytes variam.

| Offset | Campo (hipótese) | Prova |
|---|---|---|
| `0x284` (u32) | tempo de jogo em 1/30 s | `g1` = 7167, ou seja, 238,9 s = 03:58 como na lista do jogo; os outros dão 13115 e 18892 |
| `0x280` (u32) | dinheiro? | `g1` = 0 (lista: $0); os outros dão 30 e 110. Falta confirmar |
| `0x28C` (u16) e `0x28F` (u8 = 4) | área ou sala? e o `F94B` = 4 depois do load | falta confirmar |
| `0x4D0`, `0x4D8` (f32) | posição x e z do jogador | `g1`: 126,85 e −560,4 (o ponto do LOAD) |

O nome é o personagem jogável, por uma tabela de índice: "ELIOT" ou "DOGS" (`8C0A960C`). O campo do índice ainda não foi achado.
A lista do painel é desenhada em `8C01E054` e `8C01E438`, que usam `"????????"` (`8C09BDF4`) quando a área não tem nome.

### 2.2 Próximos passos da pesquisa

- Confirmar o dinheiro, a área e o personagem: ler o código de `8C01E054` e `8C01E438`, ou gerar saves sintéticos novos.
- Achar o contrato entre `8C052240` e o painel `8C01D200(0, 8)`: o que o laço espera do painel para voltar ao jogo.
- Achar a assinatura e os retornos de `8C01CCE0`, a máquina de SAVE.
- Definir os pontos seguros de save e de autosave (12.5) e documentá-los antes de implementar.
- Desenho proposto, a confirmar na implementação:
  - a Remastered usa um cartão de trabalho próprio, para nunca tocar o cartão da Original;
  - a biblioteca de saves de PC (`.bssave`) tem cabeçalho TOML legível, a imagem VMS exata e o SHA-256;
  - a gravação é atômica, com `.bak`;
  - os saves do N4 são importados por cópia, com os originais intactos;
  - o comparador da campanha ignora só a data dos `.bssave`, como já faz com o envelope `.bsv`.
