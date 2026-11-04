#!/usr/bin/env bash
# N9-L: coleta de evidencias da sessao em Ubuntu nativo. Somente leitura: nao instala, nao muda configuracao, nao encerra
# processos. Comandos com sudo so com --privilegiado, e cada um e mostrado e perguntado antes.
#
#   coletar.sh inicio            sistema antes de qualquer pacote (A1: o que o Ubuntu ja traz)
#   coletar.sh sistema <rotulo>  de novo, depois de instalar algo (ex.: depois-ferramentas, depois-nvidia)
#   coletar.sh jogo              hashes do Builder e do jogo, VERSAO.txt, edicao, logs, pastas XDG (sem dados do jogo)
#   coletar.sh final             tudo acima, a matriz de resultados e o pacote evidencias-<data>.tar.gz, com a varredura
#                                de privacidade
#   [--privilegiado]             acrescenta o log do kernel filtrado (GPU, audio, entrada), com autorizacao
#
# Nunca coleta: dados do jogo, saves (so os hashes), o CDI, MAC, numero de serie, UUID, nome da maquina.
set -uo pipefail
source "$(dirname "$0")/comum.sh"
modo=${1:-}; rotulo=${2:-}
priv=0; for a in "$@"; do [ "$a" = "--privilegiado" ] && priv=1; done
mkdir -p "$BS_EVID" || falha "sem $BS_EVID"

# roda um comando opcional e grava a saida filtrada; ausencia do comando fica registrada, sem erro
cmd() {  # cmd <arquivo> <comando...>
    local out=$1; shift
    { printf '$ %s\n' "$*"
      if command -v "$1" >/dev/null 2>&1; then timeout 60 "$@" 2>&1; echo "[rc=$?]"; else echo "[comando ausente: $1]"; fi
    } | privacidade >> "$out"
    echo >> "$out"
}

sistema() {  # sistema <pasta>
    local d=$1 f
    mkdir -p "$d"
    f="$d/sistema.txt"; : > "$f"
    { echo "coleta: $(agora)"; echo "kernel: $(uname -srvm)"; } >> "$f"
    cmd "$f" cat /etc/os-release
    cmd "$f" cat /proc/cmdline
    cmd "$f" sh -c 'grep -m1 "model name" /proc/cpuinfo; nproc'
    cmd "$f" free -m
    cmd "$f" lsblk -o NAME,SIZE,TYPE,FSTYPE,RM,TRAN,RO,MOUNTPOINTS
    cmd "$f" df -h -x tmpfs -x devtmpfs -x squashfs -x efivarfs
    cmd "$f" df -h /tmp "$HOME"
    cmd "$f" sh -c 'mokutil --sb-state 2>&1 || echo "(mokutil ausente)"'
    cmd "$f" sh -c 'echo "XDG_SESSION_TYPE=$XDG_SESSION_TYPE XDG_CURRENT_DESKTOP=$XDG_CURRENT_DESKTOP WAYLAND_DISPLAY=${WAYLAND_DISPLAY:+sim} DISPLAY=${DISPLAY:+sim}"'
    cmd "$f" ldd --version
    # pacotes que importam: o que o A1 promete nao instalar e o que o jogo carrega
    cmd "$f" sh -c "dpkg-query -W -f='\${Package} \${Version} \${Status}\n' libvulkan1 mesa-vulkan-drivers vulkan-tools ffmpeg binutils \
        build-essential gcc g++ clang cmake ninja-build wine wine64 'nvidia-driver-*' 'libnvidia-gl-*' pipewire pipewire-pulse \
        wireplumber pulseaudio libsdl3-0 libsdl2-2.0-0 2>&1 | grep -v 'no packages found'"
    cmd "$f" sh -c 'command -v wine wine64 proton steam 2>&1 || echo "(nenhum)"'

    f="$d/gpu.txt"; : > "$f"
    cmd "$f" lspci -nnk -d ::0300
    cmd "$f" lspci -nnk -d ::0302
    cmd "$f" lspci -nnk -d ::0380
    cmd "$f" sh -c 'for c in /sys/class/drm/card*; do [ -e "$c/device/driver" ] && echo "$(basename "$c") $(basename "$(readlink -f "$c/device/driver")") $(cat "$c/device/vendor" "$c/device/device" 2>/dev/null | tr "\n" " ")"; done'
    cmd "$f" sh -c 'for s in /sys/class/drm/card*-*/status; do echo "$s: $(cat "$s")"; done'
    cmd "$f" sh -c 'ls /usr/share/vulkan/icd.d/ /etc/vulkan/icd.d/ 2>&1'
    cmd "$f" sh -c 'lsmod | grep -E "^(amdgpu|nvidia|nvidia_drm|nvidia_modeset|nouveau|nova_core|i915|xe) " || echo "(nenhum modulo de GPU listado)"'
    cmd "$f" cat /proc/driver/nvidia/version
    cmd "$f" nvidia-smi -L
    cmd "$f" vulkaninfo --summary
    cmd "$f" sh -c 'command -v glxinfo >/dev/null && glxinfo -B | grep -E "OpenGL (vendor|renderer|version)" || echo "(glxinfo ausente)"'

    f="$d/audio.txt"; : > "$f"
    cmd "$f" pactl info
    cmd "$f" pactl list short sinks
    cmd "$f" pactl list short sources
    cmd "$f" wpctl status
    cmd "$f" aplay -l
    cmd "$f" cat /proc/asound/cards

    f="$d/entrada.txt"; : > "$f"
    cmd "$f" lsusb
    cmd "$f" sh -c 'grep -E "^(N: Name|H: Handlers|U: Uniq)" /proc/bus/input/devices'
    # sem /dev/input/by-id: os nomes de la trazem o numero de serie do aparelho
    cmd "$f" sh -c 'ls -l /dev/hidraw* /dev/uinput 2>&1 | sed -E "s/^([^ ]+) .* (\/dev\/[^ ]+)$/\1 \2/"'
    cmd "$f" sh -c 'lsmod | grep -E "^(xpad|hid_microsoft|hid_xpadneo|hid_sony|hid_playstation|hid_nintendo|btusb|uhid) " || echo "(nenhum)"'
    cmd "$f" bluetoothctl devices Connected

    if [ $priv -eq 1 ]; then
        f="$d/kernel.txt"; : > "$f"
        echo "log do kernel (dmesg): filtrado por GPU, audio e entrada"
        privilegiado sh -c 'dmesg --level=emerg,alert,crit,err,warn,notice,info 2>&1 | grep -iE "amdgpu|radeon|nvidia|nouveau|nova|drm|snd|hda|usb.*(xbox|controller|gamepad)|xpad|input:|hid|bluetooth|firmware"' \
            2>&1 | privacidade >> "$f"
    fi
    echo "sistema em $d"
}

jogo() {
    local d="$BS_EVID/jogo" f
    mkdir -p "$d"; f="$d/jogo.txt"; : > "$f"
    { echo "coleta: $(agora)"; echo "pasta do jogo: $BS_JOGO"; } | privacidade >> "$f"
    for x in "$BS_BUILDER" "$(readlink -f "$BS_BUILDER" 2>/dev/null)" "$BS_EXE"; do
        [ -n "$x" ] && [ -f "$x" ] && echo "sha256 $(sha "$x")  $x" | privacidade >> "$f"
    done
    for x in "$HOME/.local/share/BlueStinger-Builder/VERSAO.txt" "$BS_JOGO/VERSAO.txt" "$BS_JOGO/instalacao.toml"; do
        [ -f "$x" ] && { echo "== $x"; cat "$x"; echo; } | privacidade >> "$f"
    done
    cmd "$f" "$BS_EXE" --version
    cmd "$f" file "$BS_EXE"
    cmd "$f" ldd "$BS_EXE"
    cmd "$f" sh -c "ldd \"$BS_EXE\" | grep -ciE 'wine|proton' | sed 's/^/bibliotecas do wine ou proton: /'"
    # pastas do usuario: so nomes, tamanhos e hashes (nada de conteudo dos saves)
    for p in "$HOME/.config/BlueStingerRecomp" "$HOME/.local/share/DC-Recomp" "$HOME/.cache/BlueStingerRecomp" \
             "$HOME/.local/share/BlueStinger-Builder" "$BS_JOGO/saves" "$BS_JOGO/mods"; do
        echo "== $p" | privacidade >> "$f"
        if [ -d "$p" ]; then
            find "$p" -maxdepth 4 \( -path '*/build/*' -o -path '*/toolchain/*' -o -path '*/kit/*' -o -path '*/zig/*' \) -prune -o -type f -printf '%s %P\n' 2>/dev/null \
                | head -200 | privacidade >> "$f"
            find "$p" -maxdepth 4 -type f \( -name 'BLUESTIN*' -o -name '*.bsv' -o -name '*.vms' \) -exec sha256sum {} + 2>/dev/null | privacidade >> "$f"
        else echo "(nao existe)" >> "$f"; fi
    done
    # logs (texto do proprio projeto): Builder e jogo
    [ -f "$BS_JOGO_DIR/.bs-build/builder.log" ] && privacidade < "$BS_JOGO_DIR/.bs-build/builder.log" > "$d/builder.log"
    [ -f "$BS_JOGO/bluestinger.log" ] && privacidade < "$BS_JOGO/bluestinger.log" > "$d/bluestinger-pasta-do-jogo.log"
    [ -f "$HOME/.config/BlueStingerRecomp/bluestinger.log" ] && privacidade < "$HOME/.config/BlueStingerRecomp/bluestinger.log" > "$d/bluestinger-xdg.log"
    echo "jogo em $d"
}

final() {
    sistema "$BS_EVID/sistema-final"
    jogo
    local m="$BS_EVID/matriz.txt"
    {
        echo "Matriz da sessao (ultimo resultado de cada item; o historico completo esta em resultados.tsv)"
        echo
        if [ -f "$BS_EVID/resultados.tsv" ]; then
            tail -n +2 "$BS_EVID/resultados.tsv" | awk -F'\t' '{u[$2]=$3"\t"$1"\t"$4} END {for (k in u) print k"\t"u[k]}' | sort
        else echo "(nenhum item anotado)"; fi
    } > "$m"
    # varredura de privacidade: caminho pessoal, usuario@maquina, MAC ou nome da maquina sobrando = nada de pacote
    local achados
    achados=$(identificadores_restantes "$BS_EVID")
    if [ -n "$achados" ]; then
        echo "$achados"
        falha "dados pessoais possiveis nos arquivos acima: o pacote NAO foi feito. Passe-os por privacidade ou apague-os e rode de novo"
    fi
    if find "$BS_EVID" -type f \( -iname '*.cdi' -o -iname '*.sfd' -o -iname '*.mp4' -o -iname '*.bin' -o -iname '*.bsv' -o -iname '*.vms' -o -iname 'BLUESTIN*' \) | grep -q .; then
        falha "ha arquivo de midia ou save em $BS_EVID: retire antes de empacotar"
    fi
    local pac="$BS_TROCA/evidencias-$(date +%Y%m%d-%H%M).tar.gz"
    ( cd "$BS_TROCA" && find evidencias -type f ! -name '*.tar.gz' -print0 | sort -z | xargs -0 sha256sum > evidencias/SHA256SUMS.txt \
      && tar --sort=name --owner=0 --group=0 -czf "$pac" evidencias ) || falha "nao consegui empacotar"
    echo
    echo "Pacote de evidencias: $pac ($(du -h "$pac" | cut -f1))"
    echo "Leve este arquivo de volta ao Windows (o disco de troca abre direto no Windows)."
}

case "$modo" in
    inicio) sistema "$BS_EVID/sistema-inicio" ;;
    sistema) [[ "$rotulo" =~ ^[A-Za-z0-9._-]+$ && "$rotulo" != *..* ]] || falha "uso: coletar.sh sistema <rotulo> (so letras, numeros, ponto, _ e -)"
             sistema "$BS_EVID/sistema-$rotulo" ;;
    jogo) jogo ;;
    final) final ;;
    *) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
