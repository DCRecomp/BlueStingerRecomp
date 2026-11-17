# N9.4, N9.9, N9.10, N9.11 e N9.12: planejamento detalhado (sem implementação)

**Situação:** planejamento leve feito em 2026-11-15, durante a produção dos filmes do N9.13, a pedido do mantenedor ("Faça
tudo"). **Nenhuma destas etapas começa sem autorização.** Base: `docs/N9-PLANO.md` seções 5, 7, 8, 14 e 15. As decisões D4,
D5 e D6 foram tomadas pelo mantenedor em 2026-11-14.

Ordem que já vale: o N9.8 fecha (D2 + a bateria 8.5); depois N9.4 → N9.9 → N9.13 (os filmes já estão em produção local) → N9.10
→ N9.11 → N9.12 → o gate de campanha.

## 1. N9.4: controles e experiência de PC

### 1.1 Decisão D4 (a), mantenedor 2026-11-14: mouse, e vibração só com prova ("não invente vibrações")

**Evidência estática (2026-11-15):**

- O `pdVib Ver 0.30` está ligado ao executável (banner em 0x8C0B96E9, `analysis/hwscan/sdk_modulos.tsv`).
- O código do módulo fica em 0x8C094FF0–0x8C095424:
  - 0x8C0951BE: o parser da resposta, que compara o tipo de função com 0x0100, o código do Puru Puru;
  - 0x8C0952D0: a chamada de retorno, com o estado por porta em 0x8C2CF480 (28 bytes por porta);
  - 0x8C095350 e 0x8C0953AC: as duas funções que montam os pedidos de vibração.
- **Nenhum chamador:** os endereços 0x8C095350, 0x8C0953AC e 0x8C09529A não aparecem:
  - em nenhuma constante de 32 bits do `1ST_READ.BIN` (pools de constantes e tabelas de ponteiros);
  - em nenhum `bsr`/`bra` do binário;
  - em nenhum dos 2093 arquivos de `game-data` (sobreposições carregadas depois).
- A análise do recompilador também não os achou: em `analysis/functions.tsv`, o 0x8C0952D0 aparece só como `data_pointer`,
  por uma referência que está dentro do próprio módulo.
- Isso bate com a prova em tempo de execução do N3: nenhum comando de vibração na rota até a área 1 (16 290 VBlanks), e o N3-0
  sem chamadas ao `pdVib`.

**Decisão aprovada pelo mantenedor em 2026-11-15:** a vibração fica fora do N9.4.
- **Original:** o comportamento não muda. Nenhum comando de vibração inventado e nenhuma vibração artificial.
- **Remastered:** nada agora. A vibração fica registrada como **possível recurso futuro**, condicionado a uma prova de que o jogo a
  pede (por exemplo, um pedido Puru Puru visto no barramento nativo durante a campanha do gate pós-N9) ou a uma nova autorização.
- A interface `IInput::vibrate` continua sem uso.

### 1.2 O que já existe (base do N9.4)

- `native/platform/input_map.cpp` (N3-1/N3-3): o `ActionMap` com as ações do jogo (A, B, X, Y, START, direcional, C, Z, D,
  gatilhos, analógico) e as da câmera Livre (`CAMERA_X/Y`, `CAMERA_CENTRALIZAR`).
- Teclado e controle SDL3 no `controles.toml`, com os perfis por modelo `[perfis."<guid>"]` e o GUID mostrado ao conectar.
- A zona morta da câmera (`[camera]`).
- O roteiro físico (`BS_CONTROLES_TOML`, `BS_CONTROLE_GUID`), que prova que o remapeamento chega ao mesmo controle lógico.
- Menus navegáveis por teclado, controle e mouse (N9.2), com o mouse físico ignorado sob o controle virtual.

### 1.3 Etapas incrementais (cada uma com nível 1 e o seu teste; nada começa durante a produção dos filmes)

| Etapa | Conteúdo | Teste de aceitação |
|---|---|---|
| 4.0 | Auditoria: a lista de ações, o caminho físico → lógico → entrada gravada e os pontos onde a Original e a Remastered divergem | documento; nenhum código |
| 4.1 | Mapeamento de teclado e controle: completar o `ActionMap` (gatilhos analógicos, botões extras do SDL3) sem mudar o mapa padrão | unitários do `input_map`; o mapa padrão produz a mesma sequência de entradas que hoje (roteiro físico × roteiro lógico) |
| 4.2 | Zonas mortas e curvas analógicas por eixo (linear, quadrática), no `controles.toml`, aplicadas **antes** da entrada gravada | unitários das curvas; com zona morta 0 e curva linear, igual à de hoje |
| 4.3 | Perfis por GUID: criar, copiar e escolher pelo menu, com o `[perfis."<guid>"]` atual | troca de perfil ao reconectar o mesmo GUID (roteiro físico) |
| 4.4 | Remapeamento de ações no menu da Remastered (e o mesmo arquivo na Original, sem menu novo nela) | cada ação remapeável e persistida; um conflito de botão avisado |
| 4.5 | Mouse: a câmera Livre da Remastered (sensibilidade, inversão); na Original, nada | determinismo com o mouse parado; estado por passo igual sem o mouse |
| 4.6 | Ícones por dispositivo (Xbox, PlayStation, teclado), trocados na hora pelo último usado | troca na hora, sem reiniciar |
| 4.7 | Testes de equivalência: o mesmo roteiro lógico pelo mapa padrão, por um mapa remapeado e por um perfil GUID dá o mesmo estado por passo | `test_controles`, `test_pd_nativo` e um caso novo de equivalência |
| 4.8 | Windows e Linux: o mesmo `controles.toml`; os nomes do SDL3 iguais nos dois | a suíte no Bazzite (mantenedor) |

**Contratos preservados:**
- o mapeamento acontece **antes** da entrada efetiva gravada; o roteiro `indice passo` continua com o mesmo significado (seção 14.2
  do plano), e as gravações existentes reproduzem iguais;
- o formato dos saves e a lógica de entrada original do jogo não mudam;
- conforto não muda velocidade, física nem combate.

### 1.4 Escopo resumido (D4 a)

| Item | Como | Teste |
|---|---|---|
| Remapeamento por ação | uma tabela ação → entradas (teclado, botões, eixos) no `controles.toml`, aplicada **antes** da entrada efetiva gravada (seção 14.2: o roteiro `indice passo` continua com o mesmo significado) | estado por passo igual ao do mapa padrão com o mesmo roteiro lógico |
| Mouse | a câmera Livre da Remastered pelo mouse (sensibilidade, inversão); na Original, nada | nenhum efeito na Original; a sensibilidade não muda velocidade, física nem combate |
| Ícones por dispositivo | Xbox, PlayStation e teclado, trocados na hora pelo último dispositivo usado | troca na hora, sem reiniciar |
| Zonas mortas e sensibilidade | por eixo, persistidas | o roteiro do controle virtual com zona morta igual ao sem ela |
| Janela, tela cheia, sem bordas e vários monitores | o que já existe no N9.5 fase 5, com o monitor escolhido persistido | 1 e 2 monitores (o mantenedor) |
| Cursor e foco | `app/cursor_policy.h` (regra de 2026-10-23) | as suítes de janela |
| Escala da interface | 1080p, 1440p, 4K e ultrawide (o 21:9 continua não habilitado, 5D) | capturas em cada resolução |

**Fora:** a vibração (seção 1.1), as macros e qualquer ajuste que mude o tempo do jogo.

**Riscos:**
- o mouse na câmera Livre é a única mudança que toca a câmera da Remastered; ela precisa de um teste de determinismo com o mouse
  parado;
- os ícones da PlayStation dependem do nome do dispositivo no SDL3.

## 2. N9.9: cinemáticas, áudio e acessibilidade

- **Filmes substituíveis:** o contrato do N5 já existe (`mods\video`, `docs/MODS-VIDEO.md`). Os filmes do N9.13 entram por
  ele, como mod local do usuário, nunca no pacote. O N9.9 só acrescenta a escolha no menu (original ou remasterizado,
  quando houver) e o teste dos 17 IDs com um pacote sintético.
- **Manifesto dos 16 filmes widescreen** (`mods\video\<pacote>\manifesto.toml`, proposta):
  - por filme: o ID, o arquivo, o SHA-256, o número de quadros, o FPS, a duração, o MD5 do áudio (igual ao original), o
    critério de validação (fade-v5) e a data;
  - o jogo só usa um filme do pacote quando o número de quadros, o FPS e a duração batem com os do original instalado;
    senão, volta para o original e avisa no log (**fallback**);
  - o áudio original é o que toca (o do pacote é o mesmo, conferido pelo MD5); as legendas continuam pelo tempo do original,
    porque a duração é igual;
  - igual no Windows e no Linux (Media Foundation e o decodificador do Linux leem o mesmo MP4 H.264).
- **Distribuição dos filmes:** fora do repositório e das releases (decisão do mantenedor). Só o processo e o manifesto vazio de
  exemplo podem ir para o git.
- **Áudio substituível:** o contrato do N6 (`--dublagem`, `mods\audio`); volumes separados de música, efeitos e falas, que já
  existem; falta a prova com um pacote sintético de música.
- **Acessibilidade:**
  - nas legendas: tamanho, contorno, fundo e contraste;
  - escala do HUD;
  - redução de flashes, por um limite de variação de brilho no pós, que fica desligado por padrão;
  - identificação de falantes, só onde o texto do jogo comprova quem fala.
- **Aceitação:** igual à do plano: duração e eventos dos filmes iguais, estado por passo igual com o áudio substituído e cada
  opção persistida.

**Riscos:** a redução de flashes não pode mudar a Original. É uma opção da Remastered no pós, que só existe com o Vulkan.

## 3. N9.10: refinamentos, modo foto e mapa (D5 b, mantenedor 2026-11-14: o mínimo, mais o planejamento do modo foto e do mapa)

**Mínimo:**
- tempo de jogo e estatísticas lidos do estado original, sem mudar os saves;
- capturas de tela (tecla própria, PNG na pasta do usuário);
- mensagens de erro melhores.

**Modo foto (só planejamento):**
- Pausa o jogo pelo mesmo caminho da pausa do N9.2 e libera a câmera Livre, com raio limitado à sala.
- Esconde o HUD pela marca cena/HUD do N9.5. Ajusta a exposição pelos parâmetros do pós do N9.8. Captura em resolução
  interna maior.
- Não pode mudar o estado do jogo. Prova: o estado por passo antes e depois de entrar e sair igual.
- Depende do N9.5 (marca do HUD) e do N9.8 (pós).
- Risco: as salas com geometria só na direção da câmera original mostram "buracos" de fundo; o raio limitado reduz isso.

**Mapa das áreas visitadas (avaliação; só planejamento):**
- O jogo não tem mapa próprio. Um mapa automático precisaria das salas visitadas (o estado já tem a sala atual) e de uma
  planta de cada sala.
- A planta pode sair da geometria da IR, pela projeção de cima dos polígonos do chão, sem arte nova.
- Riscos:
  - revelar salas ainda não visitadas (fica proibido: só as visitadas);
  - o custo de extrair a planta.
- Proposta: um protótipo só de validação numa área antes de decidir.

## 4. N9.11: desempenho e compatibilidade (D6 a, mantenedor 2026-11-14: Intel pendente até existir hardware)

- **Matriz:** RTX 5080 e AMD integrada, as duas disponíveis. A Intel fica marcada como pendente de hardware, não como aprovada
  nem como falha.
- **Itens herdados:**
  - a AMD integrada sem sustentar 1080 com os efeitos (sombras aprimoradas 26,0 → 53,5 ms; o AO);
  - o diagnóstico do desempenho no Linux (P-L1), que fica sem números até a medição;
  - a constante de VRAM (seção 12 do plano, N9.12).
- **Ferramentas que já existem:** `n95_desempenho.py` (janela, p50/p99), `post_cost_4k`, telemetria opt-in.
- **Benchmarks e memória:** a matriz do `n95_desempenho` em 1080p, 1440p e 4K, com o pico de RAM e de VRAM por perfil (a
  estimativa de VRAM do N9.12 é conferida contra essas medidas). Windows e Linux com o mesmo roteiro.
- **Aceitação:**
  - igual à do plano, com a Intel registrada à parte;
  - a campanha completa (gate) com uma única gravação, depois do N9.11.

## 5. N9.12: pendências técnicas (estabilização)

Lista de entrada, a do plano mais o que surgiu desde então:

1. A estimativa de VRAM (96 + 4 + 4 B/px contra ~268 B/px medido no 4:3) com os alvos do N9.8 (pós, máscara, área). É o item
   mais concreto: a medida já existe e falta a fórmula.
2. A árvore integrada: o master com a N9.8 inteira e a D2, compilada e com as baterias H, R, P e J.
3. A AMD integrada: os perfis automáticos que desligam o que não cabe (sem mudar o que o jogador escolheu).
4. HUD: arma, munição, mira e GAME OVER sem identificação; o `kHudInvW` empírico (8.4).
5. Abertos do N9.7: a translúcida por ordem × software; a proposta do controle virtual (seção 11.3), à espera de decisão.
6. Do N9.8:
   - mipmaps sem alfa pré-multiplicado;
   - cache de texturas HD sem descarte por orçamento;
   - translúcidas 3D sobre o vazio sem os efeitos do pós.
7. Linux/Bazzite: o pacote e a bateria do N9.5 em diante.
8. O `boot_trace` sem os snapshots do Flycast (registrado, sem bloquear).
9. As conferências visuais pendentes com o mantenedor (N9.2, N9.5–N9.8) e a regressão das baterias do N9.5 em diante na árvore
   integrada.

**Aceitação:** igual à do plano (cada risco resolvido ou aceito com evidência; as baterias sem falhas na árvore integrada).

## 6. O que fica para o mantenedor decidir antes de cada etapa

| Etapa | Decisão |
|---|---|
| N9.4 | ~~a vibração~~ decidida em 2026-11-15 (fora); a autorização para começar a 4.0 depois da 8.5 |
| N9.9 | a redução de flashes ligada ou desligada por padrão na Remastered (proposta: desligada) |
| N9.10 | depois do planejamento: implementar o modo foto? fazer o protótipo do mapa? |
| N9.11 | a meta de p99 por perfil na AMD integrada |
| N9.12 | a ordem dos itens (proposta: a VRAM primeiro) |
