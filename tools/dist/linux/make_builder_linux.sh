#!/usr/bin/env bash
# N9-L (L5, D-L5): monta o pacote do Blue Stinger Builder para Linux x86-64 (.tar.gz), sempre a partir de um commit ou
# arvore do git (nunca da arvore de trabalho: nenhum arquivo particular entra; o known_versions.h e o do commit).
#
#   make_builder_linux.sh --repo <repo> --tree <commit|arvore> --out <pasta> --sdl3 <SDL3 pre-compilado>
#                         --deps <pasta com tomlplusplus/ e freetype/> [--toolchain <compilador portatil>]
#                         [--versao <rotulo>] [--game-data <pasta com 1ST_READ.BIN e IP.BIN>]
#
# --toolchain: a pasta do compilador portatil (bs-toolchain.cmake, cmake/, bin/ninja, VERSAO.txt, licencas/). Ela
#   compila o bs-builder e vai para o pacote em toolchain/. Sem ela o bs-builder sai do clang do sistema e o pacote fica
#   "sem compilador" (o usuario precisa de --compilador-sistema): so para desenvolvimento.
# --game-data: amostras do 1ST_READ.BIN e do IP.BIN locais para provar que nenhum byte do jogo entrou no pacote.
#
# Saida: <out>/BlueStinger-Builder-<versao>-linux-x86_64.tar.gz e o .sha256 ao lado; a pasta montada fica em
# <out>/BlueStinger-Builder-<versao>-linux-x86_64/ (para os testes). Recusa se faltar algo, se houver conteudo do jogo,
# caminhos do ambiente de desenvolvimento nos binarios ou dependencia dinamica fora da base do sistema.
set -euo pipefail
umask 022
export LC_ALL=C TZ=UTC

repo=""; tree=""; out=""; sdl3=""; deps=""; toolchain=""; versao=""; gamedata=""
while [ $# -gt 0 ]; do
    case "$1" in
        --repo) repo=$2; shift 2 ;; --tree) tree=$2; shift 2 ;; --out) out=$2; shift 2 ;;
        --sdl3) sdl3=$2; shift 2 ;; --deps) deps=$2; shift 2 ;; --toolchain) toolchain=$2; shift 2 ;;
        --versao) versao=$2; shift 2 ;; --game-data) gamedata=$2; shift 2 ;;
        *) echo "opcao desconhecida: $1" >&2; exit 2 ;;
    esac
done
falha() { echo "ERRO: $*" >&2; exit 1; }
passo() { echo "== $*"; }
[ -n "$repo" ] && [ -n "$tree" ] && [ -n "$out" ] && [ -n "$sdl3" ] && [ -n "$deps" ] || falha "uso: veja o cabecalho do script"
git() { command git -c safe.directory='*' "$@"; }
sha=$(git -C "$repo" rev-parse --short=10 "$tree") || falha "arvore $tree desconhecida"
[ -n "$versao" ] || versao=$(git -C "$repo" describe --always --abbrev=7 "$tree" 2>/dev/null || echo "$sha")
# o rotulo vira nome de pasta (criada e apagada em --out): um componente so, sem barra nem .. (revisao Codex 20261103-105354)
[[ "$versao" =~ ^[A-Za-z0-9][A-Za-z0-9._+-]*$ ]] && [[ "$versao" != *..* ]] || falha "rotulo de versao invalido: '$versao' (so letras, numeros e . _ + -)"
for f in "$sdl3/lib/cmake/SDL3/SDL3Config.cmake" "$sdl3/lib/libSDL3.a" "$sdl3/LICENSE.txt" \
         "$deps/tomlplusplus/CMakeLists.txt" "$deps/tomlplusplus/LICENSE" "$deps/freetype/CMakeLists.txt" "$deps/freetype/LICENSE.TXT"; do
    [ -f "$f" ] || falha "falta $f"
done
if [ -n "$toolchain" ]; then
    for f in bs-toolchain.cmake VERSAO.txt cmake/bin/cmake bin/ninja; do [ -e "$toolchain/$f" ] || falha "compilador portatil sem $f"; done
    [ -d "$toolchain/licencas" ] || falha "compilador portatil sem licencas/"
    toolchain=$(cd "$toolchain" && pwd -P)
fi
# absolutos: o CMake roda de dentro da pasta de compilacao (abaixo)
sdl3=$(cd "$sdl3" && pwd -P); deps=$(cd "$deps" && pwd -P)

nome=BlueStinger-Builder-$versao-linux-x86_64
mkdir -p "$out"; out=$(cd "$out" && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/bs-pacote.XXXXXX"); trap 'rm -rf "$work"' EXIT
stage=$work/$nome
mkdir -p "$stage/kit" "$stage/licencas" "$work/src"

passo "fontes do commit $sha"
git -C "$repo" archive "$sha" | tar -x -C "$work/src"

# fontes de terceiros sem o .git (o pacote leva so o que compila)
copia_fontes() {  # <origem> <destino>
    if git -C "$1" rev-parse --git-dir > /dev/null 2>&1; then mkdir -p "$2"; git -C "$1" archive HEAD | tar -x -C "$2"
    else mkdir -p "$2"; cp -a "$1/." "$2/"; rm -rf "$2/.git"; fi
}

passo "compilando o bs-builder e o instalador (Release)"
# N9.1: o instalador grafico (BlueStinger-Recompiler) e o desinstalador saem do SDL3 pre-compilado do kit, estatico
args=(-S "$work/src" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBS_BUILD_TESTS=OFF -DBS_BUILD_GAME=OFF
      -DBS_BUILD_TOOLS=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DBS_SDL3_PREBUILT="$sdl3"
      -DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS="$deps/tomlplusplus" -DFETCHCONTENT_SOURCE_DIR_FREETYPE="$deps/freetype")
if [ -n "$toolchain" ]; then
    args+=(-DCMAKE_TOOLCHAIN_FILE="$toolchain/bs-toolchain.cmake"); cmakebin=$toolchain/cmake/bin/cmake
    args+=(-DCMAKE_MAKE_PROGRAM="$toolchain/bin/ninja")
else
    args+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld); cmakebin=cmake
fi
# o caminho da compilacao nao pode entrar no binario (__FILE__ de asserts): o prefixo vira "bs"
args+=(-DCMAKE_C_FLAGS="-ffile-prefix-map=$work/src=bs -ffile-prefix-map=$deps=deps"
       -DCMAKE_CXX_FLAGS="-ffile-prefix-map=$work/src=bs -ffile-prefix-map=$deps=deps")
# de dentro da pasta de compilacao: o Zig 0.13 grava "-o -" (o teste da biblioteca padrao do CMake) num arquivo chamado
# "-" na pasta atual, em vez da saida padrao; fora dela ele apareceria em quem chamou o script
mkdir -p "$work/build"
(cd "$work/build" && "$cmakebin" "${args[@]}") > "$work/configure.txt" 2>&1 || { tail -30 "$work/configure.txt"; falha "configuracao do CMake"; }
binarios=(bs-builder BlueStinger-Recompiler BlueStinger-Desinstalar)
"$cmakebin" --build "$work/build" --target "${binarios[@]}" > "$work/build.txt" 2>&1 || { grep -E 'error|FAILED' "$work/build.txt" | head -30; falha "compilacao"; }
for b in "${binarios[@]}"; do
    cp "$work/build/builder/$b" "$stage/$b"
    strip --strip-unneeded "$stage/$b" 2>/dev/null || true
done

passo "kit de fontes"
entradas=(CMakeLists.txt LICENSE app runtime tools/extractor tools/dist/LEIA-ME.txt config tools/dcmap native legacy transitional
          tools/build third_party/dr_libs third_party/xiph third_party/vulkan mods-padrao
          third_party/fonts third_party/pl_mpeg tools/dist/linux/LEIA-ME-JOGO.txt)
git -C "$repo" archive "$sha" -- "${entradas[@]}" | tar -x -C "$stage/kit"
printf '%s\n' "$versao" > "$stage/kit/VERSION"
copia_fontes "$deps/tomlplusplus" "$stage/kit/third_party/tomlplusplus"
copia_fontes "$deps/freetype" "$stage/kit/third_party/freetype"
mkdir -p "$stage/kit/third_party/SDL3-linux"; cp -a "$sdl3/." "$stage/kit/third_party/SDL3-linux/"
# o sdl3.pc grava o prefixo da maquina que compilou o SDL3 (caminho e nome de usuario): relativo a ele mesmo. O jogo usa o
# pacote CONFIG do CMake (app/CMakeLists.txt), nao o pkg-config
pc=$stage/kit/third_party/SDL3-linux/lib/pkgconfig/sdl3.pc
[ -f "$pc" ] && sed -i 's|^prefix=.*|prefix=${pcfiledir}/../..|' "$pc"

passo "licencas e textos"
L=$stage/licencas; K=$stage/kit
cp "$work/src/LICENSE" "$L/BlueStingerRecompiled-MIT.txt"
cp "$sdl3/LICENSE.txt" "$L/SDL3-zlib.txt"
cp "$deps/tomlplusplus/LICENSE" "$L/tomlplusplus-MIT.txt"
cp "$deps/freetype/LICENSE.TXT" "$L/FreeType-LICENSE.txt"; cp "$deps/freetype/docs/FTL.TXT" "$L/FreeType-FTL.txt"
cp "$K/third_party/fonts/noto-sans/OFL.txt" "$L/NotoSans-OFL-1.1.txt"
cp "$K/third_party/pl_mpeg/LICENSE" "$L/pl_mpeg-MIT.txt"
cp "$K/third_party/dr_libs/LICENSE" "$L/dr_flac-MIT0.txt"
cp "$K/third_party/xiph/libogg/COPYING" "$L/libogg-BSD.txt"; cp "$K/third_party/xiph/libvorbis/COPYING" "$L/libvorbis-BSD.txt"
cp "$K/third_party/vulkan/Apache-2.0.txt" "$L/Vulkan-Headers-Apache-2.0.txt"
# v0.8.0 (docs/N9-L-LICENCAS.md, secao 7): avisos de terceiros do SDL3, HIDAPI, yuv2rgb, creditos e procedencia
T=$work/src/third_party/licencas
for f in AVISOS.txt SDL3-avisos-terceiros.txt HIDAPI-LICENSE.txt HIDAPI-LICENSE-bsd.txt yuv2rgb-BSD-3.txt; do
    [ -f "$T/$f" ] || falha "falta third_party/licencas/$f"; cp "$T/$f" "$L/$f"
done
# os avisos sao das fontes de uma versao do SDL3: o pre-compilado tem de ser dela (revisao Codex 20261104-103026)
sdlv=$(sed -n 's/^set(PACKAGE_VERSION "\([0-9.]*\)").*/\1/p' "$sdl3/lib/cmake/SDL3/SDL3ConfigVersion.cmake" | head -1)
avv=$(sed -n '1s/^Avisos de terceiros dentro do SDL3 \([0-9.]*\) .*/\1/p' "$T/SDL3-avisos-terceiros.txt")
[ -n "$sdlv" ] && [ "$sdlv" = "$avv" ] || falha "SDL3 pre-compilado $sdlv, avisos de terceiros do SDL3 $avv"
cp "$work/src/tools/dist/linux/LEIA-ME-BUILDER.txt" "$stage/LEIA-ME.txt"
cp "$work/src/tools/dist/linux/instalar.sh" "$stage/instalar.sh"; chmod 755 "$stage/instalar.sh" "$stage/bs-builder" "$stage/BlueStinger-Recompiler" "$stage/BlueStinger-Desinstalar"
if [ -n "$toolchain" ]; then
    passo "compilador portatil"
    mkdir -p "$stage/toolchain"; cp -a "$toolchain/." "$stage/toolchain/"
    mkdir -p "$L/compilador"; cp -a "$toolchain/licencas/." "$L/compilador/"
    # textos que o toolchain nao traz (kernel GPL-2.0 com a syscall-note, LGPL-2.1, APSL-2.0, componentes do CMake):
    # do repositorio, com a procedencia; o toolchain/ fica como foi testado
    for f in "$T"/compilador/*.txt; do cp "$f" "$L/compilador/$(basename "$f")"; done
    [ -f "$L/compilador/GPL-2.0.txt" ] && [ -f "$L/compilador/Linux-syscall-note.txt" ] || falha "faltam os textos do kernel"
fi

passo "conferencias"
# nada do jogo: nomes proibidos
if find "$stage" \( -iname '1ST_READ.BIN' -o -iname 'IP.BIN' -o -name generated -o -name game-data -o -name analysis \
     -o -name snapshots -o -iname '*.cdi' -o -iname '*.gdi' -o -iname '*.chd' -o -iname '*.iso' -o -iname '*.sfd' \
     -o -iname '*.mp4' -o -iname '*.adx' -o -iname 'known_versions.h.*' \) -print | grep -q .; then
    find "$stage" \( -iname '1ST_READ.BIN' -o -iname 'IP.BIN' -o -name generated -o -name game-data -iname '*.cdi' -o -iname '*.sfd' -o -iname '*.mp4' \) -print >&2
    falha "conteudo do jogo no pacote"
fi
# o known_versions.h do kit e o publico (o do commit); o particular tem outras versoes
cmp -s "$stage/kit/tools/extractor/known_versions.h" <(git -C "$repo" show "$sha:tools/extractor/known_versions.h") || falha "known_versions.h do kit difere do commit"
grep -q 'hedhunter\|MK-5104150' "$stage/kit/tools/extractor/known_versions.h" && falha "known_versions.h particular no kit"
# bytes do jogo (amostras de 48 bytes com variedade, como no make_builder.ps1)
if [ -n "$gamedata" ]; then
    python3 -I - "$gamedata" "$stage" <<'PY' || falha "bytes do jogo no pacote"
import os, sys
gd, stage = sys.argv[1], sys.argv[2]
needles = []
for name, start, step, want in (("1ST_READ.BIN", 0x1000, 0x31000, 8), ("IP.BIN", 0x400, 0xC00, 6)):
    p = os.path.join(gd, name)
    if not os.path.isfile(p):
        print("aviso: sem", name); continue
    b = open(p, "rb").read(); got = 0; off = start
    while off + 48 <= len(b) and got < want:
        s = b[off:off + 48]
        if len(set(s)) >= 20: needles.append(s); got += 1
        off += step
    if got < 3: sys.exit("amostras insuficientes de " + name)
for root, _, files in os.walk(stage):
    for f in files:
        data = open(os.path.join(root, f), "rb").read()
        for n in needles:
            if n in data: sys.exit(os.path.join(root, f) + " contem bytes do jogo")
print("sem bytes do jogo:", len(needles), "amostras")
PY
else
    echo "aviso: sem --game-data; conferencia so por nomes"
fi
# nenhum caminho do ambiente de desenvolvimento nos binarios nossos (o bs-builder e o SDL3 pre-compilado)
for p in "$repo" "$HOME" "$work" "$sdl3" "$deps" "$out" ${toolchain:+"$toolchain"} "${TMPDIR:-/tmp}/bs-"; do
    for bin in "$stage/bs-builder" "$stage/BlueStinger-Recompiler" "$stage/BlueStinger-Desinstalar" "$stage/kit/third_party/SDL3-linux/lib/libSDL3.a"; do
        if grep -qaF -- "$p" "$bin"; then falha "$(basename "$bin") contem o caminho '$p'"; fi
    done
    # e em qualquer outro arquivo do pacote (texto do pkg-config, CMake gerado...): v0.8.0, o sdl3.pc levava o HOME
    achou=$(grep -rlaF -- "$p" "$stage" | head -3 || true)
    [ -z "$achou" ] || falha "caminho de desenvolvimento '$p' em: $achou"
done
# dependencias dinamicas: so a base do sistema (glibc e o carregador). N9.1: o SDL3 do instalador abre X11, Wayland e o
# portal com dlopen, so ao abrir a janela; a glibc minima de cada binario e a do alvo (2.31)
for b in "${binarios[@]}"; do
    db=$(readelf -d "$stage/$b" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | sort -u | tr '\n' ' ')
    for d in $db; do
        case "$d" in libc.so.6|libm.so.6|libdl.so.2|libpthread.so.0|librt.so.1|ld-linux-x86-64.so.2) ;;
            libstdc++.so.6|libgcc_s.so.1) [ -z "$toolchain" ] || falha "$b depende de $d (o compilador portatil liga o C++ estatico)" ;;
            *) falha "$b depende de $d, fora da base do sistema" ;;
        esac
    done
    [ "$b" = bs-builder ] && deps_bin=$db
    g=$(objdump -T "$stage/$b" | sed -n 's/.*GLIBC_\([0-9.]*\).*/\1/p' | sort -t. -k1,1n -k2,2n -u | tail -1)
    [ -z "$toolchain" ] || [ "$(printf '%s\n2.31\n' "$g" | sort -t. -k1,1n -k2,2n | tail -1)" = 2.31 ] || falha "$b precisa da glibc $g (o alvo e 2.31)"
done
glibc=$(objdump -T "$stage/bs-builder" | sed -n 's/.*GLIBC_\([0-9.]*\).*/\1/p' | sort -t. -k1,1n -k2,2n -u | tail -1)
# responde como esperado (sem argumentos: uso, codigo 2)
set +e; "$stage/bs-builder" > /dev/null 2>&1; rc=$?; set -e
[ "$rc" = 2 ] || falha "bs-builder montado nao respondeu (codigo $rc)"
# o instalador grafico sem janela: as capturas de todas as telas, sem texto cortado (saida 0)
set +e; BS_INSTALADOR_TESTE_RAIZ="$work/raiz-capturas" "$stage/BlueStinger-Recompiler" --captura "$work/capturas" --escalas 100,200 > "$work/capturas.txt" 2>&1; rc=$?; set -e
[ "$rc" = 0 ] || { tail -5 "$work/capturas.txt"; falha "instalador grafico montado: capturas com codigo $rc"; }

passo "VERSAO.txt e SHA256SUMS.txt"
kitn=$(find "$stage/kit" -type f | wc -l); kitmb=$(du -sm "$stage/kit" | cut -f1)
{
    echo "Blue Stinger Builder $versao (Linux x86-64)"
    echo "Distribuicao: Builder (sem conteudo do jogo)"
    echo "Fontes: commit/arvore $sha"
    # SOURCE_DATE_EPOCH (reproducible-builds.org): o mesmo commit e a mesma data dao o mesmo .tar.gz
    echo "Montada em: $(date -u -d "@${SOURCE_DATE_EPOCH:-$(date +%s)}" '+%Y-%m-%d %H:%M') UTC"
    echo "bs-builder: $deps_bin; glibc minima $glibc"
    echo "Compilador do bs-builder: $(if [ -n "$toolchain" ]; then head -1 "$toolchain/VERSAO.txt"; else clang++ --version | head -1; echo " (do sistema: so desenvolvimento)"; fi)"
    echo "Kit de fontes: $kitn arquivos, $kitmb MB (SDL3 pre-compilado $(grep -o 'release-[0-9.]*' "$sdl3/VERSAO-SDL3.txt" 2>/dev/null || echo release-3.2.30); FreeType e toml++ em fontes)"
    if [ -n "$toolchain" ]; then echo "Compilador portatil (toolchain/):"; sed 's/^/  /' "$toolchain/VERSAO.txt"
    else echo "Compilador portatil: NAO incluido (use bs-builder --compilador-sistema)"; fi
} > "$stage/VERSAO.txt"
(cd "$stage" && find . -type f ! -name SHA256SUMS.txt -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS.txt)
(cd "$stage" && sha256sum --quiet -c SHA256SUMS.txt) || falha "SHA256SUMS.txt nao confere"

passo "tar.gz"
rm -rf "${out:?}/$nome" "$out/$nome.tar.gz" "$out/$nome.tar.gz.sha256"
tar --sort=name --mtime='@0' --owner=0 --group=0 --numeric-owner -C "$work" -cf - "$nome" | gzip -9 -n > "$out/$nome.tar.gz"
(cd "$out" && sha256sum "$nome.tar.gz" > "$nome.tar.gz.sha256")
mv "$stage" "$out/$nome"
echo "OK: $out/$nome.tar.gz ($(du -h "$out/$nome.tar.gz" | cut -f1); extraido $(du -sh "$out/$nome" | cut -f1)); $(cut -d' ' -f1 "$out/$nome.tar.gz.sha256")"
