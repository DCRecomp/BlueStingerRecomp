#!/usr/bin/env bash
# N9-L (L5, D-L2): SDL3 pre-compilado do kit do Linux (third_party/SDL3-linux). Feito uma vez, na maquina que monta o
# pacote, com os cabecalhos de X11, Wayland, PipeWire, PulseAudio e ALSA dela: todos os backends ficam ligados e cada um
# carrega a biblioteca do sistema em tempo de execucao (dlopen). Assim o usuario compila o jogo sem esses cabecalhos.
#   sdl3_linux.sh <fontes do SDL3 release-3.2.30> <prefixo de saida> [arquivo de toolchain do CMake]
# Mesmas opcoes do app/CMakeLists.txt (estatico, sem testes, sem exemplos, sem asserts). Recusa se algum backend
# esperado ficar de fora.
set -euo pipefail
src=${1:?fontes do SDL3}; prefix=${2:?prefixo}; tcfile=${3:-}
[ -f "$src/CMakeLists.txt" ] || { echo "sem SDL3 em $src" >&2; exit 1; }
grep -q 'set(MAJOR_VERSION 3)' "$src/CMakeLists.txt" 2>/dev/null || grep -q 'project(SDL3' "$src/CMakeLists.txt" || { echo "$src nao e o SDL3" >&2; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/bs-sdl3.XXXXXX"); trap 'rm -rf "$work"' EXIT
# absolutos: o CMake roda de dentro de $work (o Zig 0.13 grava "-o -" num arquivo "-" na pasta atual)
src=$(cd "$src" && pwd -P); prefix=$(realpath -m -- "$prefix"); [ -n "$tcfile" ] && tcfile=$(realpath -- "$tcfile")
# CMAKE com barra (caminho relativo a quem chamou) vira absoluto; nome simples continua pelo PATH (revisao Codex 20261103-143212)
CMAKE=${CMAKE:-cmake}; case "$CMAKE" in */*) CMAKE=$(realpath -- "$CMAKE") ;; esac
args=(-S "$src" -B "$work" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix"
      -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF -DSDL_ASSERTIONS=disabled
      -DSDL_X11=ON -DSDL_WAYLAND=ON -DSDL_PIPEWIRE=ON -DSDL_PULSEAUDIO=ON -DSDL_ALSA=ON
      -DSDL_X11_SHARED=ON -DSDL_WAYLAND_SHARED=ON -DSDL_PIPEWIRE_SHARED=ON -DSDL_PULSEAUDIO_SHARED=ON -DSDL_ALSA_SHARED=ON
      -DSDL_VULKAN=ON -DSDL_HIDAPI=ON -DSDL_DEPS_SHARED=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON)
# com o compilador portatil (alvo glibc 2.31): os cabecalhos da glibc sao os do compilador; os de X11, Wayland, PipeWire,
# PulseAudio e ALSA vem do sistema, depois deles (-idirafter), e as bibliotecas so para ler o SONAME (o SDL3 as carrega com
# dlopen; nada do sistema e ligado)
[ -n "$tcfile" ] && args+=(-DCMAKE_TOOLCHAIN_FILE="$tcfile" -DBS_TOOLCHAIN_SYSTEM_PATHS=ON
                           "-DCMAKE_C_FLAGS=-idirafter /usr/include -ffile-prefix-map=$work=sdl3-build -ffile-prefix-map=$src=sdl3"
                           "-DCMAKE_CXX_FLAGS=-idirafter /usr/include -ffile-prefix-map=$work=sdl3-build -ffile-prefix-map=$src=sdl3"
                           -DCMAKE_LIBRARY_ARCHITECTURE="$(gcc -print-multiarch 2>/dev/null || echo x86_64-linux-gnu)"
                           -DCMAKE_MAKE_PROGRAM="$(dirname "$tcfile")/bin/ninja")
[ -z "$tcfile" ] && args+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
                           "-DCMAKE_C_FLAGS=-ffile-prefix-map=$work=sdl3-build -ffile-prefix-map=$src=sdl3")
(cd "$work" && "${CMAKE:-cmake}" "${args[@]}") > "$work/configure.txt" 2>&1 || { tail -40 "$work/configure.txt"; exit 1; }
# os backends que o jogo usa (video, audio, entrada) tem de estar no resumo da configuracao
for want in 'SDL_X11' 'SDL_WAYLAND' 'SDL_PIPEWIRE' 'SDL_PULSEAUDIO' 'SDL_ALSA'; do
    grep -Eq "^-- +${want} +\(Wanted: ON\): ON" "$work/configure.txt" || { grep -E "^-- +SDL_(X11|WAYLAND|PIPEWIRE|PULSEAUDIO|ALSA)" "$work/configure.txt"; echo "backend $want ficou de fora" >&2; exit 1; }
done
"${CMAKE:-cmake}" --build "$work" > "$work/build.txt" 2>&1 || { grep -E 'error|FAILED' "$work/build.txt" | head -30; exit 1; }
rm -rf "$prefix"
"${CMAKE:-cmake}" --install "$work" > /dev/null
cp "$src/LICENSE.txt" "$prefix/LICENSE.txt"
# nenhum caminho desta maquina no binario (vai no pacote)
if grep -qaF -- "$HOME" "$prefix/lib/libSDL3.a" || grep -qaF -- "$work" "$prefix/lib/libSDL3.a"; then
    echo "libSDL3.a contem caminhos desta maquina" >&2; exit 1
fi
grep -E "^-- +SDL_(X11|WAYLAND|PIPEWIRE|PULSEAUDIO|ALSA|VULKAN|HIDAPI|DBUS|IBUS|LIBUDEV) " "$work/configure.txt" > "$prefix/BACKENDS.txt" || true
# D-Bus (inibir o descanso de tela, portais) e opcional: sem libdbus-1-dev na maquina do pacote ele fica de fora
grep -Eq "^-- +SDL_DBUS +\(Wanted: ON\): ON" "$work/configure.txt" || echo "aviso: SDL3 sem D-Bus (instale libdbus-1-dev na maquina que monta o pacote)" >&2
echo "SDL3 pronto em $prefix"
