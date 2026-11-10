# Telemetria de desempenho (N9.5 fase 5C)

Decisão do mantenedor (D5-3, 2026-11-10): a telemetria existe no executável de produto, **desligada por padrão**, para medir o
jogo como o jogador o roda. Código: `native/platform/telemetry.{h,cpp}`; ferramenta da matriz: `tools/dev/n95_desempenho.py`.

## Como ligar

| Variável | Valor | Efeito |
|---|---|---|
| `BS_TELEMETRIA` | caminho de um arquivo `.csv` que **não existe** | liga; grava uma linha por segundo e o resumo no fim |
| `BS_TELEMETRIA_DE` | VBlank | a medição começa nesse VBlank (o boot e o título ficam de fora) |
| `BS_TELEMETRIA_ATE` | VBlank | a medição termina nesse VBlank: o resumo sai ali e nada mais é gravado |

Sem `BS_TELEMETRIA`, nada muda no jogo: cada ponto de medição custa a leitura de um `bool`, sem relógio, sem alocação, sem
consulta na GPU, sem extensão Vulkan a mais.

## Regras (o que a telemetria nunca faz)

- **Somente leitura:** nada do jogo, do render, das configurações ou dos saves muda. Nenhuma sincronização nova entre CPU e
  GPU: o render do Vulkan já espera a cerca de cada quadro, e os tempos de GPU são lidos depois dela.
- **Nada pela rede.** O destino tem de estar num volume local: a pasta é resolvida (junções, links simbólicos e unidades
  mapeadas são seguidos) e um volume remoto (caminho UNC, unidade de rede; no Linux NFS, SMB/CIFS, AFS, 9P e Ceph) é recusado.
  Depois de criado, o volume do arquivo **aberto** é conferido de novo pelo handle (a pasta pode mudar entre a consulta e a
  abertura): num volume remoto nada é gravado; no Windows o arquivo é apagado pelo próprio handle; no Linux fica vazio (nada
  é apagado por caminho).
- **Nunca sobrescreve:** o arquivo é criado de forma atômica só se não existir (`CREATE_NEW` / `O_EXCL`); a pasta tem de
  existir (nada é criado). Recusado, o jogo segue sem telemetria e o log diz o motivo (`[telemetria] recusada (...)`).
- **Nenhum dado pessoal:** o arquivo tem números, o nome da GPU e a configuração da imagem (altura interna, proporção).
  Nenhum caminho, nome de usuário, nome de arquivo do jogo ou dado do save. O log mostra só o nome do arquivo.
- **O fim:** o jogo grava o resumo depois de parar as próprias threads; o `atexit` é só um recurso que nunca bloqueia.

## O arquivo

Linhas `#` no início dizem o que é medido. Uma linha por segundo de relógio (`na` = indisponível nesta execução):

| Coluna | O que é | Origem |
|---|---|---|
| `quadros`, `fps` | quadros apresentados no segundo | **medido**: as apresentações bem-sucedidas com a janela visível; sem janela, `na` |
| `ft_p50_ms` … `ft_max_ms` | frametime do segundo (percentis por posição, sem interpolação) | **medido**: relógio monotônico entre duas apresentações vistas seguidas; uma volta sem quadro visto (janela minimizada, apresentação falha, o começo do trecho) recomeça a base, e o tempo dela não vira frametime |
| `acima_16_7_pct`, `acima_25_pct` | fração dos quadros acima de 16,7 ms e de 25 ms | **medido**. Com V-Sync a 60 Hz, o frametime oscila em torno de 16,67 ms e a primeira coluna marca quase metade; a de 25 ms é a de quadro perdido |
| `vblanks` | VBlanks do jogo no segundo | **medido**. Na janela, abaixo de 60 o jogo ficou mais lento que o tempo real (o render não acompanhou): a janela continua a apresentar a 60 Hz repetindo o último quadro, então o `fps` sozinho não mostra a lentidão; olhe esta coluna e a `acima_25_pct`. Sem janela, o jogo corre o mais rápido que pode (acima de 60) |
| `cpu_jogo_pct`, `cpu_janela_pct` | tempo de CPU da thread do jogo (SH-4 recompilado e o render) e da janela (apresentação), em % de um núcleo | **medido** pelo sistema (`GetThreadTimes`, `CLOCK_THREAD_CPUTIME_ID`) |
| `renders_gpu`, `gpu_*_ms` | média por render: envios de textura, raster (passes A e B e os volumes), resolve dos translúcidos, saída (slot, redução, leitura de volta) e o total | **medido** por timestamps do Vulkan; sem timestamps na fila, `na` |
| `mem_<categoria>_mib` | memória local da GPU alocada pelo jogo: alvos, slots, texturas, buffers por quadro, pós-processamento, apresentação, outros | **medido**: o tamanho que o driver pediu para cada alocação; o que o driver usa por fora delas não entra |
| `mem_cpu_mib` | as alocações do Vulkan na memória da CPU (envios, leitura de volta) | **medido** |
| `vram_uso_mib`, `vram_orcamento_mib` | uso e orçamento da memória local do processo, segundo o driver | **medido pelo driver** (`VK_EXT_memory_budget`, ligada só com a telemetria); sem a extensão, `na` |
| `processo_mib` | memória privada do processo (a verificação de vazamento ao longo de uma rota) | **medido** pelo sistema |
| `altura`, `proporcao` | a resolução interna e a proporção em uso | contexto |

O resumo (`# resumo:`) traz o dispositivo, os percentis da execução inteira, a GPU média e o p99 por render, a CPU média, os
picos de memória por categoria e a memória do processo no início, no fim e no pico. Um trecho pedido que não chegou
(`BS_TELEMETRIA_DE` além do fim) sai como "não alcançado", sem números.

**Estimado:** nada no arquivo. As estimativas de memória (a fórmula de 96 B por pixel do limite da resolução) ficam nos
relatórios, sempre marcadas como estimativa.

## A matriz

`python tools/dev/n95_desempenho.py --exe <produto> --data <data> --cartao <cartão de teste> --saida tmp/<pasta> [--janela]
[--trechos corredor,cais,exterior,combate] [--configs 4:3@480,...] [--gpu <nome>]`: cada caso numa pasta própria, no modo
espectador (o roteiro conduz; nenhuma entrada global), com o trecho limitado por `BS_TELEMETRIA_DE`/`_ATE`. Resultados de
2026-11-10 em `docs/N9-5-AUDITORIA.md`, seção 16.3.
