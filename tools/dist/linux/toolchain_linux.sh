#!/usr/bin/env bash
# N9-L (L5, D-L2): monta o compilador portatil do pacote Linux (pasta toolchain/) a partir de downloads oficiais conferidos.
#   toolchain_linux.sh <pasta dos downloads> <pasta de saida>
# Pecas (versoes fixas; a escolha do Zig foi do mantenedor em 2026-11-03):
#   Zig 0.13.0      zig-linux-x86_64-0.13.0.tar.xz    Clang/LLD 18.1.6 embutidos, libc++ estatica, alvo glibc 2.31 (MIT;
#                                                       LLVM: Apache-2.0 com excecao LLVM; cabecalhos da glibc LGPL)
#   CMake 3.30.5    cmake-3.30.5-linux-x86_64.tar.gz  binario oficial da Kitware (BSD-3); so bin/cmake e share/
#   Ninja 1.12.1    ninja-linux.zip (renomeado ninja-1.12.1-linux.zip)   binario oficial (Apache-2.0)
# O SHA-256 de cada download e conferido contra o valor fixado aqui (Zig e CMake: os publicados por eles; Ninja: o medido
# no download de 2026-11-03, a release nao publica soma). Saida:
#   bs-toolchain.cmake   arquivo de toolchain do CMake (compiladores pelos invocadores de bin/)
#   bin/bs-cc bin/bs-c++ bin/bs-ar bin/bs-ranlib   invocadores do Zig com o alvo fixo
#   bin/ninja  cmake/  zig/  VERSAO.txt  licencas/
set -euo pipefail
export LC_ALL=C
dl=${1:?pasta dos downloads}; out=${2:?pasta de saida}
ZIG=zig-linux-x86_64-0.13.0.tar.xz;  ZIG_SHA=d45312e61ebcc48032b77bc4cf7fd6915c11fa16e4aad116b66c9468211230ea
CMK=cmake-3.30.5-linux-x86_64.tar.gz; CMK_SHA=f747d9b23e1a252a8beafb4ed2bc2ddf78cff7f04a8e4de19f4ff88e9b51dc9d
NJA=ninja-1.12.1-linux.zip;          NJA_SHA=6f98805688d19672bd699fbbfa2c2cf0fc054ac3df1f0e6a47664d963d530255
NJA_LIC=ninja-1.12.1-COPYING
for p in "$ZIG $ZIG_SHA" "$CMK $CMK_SHA" "$NJA $NJA_SHA"; do
    set -- $p
    [ -f "$dl/$1" ] || { echo "falta $dl/$1" >&2; exit 1; }
    echo "$2  $dl/$1" | sha256sum -c --quiet || { echo "SHA-256 de $1 nao confere" >&2; exit 1; }
done
[ -f "$dl/$NJA_LIC" ] || { echo "falta $dl/$NJA_LIC (licenca do Ninja, COPYING da tag v1.12.1)" >&2; exit 1; }
command -v python3 > /dev/null || { echo "falta o python3 (para abrir o zip do Ninja)" >&2; exit 1; }

rm -rf "$out"; mkdir -p "$out/bin" "$out/licencas"
tar -xJf "$dl/$ZIG" -C "$out"; mv "$out/zig-linux-x86_64-0.13.0" "$out/zig"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
tar -xzf "$dl/$CMK" -C "$tmp"; mkdir -p "$out/cmake/bin"
cp -a "$tmp/cmake-3.30.5-linux-x86_64/bin/cmake" "$out/cmake/bin/"
cp -a "$tmp/cmake-3.30.5-linux-x86_64/share" "$out/cmake/"
rm -rf "$out/cmake/share/cmake-3.30/Help" "$out/cmake/share/man" "$out/cmake/share/doc"
python3 -I -m zipfile -e "$dl/$NJA" "$out/bin"; chmod 755 "$out/bin/ninja"

# invocadores: o alvo, a CPU base e o cache do Zig ficam fixos aqui (o mesmo compilador e as mesmas opcoes para todos).
# -g0: o Zig gera informacao de depuracao ate no -O2 (o clang nao); um -g do CMake (Debug) vem depois e vale
for t in cc c++; do
cat > "$out/bin/bs-$t" <<EOF
#!/bin/sh
# Blue Stinger Builder: compilador portatil (Zig 0.13.0, Clang 18). Alvo: x86-64 base, glibc 2.31.
d=\$(dirname "\$0")
: "\${ZIG_GLOBAL_CACHE_DIR:=\${XDG_CACHE_HOME:-\$HOME/.cache}/BlueStingerRecomp/zig}"
: "\${ZIG_LOCAL_CACHE_DIR:=\$ZIG_GLOBAL_CACHE_DIR}"
export ZIG_GLOBAL_CACHE_DIR ZIG_LOCAL_CACHE_DIR
exec "\$d/../zig/zig" $t -target x86_64-linux-gnu.2.31 -mcpu=baseline -g0 "\$@"
EOF
done
for t in ar ranlib; do
cat > "$out/bin/bs-$t" <<EOF
#!/bin/sh
d=\$(dirname "\$0")
: "\${ZIG_GLOBAL_CACHE_DIR:=\${XDG_CACHE_HOME:-\$HOME/.cache}/BlueStingerRecomp/zig}"
export ZIG_GLOBAL_CACHE_DIR
exec "\$d/../zig/zig" $t "\$@"
EOF
done
chmod 755 "$out"/bin/bs-*

cat > "$out/bs-toolchain.cmake" <<'EOF'
# Blue Stinger Builder (N9-L L5, D-L2): compilador portatil. Zig 0.13.0 (Clang/LLD 18.1.6), alvo x86-64 base com glibc 2.31,
# libc++ estatica. As opcoes de fidelidade (-fno-strict-aliasing -fwrapv -ffp-contract=off) vem do CMakeLists.txt do kit.
get_filename_component(_bs_tc "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(CMAKE_C_COMPILER   "${_bs_tc}/bin/bs-cc")
set(CMAKE_CXX_COMPILER "${_bs_tc}/bin/bs-c++")
set(CMAKE_ASM_COMPILER "${_bs_tc}/bin/bs-cc")
set(CMAKE_AR           "${_bs_tc}/bin/bs-ar"     CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB       "${_bs_tc}/bin/bs-ranlib" CACHE FILEPATH "" FORCE)
# so o que esta no toolchain e no kit: nada de bibliotecas nem cabecalhos do sistema (BS_TOOLCHAIN_SYSTEM_PATHS=ON so na
# maquina que monta o pacote, para o SDL3 achar X11, Wayland e PipeWire: tools/dist/linux/sdl3_linux.sh)
if(NOT BS_TOOLCHAIN_SYSTEM_PATHS)
  set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH OFF)
  set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH OFF)
endif()
set(CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY ON)
EOF

z=$out/zig
cp "$z/LICENSE" "$out/licencas/Zig-MIT.txt"
cp "$z/lib/libcxx/LICENSE.TXT" "$out/licencas/LLVM-Clang-LLD-libcxx-Apache-2.0-LLVM-exception.txt"
cp "$z/lib/libc/glibc/LICENSES" "$out/licencas/glibc-LICENSES.txt"
cp "$z/lib/libc/musl/COPYRIGHT" "$out/licencas/musl-COPYRIGHT.txt"
cp "$z/lib/libc/mingw/COPYING" "$out/licencas/mingw-w64-COPYING.txt"
cp "$z/lib/libc/wasi/LICENSE" "$out/licencas/wasi-libc-LICENSE.txt"
cp "$tmp/cmake-3.30.5-linux-x86_64/doc/cmake/Copyright.txt" "$out/licencas/CMake-BSD-3.txt"
# as bibliotecas que o CMake leva dentro dele (curl, zlib, expat, libarchive...): os textos de cada uma
(cd "$tmp/cmake-3.30.5-linux-x86_64/doc/cmake" && find . -mindepth 2 -type f \( -iname 'copyright*' -o -iname 'copying*' -o -iname 'license*' \) -print0 |
    while IFS= read -r -d '' f; do mkdir -p "$out/licencas/CMake-componentes/$(dirname "$f")"; cp "$f" "$out/licencas/CMake-componentes/$f"; done)
cp "$dl/$NJA_LIC" "$out/licencas/Ninja-Apache-2.0.txt"
cat > "$out/licencas/LEIA-ME.txt" <<'EOF'
Licencas do compilador portatil (pasta toolchain/ do Blue Stinger Builder para Linux)
- Zig 0.13.0 (zig/): MIT (Zig-MIT.txt). O executavel zig contem o LLVM, o Clang e o LLD 18.1.6: Apache-2.0 com excecoes
  LLVM (LLVM-Clang-LLD-libcxx-Apache-2.0-LLVM-exception.txt, o mesmo texto do libc++, libc++abi e libunwind em zig/lib).
- zig/lib/libc: cabecalhos e fontes das bibliotecas C para cada alvo, distribuidos como fontes: glibc (LGPL-2.1+,
  glibc-LICENSES.txt), musl (MIT), mingw-w64, wasi-libc e os cabecalhos do kernel Linux (GPL-2.0 com Linux-syscall-note,
  conforme o SPDX de cada arquivo).
- CMake 3.30.5 (cmake/): BSD-3-Clause (CMake-BSD-3.txt); so o executavel cmake e os modulos, sem documentacao.
- Ninja 1.12.1 (bin/ninja): Apache-2.0 (Ninja-Apache-2.0.txt).
O jogo compilado por este compilador liga o libc++, o libc++abi, o libunwind e o compiler-rt (excecao LLVM: sem exigencia de
aviso no binario) e usa a glibc do sistema dinamicamente.
EOF

zv=$("$z/zig" version); cv=$("$z/zig" cc --version | head -1); kv=$("$out/cmake/bin/cmake" --version | head -1); nv=$("$out/bin/ninja" --version)
cat > "$out/VERSAO.txt" <<EOF
Zig $zv ($cv); alvo x86_64-linux-gnu.2.31, CPU base, libc++ estatica
CMake: $kv
Ninja: $nv
Procedencia (SHA-256 conferido): $ZIG $ZIG_SHA; $CMK $CMK_SHA; $NJA $NJA_SHA
EOF
"$out/bin/bs-c++" --version > /dev/null
echo "toolchain pronto em $out ($(du -sh "$out" | cut -f1))"; cat "$out/VERSAO.txt"
