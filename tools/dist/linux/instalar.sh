#!/usr/bin/env bash
# Blue Stinger Builder para Linux: instalacao do pacote (N9-L L5, D-L5). Sem root, sem Wine, sem pacotes do sistema.
#
#   ./instalar.sh [--destino <pasta>] [--atalho]      instala ou atualiza (padrao: ~/.local/share/BlueStinger-Builder)
#   ./instalar.sh --verificar [--destino <pasta>]     confere a instalacao contra o SHA256SUMS.txt dela
#   ./instalar.sh --ajuda
#
# Regras:
# - So escreve no destino e em pastas temporarias ao lado dele (<destino>.novo-*, <destino>.troca, <destino>.anterior).
#   --atalho e o unico passo fora disso: um link em ~/.local/bin/bs-builder (nunca substitui um arquivo que nao e nosso).
# - O pacote e conferido (SHA256SUMS.txt) antes de qualquer mudanca; a copia nova e conferida de novo no destino.
# - A troca e por rename: uma interrupcao deixa a instalacao anterior ou a nova, nunca uma mistura. Uma troca
#   interrompida no meio e desfeita na proxima execucao.
# - Atualizacao: arquivos que voce acrescentou (fora da lista do pacote anterior) passam para a nova versao; arquivos do
#   pacote que voce alterou ficam guardados como <arquivo>.usuario-<data> na nova versao, e a lista sai no fim. Nada
#   seu e apagado em silencio.
# - Jogos gerados, saves (~/.local/share/DC-Recomp), configuracoes (~/.config/BlueStingerRecomp) e o cache de compilacao
#   (~/.cache/BlueStingerRecomp) nao sao tocados.
# - Nada de entrada do usuario vira comando: caminhos so como argumentos entre aspas.
set -euo pipefail
umask 022
LC_ALL=C
export LC_ALL

MARCA=".bs-builder-instalacao"   # arquivo que identifica uma pasta instalada por este script
pacote=$(cd "$(dirname "$0")" && pwd -P)
destino=""
verificar=0
atalho=0

falha() { printf 'ERRO: %s\n' "$*" >&2; exit 1; }
info() { printf '%s\n' "$*"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --destino) [ $# -ge 2 ] || falha "--destino precisa de uma pasta"; destino=$2; shift 2 ;;
        --verificar) verificar=1; shift ;;
        --atalho) atalho=1; shift ;;
        --ajuda|-h|--help) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) falha "opcao desconhecida: $1 (veja --ajuda)" ;;
    esac
done

for ferramenta in sha256sum cp mv mkdir rm find sort mktemp date readlink; do
    command -v "$ferramenta" > /dev/null 2>&1 || falha "falta a ferramenta '$ferramenta' no sistema (coreutils)"
done

if [ -z "$destino" ]; then
    base=${XDG_DATA_HOME:-}
    case "$base" in /*) ;; *) [ -n "${HOME:-}" ] || falha "HOME nao definido; use --destino"; base=$HOME/.local/share ;; esac
    destino=$base/BlueStinger-Builder
fi
case "$destino" in /*) ;; *) destino=$(pwd -P)/$destino ;; esac
destino=${destino%/}
[ -n "$destino" ] && [ "$destino" != "/" ] || falha "destino invalido"
[ "$destino" != "${HOME:-/nao-existe}" ] || falha "o destino nao pode ser a sua pasta pessoal; escolha uma subpasta"
case "$destino" in *";"*|*"\\"*|*'"'*|*'$'*) falha "o destino tem ; \\ \" ou \$, que o CMake nao aceita em caminhos (o Builder compila de dentro dele); escolha outra pasta" ;; esac
pai=$(dirname "$destino")
nome=$(basename "$destino")

# confere uma pasta contra o SHA256SUMS.txt dela (linhas "<hash>  ./<arquivo>"); saida: lista de problemas
conferir() {
    local dir=$1
    [ -f "$dir/SHA256SUMS.txt" ] || { echo "falta SHA256SUMS.txt"; return 1; }
    (cd "$dir" && sha256sum --quiet --strict -c SHA256SUMS.txt 2>&1) || return 1
}
# os caminhos ("./...") do SHA256SUMS.txt de uma pasta, um por linha (para comparacao exata com grep -x)
lista() { cut -c67- "$1/SHA256SUMS.txt"; }

if [ "$verificar" = 1 ]; then
    [ -f "$destino/$MARCA" ] || falha "$destino nao e uma instalacao do Blue Stinger Builder"
    if saida=$(conferir "$destino"); then
        # (here-string, nao pipe: com pipefail, o grep -q que sai cedo faria o produtor falhar com SIGPIPE)
        pacote_lista=$(lista "$destino")
        extras=$(cd "$destino" && find . -type f ! -name SHA256SUMS.txt ! -name "$MARCA" -print | sort |
                 while IFS= read -r f; do grep -qxF -- "$f" <<< "$pacote_lista" || printf '%s\n' "$f"; done)
        info "instalacao integra: $destino ($(grep -c '' "$destino/SHA256SUMS.txt") arquivos conferidos)"
        [ -z "$extras" ] || info "arquivos acrescentados por voce (fora do pacote):" "$extras"
        [ -x "$destino/bs-builder" ] || falha "bs-builder sem permissao de execucao"
        exit 0
    fi
    printf '%s\n' "$saida" >&2
    falha "a instalacao em $destino nao confere com a lista do pacote (arquivos alterados ou faltando acima)"
fi

[ "$(id -u)" != 0 ] || info "aviso: rodando como root; a instalacao ficara com dono root (o Builder nao precisa de root)"

info "conferindo o pacote em $pacote"
saida=$(conferir "$pacote") || { printf '%s\n' "$saida" >&2; falha "o pacote esta incompleto ou alterado; baixe e extraia de novo"; }
[ -x "$pacote/bs-builder" ] || falha "o pacote nao tem o bs-builder executavel"
case "$destino/" in "$pacote"/*) falha "o destino nao pode ficar dentro do pacote" ;; esac
case "$pacote/" in "$destino"/*) falha "o pacote nao pode ficar dentro do destino (extraia-o em outra pasta)" ;; esac

mkdir -p "$pai" || falha "nao foi possivel criar $pai"
[ -w "$pai" ] || falha "sem permissao de gravacao em $pai"

# troca interrompida numa execucao anterior: a instalacao anterior volta para o lugar
if [ ! -e "$destino" ] && [ -f "$pai/$nome.troca/$MARCA" ]; then
    info "desfazendo uma troca interrompida: $pai/$nome.troca volta para $destino"
    mv -T "$pai/$nome.troca" "$destino"
fi
if [ -e "$destino" ] && [ -f "$pai/$nome.troca/$MARCA" ]; then rm -rf -- "$pai/$nome.troca"; fi
# copias novas incompletas de execucoes interrompidas (so as nossas)
for d in "$pai/$nome".novo-*; do
    if [ -d "$d" ] && { [ -f "$d/.bs-builder-copia" ] || [ -f "$d/$MARCA" ]; }; then rm -rf -- "$d"; fi
done

if [ -e "$destino" ]; then
    [ -d "$destino" ] || falha "$destino existe e nao e uma pasta"
    if [ ! -f "$destino/$MARCA" ] && [ -n "$(ls -A "$destino" 2>/dev/null)" ]; then
        falha "$destino ja existe, nao esta vazio e nao foi instalado por este script; escolha outra pasta com --destino"
    fi
fi

novo=$(mktemp -d "$pai/$nome.novo-XXXXXX")
# sempre 0: com set -e, uma falha dentro da armadilha de saida trocaria o codigo de saida (130 do Ctrl+C virava 1)
limpar() { if [ -n "${novo:-}" ] && [ -d "$novo" ]; then rm -rf -- "$novo"; fi; return 0; }
trap 'limpar' EXIT
trap 'limpar; echo "interrompido: nada foi trocado" >&2; exit 130' INT TERM HUP
: > "$novo/.bs-builder-copia"

info "copiando para $novo"
cp -a "$pacote/." "$novo/"
saida=$(conferir "$novo") || { printf '%s\n' "$saida" >&2; falha "a copia no destino nao confere (disco cheio ou com defeito?)"; }
# a pasta aceita executar programas? (montagem noexec quebraria o Builder so na hora de compilar)
set +e
"$novo/bs-builder" > /dev/null 2>&1
rc=$?
set -e
[ "$rc" = 2 ] || falha "o bs-builder nao executa em $pai (codigo $rc). A pasta pode estar montada sem permissao de execucao (noexec); use --destino em outra pasta"

guardados=()
if [ -f "$destino/$MARCA" ] && [ -f "$destino/SHA256SUMS.txt" ]; then
    carimbo=$(date +%Y%m%d-%H%M%S)
    # arquivos do pacote anterior alterados por voce: guardados na nova versao
    while IFS= read -r linha; do
        hash=${linha%%  *}; arq=${linha#*  }
        [ -f "$destino/$arq" ] || continue
        atual=$(sha256sum "$destino/$arq" | cut -d' ' -f1)
        if [ "$atual" != "$hash" ]; then
            mkdir -p "$(dirname "$novo/$arq")"
            cp -a "$destino/$arq" "$novo/$arq.usuario-$carimbo"
            guardados+=("$arq.usuario-$carimbo")
        fi
    done < "$destino/SHA256SUMS.txt"
    # arquivos acrescentados por voce: vao junto (sem substituir os do pacote novo)
    anteriores=$(lista "$destino")
    while IFS= read -r -d '' f; do
        rel=${f#./}
        if [ "$rel" = "SHA256SUMS.txt" ] || [ "$rel" = "$MARCA" ]; then continue; fi
        grep -qxF -- "./$rel" <<< "$anteriores" && continue
        if [ -e "$novo/$rel" ]; then
            cp -a "$destino/$rel" "$novo/$rel.usuario-$carimbo"; guardados+=("$rel.usuario-$carimbo")
        else
            mkdir -p "$(dirname "$novo/$rel")"; cp -a "$destino/$rel" "$novo/$rel"; guardados+=("$rel")
        fi
    done < <(cd "$destino" && find . -type f -print0)
fi
date '+%Y-%m-%d %H:%M:%S' > "$novo/$MARCA"
head -1 "$novo/VERSAO.txt" >> "$novo/$MARCA" 2>/dev/null || true

# so testes (test_builder_linux.py, B14): o processo morre aqui sem limpar nada, como numa queda de energia
parada() { [ "${BS_INSTALAR_TESTE_PARADA:-}" = "$1" ] && kill -9 $$; return 0; }
parada copia

# troca: a anterior vira .troca, a nova entra, a anterior vira .anterior (a .anterior mais antiga sai)
trap '' INT TERM HUP
if [ -e "$destino" ]; then
    mv -T "$destino" "$pai/$nome.troca"
fi
parada troca
rm -f -- "$novo/.bs-builder-copia"
if ! mv -T "$novo" "$destino"; then
    [ -e "$pai/$nome.troca" ] && mv -T "$pai/$nome.troca" "$destino"
    falha "nao foi possivel trocar a instalacao; a anterior foi mantida"
fi
novo=""
if [ -e "$pai/$nome.troca" ]; then
    if [ -f "$pai/$nome.anterior/$MARCA" ]; then rm -rf -- "$pai/$nome.anterior"; fi
    if [ -e "$pai/$nome.anterior" ]; then
        info "aviso: $pai/$nome.anterior nao e nosso; a versao anterior fica em $pai/$nome.troca"
    else
        mv -T "$pai/$nome.troca" "$pai/$nome.anterior"
    fi
fi
trap - INT TERM HUP EXIT

if [ "$atalho" = 1 ]; then
    bin=${HOME:?}/.local/bin
    mkdir -p "$bin"
    alvo=$bin/bs-builder
    if [ -L "$alvo" ] || [ ! -e "$alvo" ]; then
        if [ -L "$alvo" ] && [ "$(readlink "$alvo")" != "$destino/bs-builder" ] && [ -e "$alvo" ]; then
            info "aviso: $alvo aponta para outro lugar; nao foi mudado"
        else
            ln -sfn "$destino/bs-builder" "$alvo"; info "atalho: $alvo"
        fi
    else
        info "aviso: $alvo ja existe e nao e um link; nao foi mudado"
    fi
fi

info "instalado: $destino"
[ -d "$pai/$nome.anterior" ] && info "versao anterior: $pai/$nome.anterior (para voltar, renomeie as pastas)"
if [ ${#guardados[@]} -gt 0 ]; then
    info "arquivos seus preservados na nova versao:"
    printf '  %s\n' "${guardados[@]}"
fi
info ""
info "Para gerar o jogo a partir do seu CDI:"
info "  \"$destino/bs-builder\" --disc \"/caminho/Blue Stinger.cdi\" --out \"\$HOME/Jogos/BlueStinger\""
