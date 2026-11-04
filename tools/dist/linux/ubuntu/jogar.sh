#!/usr/bin/env bash
# N9-L: abre o jogo gerado na janela, com o log e as bibliotecas carregadas gravados nas evidencias.
#   jogar.sh <rotulo> [VAR=valor ...] [-- argumentos do jogo]
# Exemplos:
#   jogar.sh a7-radv BS_VULKAN_GPU=AMD
#   jogar.sh a7-software -- --render software
#   jogar.sh a8-pipewire SDL_AUDIO_DRIVER=pipewire
# Grava evidencias/sessoes/<rotulo>.txt (saida do jogo), <rotulo>.libs.txt (bibliotecas do processo: driver Vulkan,
# PipeWire/PulseAudio, Wayland/X11) e <rotulo>.resumo.txt. Feche o jogo normalmente (menu ou janela) para terminar.
# So acompanha o processo que ele mesmo abriu; nunca encerra outros.
set -uo pipefail
source "$(dirname "$0")/comum.sh"
rotulo=${1:?uso: jogar.sh <rotulo> [VAR=valor ...] [-- argumentos]}; shift
[[ "$rotulo" =~ ^[A-Za-z0-9._-]+$ ]] || falha "rotulo so com letras, numeros, ponto, _ e -"
vars=(); args=()
while [ $# -gt 0 ]; do
    if [ "$1" = "--" ]; then shift; args=("$@"); break; fi
    [[ "$1" =~ ^[A-Z_][A-Z0-9_]*=.*$ ]] || falha "esperava VAR=valor ou --: $1"
    vars+=("$1"); shift
done
[ -x "$BS_EXE" ] || falha "sem o jogo em $BS_EXE (rode a1_a2.sh)"
d="$BS_EVID/sessoes"; mkdir -p "$d"
out="$d/$rotulo.txt"; libs="$d/$rotulo.libs.txt"; res="$d/$rotulo.resumo.txt"
[ -e "$out" ] && falha "$out ja existe: use outro rotulo"
{ echo "inicio: $(agora)"; echo "ambiente: ${vars[*]:-}"; echo "argumentos: ${args[*]:-}"; } | privacidade > "$res"

ini=$(date +%s)
( cd "$BS_JOGO" && exec env "${vars[@]}" "$STDBUF" -oL ./BlueStinger "${args[@]}" ) > >(tee >(privacidade > "$out")) 2>&1 &
pid=$!
# bibliotecas carregadas: uma amostra aos 15 s e outras a cada 60 s (os drivers de audio e controle carregam sob demanda)
(
    sleep 15
    while [ -r "/proc/$pid/maps" ]; do
        grep -oE '/[^ ]+\.so[.0-9]*' "/proc/$pid/maps" 2>/dev/null | sed 's#.*/##' >> "$libs.bruto"
        sleep 60
    done
) &
amostra=$!
wait "$pid"; rc=$?
kill "$amostra" 2>/dev/null; wait "$amostra" 2>/dev/null
sleep 1
sort -u "$libs.bruto" 2>/dev/null > "$libs"; rm -f "$libs.bruto"
seg=$(( $(date +%s) - ini ))

{
    echo "fim: $(agora); codigo de saida $rc; ${seg} s"
    echo "-- dispositivo Vulkan usado (a prova do driver e este nome; o vulkaninfo diz o driver de cada nome):"
    grep -E '^\[apresentacao\] Vulkan direta:|^\[render\] (Vulkan:|renderizador:|Vulkan indisponivel)' "$out" | sort -u
    echo "-- o que o jogo disse:"
    grep -E '^\[(render|apresentacao|audio|controles|input|video|edicao|camera|saves|menu|legenda|legendas|texto)\]' "$out" | sort | uniq -c | sort -rn | head -60
    echo "-- bibliotecas carregadas (o carregador do Vulkan abre TODOS os drivers instalados, entao libvulkan_* aqui nao diz"
    echo "   qual foi usado; servem para o audio e o video do SDL: pipewire/pulse, wayland/X11):"
    grep -E 'vulkan|nvidia|pipewire|pulse|asound|wayland|libX11|xcb|decor|udev|ffmpeg|avcodec' "$libs"
} >> "$res"
cat "$res"
if grep -qE 'Vulkan direta: llvmpipe|\[render\] Vulkan: llvmpipe' "$out"; then
    aviso "o Vulkan usado foi o llvmpipe (CPU), nao a GPU: isto NAO aprova o A7"
fi
grep -q 'renderizador: software' "$out" && ! printf '%s\n' "${args[@]}" | grep -q software && aviso "o jogo recuou para o software sem ser pedido: veja o motivo no log"
echo
echo "Agora anote o que viu: bash \"$BS_FERRAMENTAS/anotar.sh\" <item> <ok|falha|parcial> \"comentario\""
