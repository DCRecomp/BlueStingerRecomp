#!/usr/bin/env bash
# N9-L A8/A11 na sessao em Ubuntu nativo: os filmes no jogo de verdade (janela, GPU, saida de audio reais).
#
#   filmes.sh todos [--perguntar]   os 16 SFD do disco, um por vez, pelo decodificador nativo e o audio do sistema.
#                                    O roteiro passo_newgame_area1 escolhe NEW GAME sozinho; cada filme toca no lugar do
#                                    NEWGAME: o SFD e copiado como mods/video/NEWGAME.MP4 (o Linux escolhe o
#                                    decodificador pelo conteudo, entao e o mesmo caminho do filme original). O NEWGAME
#                                    toca sem mod. Ao fim de cada filme o jogo e fechado. Uns 65 min (cada filme tem
#                                    ~150 s de rota antes). As variaveis do ambiente valem para o jogo: rode com
#                                    BS_VULKAN_GPU="AMD Radeon" bash filmes.sh todos (senao o Vulkan pode ser o llvmpipe).
#                                    --perguntar: depois de cada filme pergunta se imagem e som estavam juntos.
#   filmes.sh um <FILME.SFD> [--perguntar]   um filme so, do mesmo jeito (para repetir um caso).
#   filmes.sh mods                  A11 dos filmes: mod MP4 sem o ffmpeg (toca o original e avisa), com o ffmpeg (toca o
#                                    mod), nome em minusculas, mod vazio e mod truncado (o jogo nao trava).
#
# Grava evidencias/filmes/<caso>.txt e evidencias/filmes/resumo.tsv. As copias em mods/video sao apagadas no fim (e no
# Ctrl+C). Se ja houver um mods/video/NEWGAME.* seu, o script para sem tocar nele. So encerra o jogo que ele abriu.
set -uo pipefail
source "$(dirname "$0")/comum.sh"
modo=${1:-}; perguntar=0; for a in "$@"; do [ "$a" = "--perguntar" ] && perguntar=1; done
roteiro="$BS_PUBLICO/ferramentas/roteiros/passo_newgame_area1.txt"
[ -x "$BS_EXE" ] || falha "sem o jogo em $BS_EXE (rode a1_a2.sh)"
[ -f "$roteiro" ] || falha "sem o roteiro $roteiro"
bsdata="$BS_JOGO/data/disc/BSDATA"; manifesto="$BS_JOGO/data/video/manifest.toml"
modv="$BS_JOGO/mods/video"; mkdir -p "$modv"
d="$BS_EVID/filmes"; mkdir -p "$d"
resumo="$d/resumo.tsv"
[ -f "$resumo" ] || printf 'quando\tcaso\tfilme\tmod\tresultado\tfim (ms logicos)\tfim esperado (ms)\tquadros\tpulados\tsegundos\tobservacoes\n' > "$resumo"
if ls "$modv" 2>/dev/null | grep -qiE '^newgame\.'; then
    falha "ja existe um mod do NEWGAME em $modv: tire-o (ele e seu) e rode de novo"
fi
# so os dois nomes que este script cria (conferido acima que nao havia mod do NEWGAME seu)
limpar() { rm -f "$modv/NEWGAME.MP4" "$modv/newgame.mp4"; }
trap 'limpar; exit 130' INT TERM
trap limpar EXIT
tol=100   # ms de diferenca aceitos entre o fim do filme e o do manifesto (SFD); o MP4 do ffmpeg usa 500

fim_esperado() {  # ms do fim do video no manifesto do Builder
    awk -v f="$1" -F' = ' '/^original/{o=$2; gsub(/"/,"",o)} /^fim_video_100ns/ && o==f {printf "%d", $2/10000; exit}' "$manifesto"
}

# roda o jogo com o roteiro ate o primeiro filme (o do NEWGAME) terminar; depois fecha o jogo
caso() {  # caso <nome> <filme mostrado> <mod: sim|nao> <limite s> [VAR=valor ...]
    local nome=$1 filme=$2 mod=$3 limite=$4; shift 4
    local out="$d/$nome-$(date +%H%M%S).txt" ini pid fim linha res="falha" obs=""
    ultimo_log=$out   # nome unico: rodar de novo (antes e depois do ffmpeg) nao apaga o log anterior
    ini=$(date +%s)
    ( cd "$BS_JOGO" && exec env BS_RELOGIO_FIXO=1700000000 BS_INPUT_SCRIPT="$roteiro" "$@" "$STDBUF" -oL ./BlueStinger --legendas nenhuma ) \
        > >(privacidade > "$out") 2>&1 &
    pid=$!
    fim=""
    while kill -0 "$pid" 2>/dev/null; do
        sleep 1
        if grep -q '^\[video\] NEWGAME' "$out" 2>/dev/null; then
            linha=$(sed -n '/^\[video\] NEWGAME/,$p' "$out" | grep -m1 -E '^\[video\] (fim natural|parado pelo jogo|erro de decodificacao)')
            [ -n "$linha" ] && { fim=$linha; sleep 3; break; }
        fi
        [ $(( $(date +%s) - ini )) -gt "$limite" ] && { obs="limite de ${limite} s"; break; }
    done
    if kill -0 "$pid" 2>/dev/null; then
        kill -TERM "$pid" 2>/dev/null
        for _ in $(seq 10); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
        kill -0 "$pid" 2>/dev/null && { kill -KILL "$pid" 2>/dev/null; obs="$obs; precisou de SIGKILL"; }
    fi
    wait "$pid" 2>/dev/null
    local seg=$(( $(date +%s) - ini ))
    local ms q p esp
    ms=$(sed -nE 's/.*fim natural: ([0-9]+) ms.*/\1/p' <<<"$fim")
    q=$(sed -nE 's/.* ([0-9]+) quadros \(([0-9]+) pulados\).*/\1/p' <<<"$fim")
    p=$(sed -nE 's/.* ([0-9]+) quadros \(([0-9]+) pulados\).*/\2/p' <<<"$fim")
    esp=$(fim_esperado "$filme")
    grep -qE 'dispositivo de audio perdido|recusou o bloco|sem dispositivo de audio' "$out" && obs="$obs; audio perdido ou ausente"
    grep -q 'sem decodificador neste sistema' "$out" && obs="$obs; aviso: sem decodificador"
    grep -qiE 'segmentation|abort|core dumped' "$out" && obs="$obs; QUEDA"
    grep -q 'Vulkan direta: llvmpipe' "$out" && obs="$obs; Vulkan na CPU (llvmpipe): use BS_VULKAN_GPU"
    sed -n '/^\[video\] NEWGAME/p' "$out" | head -1 | grep -q '(mod)' && local tocou_mod=sim || local tocou_mod=nao
    if [[ "$fim" == *"fim natural"* ]] && [ -n "$esp" ] && [ -n "$ms" ] && [ $(( ms > esp ? ms - esp : esp - ms )) -le "$tol" ] \
       && [ "$tocou_mod" = "$mod" ] && [[ "$obs" != *perdido* && "$obs" != *QUEDA* ]]; then
        res=ok
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$(agora)" "$nome" "$filme" "$tocou_mod" "$res" "${ms:--}" "${esp:--}" \
        "${q:--}" "${p:--}" "$seg" "${obs#; }" >> "$resumo"
    echo "$nome: $res (filme $filme, mod tocado: $tocou_mod, fim ${ms:--} ms de ${esp:--} ms, ${q:--} quadros, ${p:--} pulados, ${seg} s) ${obs#; }"
    if [ $perguntar -eq 1 ]; then
        if pergunta "  $filme: imagem, som e sincronia de falas corretos?"; then anotar "A8.filme.$filme" ok "visto e ouvido no hardware"
        else read -r -p "  o que estava errado? " o; anotar "A8.filme.$filme" falha "$o"; fi
    fi
    [ "$res" = ok ]
}

case "$modo" in
todos)
    falhas=0
    mapfile -t sfds < <(awk -F' = ' '/^original/{o=$2; gsub(/"/,"",o)} /^ausente/{o=""} /^fim_video_100ns/ && o!="" {print o}' "$manifesto")
    [ ${#sfds[@]} -eq 16 ] || falha "esperava 16 filmes no manifesto, achei ${#sfds[@]}"
    echo "16 filmes; o NEWGAME primeiro (sem mod), depois os outros no lugar dele."
    caso "nativo-NEWGAME" NEWGAME.SFD nao $(( 600 + 453 )) || falhas=$((falhas+1))
    for f in "${sfds[@]}"; do
        [ "$f" = NEWGAME.SFD ] && continue
        cp -f "$bsdata/$f" "$modv/NEWGAME.MP4" || falha "nao consegui copiar $f"
        lim=$(( 600 + $(fim_esperado "$f") / 1000 * 2 ))
        caso "nativo-${f%.SFD}" "$f" sim "$lim" || falhas=$((falhas+1))
        rm -f "$modv/NEWGAME.MP4"
    done
    anotar "A8.16-filmes-automatico" "$([ $falhas -eq 0 ] && echo ok || echo falha)" "$((16 - falhas))/16 com fim natural no tempo do manifesto (filmes/resumo.tsv)"
    ;;
um)
    f=${2:?uso: filmes.sh um <FILME.SFD>}
    [ -n "$(fim_esperado "$f")" ] || falha "$f nao esta no manifesto"
    if [ "$f" = NEWGAME.SFD ]; then
        caso "nativo-NEWGAME" NEWGAME.SFD nao $(( 600 + 453 ))
    else
        cp -f "$bsdata/$f" "$modv/NEWGAME.MP4" || falha "nao consegui copiar $f"
        caso "nativo-${f%.SFD}" "$f" sim $(( 600 + $(fim_esperado "$f") / 1000 * 2 ))
    fi
    ;;
mods)
    e017="$BS_PRIVADO/mod-E017.mp4"
    [ -f "$e017" ] || falha "sem $e017"
    semff="/usr/local/bs-sem-ffmpeg"   # PATH sem nenhuma pasta com o ffmpeg; o jogo e chamado pelo caminho
    cp -f "$e017" "$modv/NEWGAME.MP4"
    caso "mod-sem-ffmpeg" NEWGAME.SFD nao 1100 PATH="$semff" \
        && grep -q 'sem decodificador neste sistema' "$ultimo_log" \
        && anotar "A11.mod-sem-decodificador" ok "toca o NEWGAME original e avisa ($(basename "$ultimo_log"))" \
        || anotar "A11.mod-sem-decodificador" falha "ver filmes/$(basename "$ultimo_log")"
    if command -v ffmpeg >/dev/null && command -v ffprobe >/dev/null; then
        tol=500
        caso "mod-ffmpeg" E017.SFD sim 400 && anotar "A11.mod-mp4-ffmpeg" ok "E017.mp4 tocado pelo ffmpeg do sistema no lugar do NEWGAME" \
            || anotar "A11.mod-mp4-ffmpeg" falha "ver filmes/$(basename "$ultimo_log")"
        rm -f "$modv/NEWGAME.MP4"; cp -f "$e017" "$modv/newgame.mp4"
        caso "mod-minusculas" E017.SFD sim 400 && anotar "A11.mod-nome-minusculas" ok "newgame.mp4 aceito" \
            || anotar "A11.mod-nome-minusculas" falha "ver filmes/$(basename "$ultimo_log")"
        rm -f "$modv/newgame.mp4"; tol=100
    else
        aviso "ffmpeg ausente: instale (sudo apt install ffmpeg) e rode 'filmes.sh mods' de novo para os casos com ffmpeg"
        anotar "A11.mod-mp4-ffmpeg" nao-feito "ffmpeg ausente"
    fi
    : > "$modv/NEWGAME.MP4"
    caso "mod-vazio" NEWGAME.SFD nao 1100 && anotar "A11.mod-vazio" ok "mod vazio recusado, original tocado" \
        || anotar "A11.mod-vazio" falha "ver filmes/$(basename "$ultimo_log")"
    head -c 2000000 "$bsdata/E017.SFD" > "$modv/NEWGAME.MP4"
    caso "mod-truncado" E017.SFD sim 400
    if grep -qiE 'segmentation|abort|core dumped' "$ultimo_log"; then anotar "A11.mod-truncado" falha "o jogo caiu"
    else anotar "A11.mod-truncado" parcial "sem queda; conferir no log como o filme terminou (filmes/$(basename "$ultimo_log"))"; fi
    rm -f "$modv/NEWGAME.MP4"
    ;;
*) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
echo "Resumo: $resumo"
