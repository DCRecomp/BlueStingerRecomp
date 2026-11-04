#!/usr/bin/env bash
# N9-L: funcoes comuns da sessao de validacao em Ubuntu nativo (docs/N9-L-ROTEIRO-UBUNTU.md). Carregado pelos outros
# scripts desta pasta com "source"; nao executa nada sozinho.
#
# Layout (tudo no disco de troca, montado pelo Ubuntu em /media/<usuario>/BS-TROCA):
#   publico/      pacote do Builder e estas ferramentas (podem ser publicados)
#   privado/      CDI, dados e C++ gerado do Windows, mods e gravacoes do usuario: NUNCA publicar nem anexar
#   trabalho.img  sistema ext4 num arquivo (preparar_sessao.sh), montado em /media/<usuario>/BS-TRABALHO
#   evidencias/   o que volta para o projeto (sem dados do jogo, sem dados pessoais)

# Pasta de troca: a que contem este script (publico/ferramentas/ubuntu -> sobe tres niveis)
BS_FERRAMENTAS=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
BS_TROCA=${BS_TROCA:-$(cd "$BS_FERRAMENTAS/../../.." && pwd -P)}
BS_PUBLICO="$BS_TROCA/publico"
BS_PRIVADO="$BS_TROCA/privado"
BS_EVID="$BS_TROCA/evidencias"
BS_IMG="$BS_TROCA/trabalho.img"
# onde o ext4 de trabalho esta montado (pelo rotulo); BS_TRAB=<pasta> usa outra pasta (ex.: sem disco, na RAM do Live)
BS_TRAB=${BS_TRAB:-$(findmnt -n -o TARGET -S LABEL=BS-TRABALHO 2>/dev/null | head -1)}
BS_TRAB=${BS_TRAB:-/media/$USER/BS-TRABALHO}
BS_JOGO_DIR=${BS_JOGO_DIR:-$BS_TRAB/Jogos/Blue Stinger}     # --out do Builder (com espaco de proposito)
BS_JOGO="$BS_JOGO_DIR/BlueStinger"                           # pasta do jogo gerado
BS_EXE="$BS_JOGO/BlueStinger"
BS_BUILDER=${BS_BUILDER:-$HOME/.local/bin/bs-builder}

STDBUF=$(command -v stdbuf || echo stdbuf)   # absoluto: os testes mudam o PATH do jogo (mod sem ffmpeg)

agora() { date '+%Y-%m-%d %H:%M:%S'; }
aviso() { printf '\n*** %s\n' "$*" >&2; }
falha() { printf '\nERRO: %s\n' "$*" >&2; exit 1; }

# Tira do texto o que identifica a pessoa ou a maquina: a pasta pessoal, o nome de usuario em caminhos, enderecos MAC
# (controles e fones Bluetooth aparecem com eles), UUIDs e numeros de serie. O nome "ubuntu" do usuario do Live so e
# trocado dentro de caminhos, para nao estragar versoes de pacote como "1ubuntu2".
#   O nome da maquina so e trocado quando nao e o padrao do Live ("ubuntu"), que nao identifica ninguem. O GUID que o SDL da
#   ao controle ("GUID <32 hex>" no log do jogo) fica: e o do modelo (barramento, fabricante, produto, versao, driver), igual
#   para todo controle do mesmo modelo, e o A9 precisa dele. Os outros 32 hex seguidos (machine-id, cookies) saem.
BS_MAQUINA=$(hostname 2>/dev/null || cat /etc/hostname 2>/dev/null || echo "")
privacidade() {
    local h=${HOME%/} u=${USER:-x} m=$BS_MAQUINA
    local maq=()
    [ -n "$m" ] && [ "$m" != ubuntu ] && [ ${#m} -ge 3 ] && maq=(-e "s/\b${m//./\\.}\b/<maquina>/g")
    sed -u -E -e "s#${h//#/\\#}#~#g" \
           -e "s#/home/${u}(/|\b)#/home/<usuario>\1#g" -e "s#/media/${u}/#/media/<usuario>/#g" \
           -e "s#/run/user/[0-9]+#/run/user/<uid>#g" \
           -e "s/\b${u}@[A-Za-z0-9._-]+/<usuario>@<maquina>/g" \
           "${maq[@]}" \
           -e 's/((Host Name|Nome do host|Hostname|hostname)[ ]*[:=][ ]*).*/\1<maquina>/' \
           -e 's/((Cookie|cookie)[ ]*[:=][ ]*).*/\1<removido>/' \
           -e 's/([0-9A-Fa-f]{2}[:_-]){5}[0-9A-Fa-f]{2}/<mac>/g' \
           -e 's/[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}/<uuid>/g' \
           -e 's/(GUID )([0-9a-fA-F]{32})/\1Z\2/g' -e 's/\b[0-9a-fA-F]{32}\b/<guid>/g' -e 's/(GUID )Z([0-9a-fA-F]{32})/\1\2/g' \
           -e 's/((Serial|serial|SERIAL|Uniq|uniq)[ _a-zA-Z.]*[=:] *)[^ ]+/\1<removido>/g'
}

# O que ainda identificaria a pessoa depois do filtro: a varredura final recusa o pacote se achar algo daqui
identificadores_restantes() {  # identificadores_restantes <pasta>
    local u=${USER:-x} m=$BS_MAQUINA
    local pad="/home/${u}/|/media/${u}/|\b${u}@|([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}"
    [ -n "$m" ] && [ "$m" != ubuntu ] && [ ${#m} -ge 3 ] && pad="$pad|\b${m//./\\.}\b"
    grep -rIlE "$pad" "$1" --exclude='*.tar.gz' 2>/dev/null
}

# Anotacao de um criterio: anotar <item> <ok|falha|parcial|nao-feito|nao-se-aplica> "o que foi visto"
anotar() {
    [ $# -ge 2 ] || falha "uso: anotar <item> <ok|falha|parcial|nao-feito|nao-se-aplica> \"comentario\""
    case "$2" in ok|falha|parcial|nao-feito|nao-se-aplica) ;; *) falha "resultado invalido: $2" ;; esac
    mkdir -p "$BS_EVID"
    [ -f "$BS_EVID/resultados.tsv" ] || printf 'quando\titem\tresultado\tcomentario\n' > "$BS_EVID/resultados.tsv"
    printf '%s\t%s\t%s\t%s\n' "$(agora)" "$1" "$2" "$(printf '%s' "${3:-}" | tr '\t\n' '  ' | privacidade)" >> "$BS_EVID/resultados.tsv"
    echo "anotado: $1 = $2"
}

# Pergunta s/n ao operador; devolve 0 para sim
pergunta() {
    local r
    read -r -p "$* [s/N] " r || return 1
    [[ "$r" =~ ^[sSyY] ]]
}

# Pede autorizacao explicita antes de um comando com sudo; mostra o comando exato
privilegiado() {
    printf '\nComando com privilegio de administrador:\n    sudo'; printf ' %q' "$@"; printf '\n'
    if pergunta "Autoriza executar este comando agora?"; then sudo "$@"; else echo "(nao executado)"; return 99; fi
}

sha() { sha256sum -- "$1" 2>/dev/null | cut -c1-64; }
