# N9.13: remasterização dos filmes com IA (planejamento)

Pedido do mantenedor (2026-11-13): "Adiciona e planeja uma etapa para fazer a remasterização dos vídeos. Quero transformar
eles em widescreen usando IA e outra versão melhorando a qualidade do 3D do vídeo, deixando uma movimentação mais moderna e
gráfica, e widescreen."

Este documento só planeja: nenhum código, nenhum modelo baixado, nenhuma fase começa sem autorização.

## 1. Ponto de partida

| Fato | Onde | Consequência |
|---|---|---|
| 16 filmes (17 IDs, o OPENING ausente no disco), MPEG-1 **320x240 a 30 fps**, áudio ADX | `docs/N5-PLANO.md`, `MODS-VIDEO.md` | a fonte é pequena: chegar a 1080p é ampliar 4,5x; o widescreen tem de inventar 1/3 a mais de imagem nas laterais |
| O filme termina quando o arquivo termina; o jogo só espera o fim, o pulo ou a interrupção | `MODS-VIDEO.md` (modo modificado) | um filme novo com outra duração não quebra o jogo, mas a seção 14.2 do N9 exige o fim no **mesmo passo lógico**: as versões novas guardam a duração e a contagem de quadros a 30 fps (ou o equivalente exato a 60 fps) |
| Filmes de mod por ID, em `mods\video` ou num pacote (`mods\<pacote>\video`, como `legendas-pt-br`), qualquer resolução e taxa | N5, N9.9 | o jogo já toca uma versão nova sem recompilar; falta escolher entre versões e mostrar em 16:9 |
| Na Remastered 16:9, o filme é imagem da CPU na área 4:3 com pilares | `docs/N9-5-AUDITORIA.md` (tabela da seção 2, linha "Filmes") | um filme 16:9 precisa de outro caminho na apresentação para ocupar a tela |
| Legendas proporcionais à altura do vídeo, centradas embaixo | `MODS-VIDEO.md` | valem igual num vídeo 16:9 maior; a posição tem de ser conferida |
| Regra do projeto: arte, áudio e vídeo do jogo nunca no repositório nem em pacote público | `CLAUDE.md`, `LEGAL.md`, N9 seção 1.2 | **os filmes remasterizados são derivados do jogo**: não vão no git nem na release. O repositório leva só o código do processo, e os filmes são gerados na máquina de quem tem o CDI |
| Regra de dependências (2026-11-03) | `docs/N9-L-LICENCAS.md` | nada pago, nada com licença não comercial ou restritiva: isso exclui APIs pagas de vídeo e vários modelos de pesquisa com licença NC |

## 2. As duas versões pedidas

| | Versão A: **Widescreen** | Versão B: **Modernizada** |
|---|---|---|
| O que muda | só o enquadramento e a nitidez: o 4:3 vira 16:9 com as laterais geradas, ampliado a 1080p (ou 1440p) | também o visual do 3D: mais detalhe nos modelos, materiais e luz de hoje, movimento mais fluido (60 fps), em 16:9 |
| O que **não** muda | os cortes, a duração, o que acontece em cada quadro, a composição do centro | a história, os cortes, a duração, as falas e o tempo de cada plano (o mesmo roteiro visual, com outra renderização) |
| Técnica (candidatas, a conferir na 13.0) | ampliação por rede de super-resolução de vídeo + extensão das laterais (outpainting de vídeo, com consistência entre quadros) | vídeo para vídeo guiado pela estrutura do original (profundidade, bordas e pose quadro a quadro), sobre a versão A; interpolação de quadros para 60 fps |
| Risco | baixo a médio: laterais inventadas em planos com movimento de câmera rápido; o centro é o original ampliado | **alto**: cintilação entre quadros, rostos e objetos que mudam de forma, texto e logos deformados; custo de GPU muito maior |
| Recuo | o original 4:3 por filme | a versão A, depois o original |

O jogador escolhe na Remastered: **Filmes: original / widescreen / modernizados**, só entre as versões instaladas. A Original
continua sempre com os filmes do disco.

## 3. Onde roda (o ponto mais importante)

Gerar esses filmes custa horas de GPU e dezenas de GB de modelos. Como eles não podem ser distribuídos, alguém precisa
gerá-los a partir do próprio CDI. As opções:

| | Opção | Prós | Contras |
|---|---|---|---|
| R1 | **Ferramenta de desenvolvimento** (`tools/filmes-ia/`, Python): o mantenedor (ou um usuário avançado) gera os pacotes localmente e os instala em `mods\` | sem custo no Builder; permite iterar com qualidade; nada a distribuir | só quem tem GPU forte e paciência; instalação manual dos modelos |
| R2 | **Etapa opcional do Recompiler** ("gerar filmes remasterizados: demorado, precisa de GPU"), com os modelos baixados pelo usuário, sob a licença deles | o jogador comum consegue | o instalador cresce em complexidade, horas de espera, VRAM mínima alta (provavelmente 12 a 24 GB para os modelos de vídeo), suporte a drivers e Linux |
| R3 | Serviço na nuvem | rápido | **recusado**: APIs pagas violam a regra de dependências, e enviar os filmes a terceiros é distribuir conteúdo do jogo |

Recomendação: **R1 primeiro**, com a R2 decidida depois da medida de tempo e qualidade da 13.0/13.2.

## 4. Fases propostas (cada uma com autorização)

| Fase | Conteúdo | Aceitação |
|---|---|---|
| 13.0 | **Investigação:** inventário dos 16 filmes (duração, cortes de cena, movimento de câmera, texto na imagem); levantamento dos modelos abertos de super-resolução, extensão de vídeo, vídeo para vídeo e interpolação, **com a licença conferida no texto de cada um** (código e pesos separados); medida de tempo e VRAM num trecho de 10 s de um filme, local, em `tmp/`; o caminho da apresentação para um filme 16:9 | relatório com a tabela de modelos (licença, VRAM, tempo por minuto de filme) e as amostras locais avaliadas pelo mantenedor; nada publicado |
| 13.1 | **Contrato no jogo:** versões de filme por pacote (`[filme] versao = "widescreen"\|"modernizado"` no `mod.toml`, ou a pasta), a escolha no menu e no `graficos.toml`, o filme 16:9 em tela cheia na Remastered 16:9 (pilares só no 4:3), recuo por filme, legendas na posição certa | pacote de teste **sintético** (barras e contador de quadros, sem arte) nas três versões: duração e passo lógico do fim iguais (`n5_matriz`), estado por passo idêntico, legendas visíveis, a Original intocada |
| 13.2 | **Versão A (widescreen):** o processo em `tools/filmes-ia/`: SFD → quadros (pl_mpeg, já no projeto) → cortes de cena → ampliação → extensão das laterais por plano → codificação (H.264 + AAC no Windows; MPEG-1 + ADX ou o que o Linux toca sem `ffmpeg`), o **áudio original sem mudança** (sincronia) | os 16 filmes gerados localmente: mesma contagem de quadros e duração, sincronia de boca e som no olho e por medida, sem cintilação nas laterais (medida de diferença entre quadros nas laterais × centro); revisão visual do mantenedor filme a filme |
| 13.3 | **Versão B (modernizada):** vídeo para vídeo guiado pela estrutura, sobre a A; interpolação para 60 fps com a duração exata | as mesmas medidas da 13.2, mais a consistência temporal (rostos e objetos estáveis entre quadros) e a fidelidade ao roteiro visual (os mesmos planos e ações); revisão visual do mantenedor; os filmes que não passarem ficam na versão A |
| 13.4 | **Bateria e documentação:** os 16 filmes × 3 versões, nas duas plataformas; o pulo com START; a perda do áudio; o espaço em disco; `docs/MODS-VIDEO.md` com o formato dos pacotes | sem falhas; a Original igual às referências; o C8 quando houver gravação |

## 5. Decisões para o mantenedor

| | Pergunta | Recomendação |
|---|---|---|
| D13-1 | Onde roda: R1 (ferramenta), R2 (Recompiler) ou as duas? | **R1 agora**; a R2 depois da medida da 13.0 e da 13.2 |
| D13-2 | Modelos: só os de licença aberta (MIT, BSD, Apache-2.0 e parecidas, código e pesos), sem os de licença não comercial, sem APIs pagas? | **sim** (a regra de 2026-11-03 já exige); a lista sai da 13.0 com cada licença conferida |
| D13-3 | A versão B pode mudar o visual (modelos, materiais, luz, fluidez) mas não o conteúdo (cortes, duração, ações, falas)? | **sim**; quem decide se um filme passou é a revisão visual do mantenedor |
| D13-4 | Taxa de quadros: a A fica a 30 fps e a B vai a 60 fps (com a duração exata)? | **sim** |
| D13-5 | Padrão no jogo: o original, e as versões só pela escolha no menu (como as texturas HD, D8-5)? | **sim**: instalar o pacote deixa a opção disponível; a escolha é do jogador |
| D13-6 | Ordem: depois do N9.9 (o contrato de filmes substituíveis e o pacote de teste) e antes do N9.11 (validação final)? | **sim**; a 13.0 (só investigação) pode correr em paralelo quando houver espera |

## 6. Riscos

- **Qualidade da IA:** a fonte em 320x240 limita o que a ampliação recupera; a versão B pode não convencer em todos os
  filmes. O critério é a revisão visual, e um filme ruim fica na versão anterior, nunca é aceito para fechar a fase.
- **Custo:** horas de GPU por filme; os modelos de vídeo grandes podem não caber numa GPU de consumo. A 13.0 mede antes de
  qualquer compromisso.
- **Licenças:** muitos modelos de vídeo publicados são de pesquisa, com licença não comercial ou termos de uso restritos nos
  pesos. Cada um é conferido no texto da licença antes de entrar na lista; na dúvida, fica de fora.
- **Distribuição:** os filmes gerados são conteúdo derivado do jogo: nunca no git, na release ou no painel. Só o código do
  processo é publicado.
- **Sincronia:** o áudio e as legendas do original ficam como estão; qualquer mudança de duração é defeito.
- **Linux:** o H.264 no Bazzite depende do `ffmpeg` do sistema (`MODS-VIDEO.md`); a 13.2 escolhe o formato por plataforma.

## 7. Decisão de distribuição (mantenedor, 2026-11-13)

O mantenedor decidiu: "Publicar abertamente, mas escolha alguma plataforma fora do GitHub para enviar o zip do pacote, e o
instalador faz o download na hora." Isso muda, **só para os filmes remasterizados e as texturas HD**, a regra de não
distribuir conteúdo derivado do jogo (como a decisão das legendas, v0.9.7-1). O código continua no GitHub, sem nenhum desses
arquivos. O mantenedor pediu antes um formato disfarçado, para que o conteúdo não fosse reconhecível; isso **não** é feito:
os pacotes vão identificados (nome, versão, manifesto, licença do processo e aviso de que derivam do jogo).

**Plataforma (recomendação):** Hugging Face Hub, num repositório público de "dataset" do projeto: grátis, arquivos grandes
(LFS), endereços diretos e estáveis por versão (`.../resolve/<revisão>/<arquivo>`), CDN rápida. Espelho opcional no Internet
Archive. Os termos de todas essas plataformas proíbem conteúdo sem os direitos: o titular pode pedir a remoção a qualquer
momento, e o instalador tem de funcionar sem o pacote.

**Como o instalador baixa (a implementar, fase 13.1/13.2):**
- O Builder traz um catálogo (`remaster-pacotes.toml`): por pacote, o id, a versão, o tamanho, o SHA-256 de cada arquivo e
  uma lista de endereços (o principal e os espelhos), na revisão fixa.
- Na tela de opções da Remastered: "Filmes remasterizados (widescreen)" e "Texturas HD", desmarcadas por padrão, com o
  tamanho do download. Marcadas, o instalador baixa (HTTPS, retomável, um arquivo por vez), confere o SHA-256 e só então
  extrai para `mods\<pacote>\` na pasta de destino. Falha de rede, hash diferente ou pacote removido: aviso claro, e a
  instalação segue com os originais (nunca bloqueia a instalação).
- O download não manda nada além do pedido do arquivo (sem telemetria; regra do N9.5 5C).
- O desinstalador remove os pacotes baixados.

**Pendências antes do primeiro envio:** a conta e o token de escrita são do mantenedor; o primeiro envio só com a
confirmação dele; o `LEGAL.md` e o `mods-padrao/README.md` registram a decisão e o aviso.
