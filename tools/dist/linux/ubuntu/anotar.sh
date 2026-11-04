#!/usr/bin/env bash
# N9-L: registra o resultado de um item da matriz (docs/N9-L-ROTEIRO-UBUNTU.md, secao "Matriz") em evidencias/resultados.tsv.
#   anotar.sh <item> <ok|falha|parcial|nao-feito|nao-se-aplica> "o que foi visto"
# Ex.: anotar.sh A7.1 ok "RADV, AMD Radeon Graphics (RADV RAPHAEL_MENDOCINO), janela e tela cheia sem falhas"
# Pode anotar o mesmo item de novo: vale o ultimo, e o historico fica.
source "$(dirname "$0")/comum.sh"
anotar "$@"
