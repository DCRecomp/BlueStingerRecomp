#!/usr/bin/env bash
# N9-L: compila um commit no Linux a partir do git, nunca da arvore de trabalho. Assim cada resultado fica ligado a um commit
# e nenhum arquivo particular entra. O codigo gerado (local, do jogo do usuario) e copiado para fora do repositorio.
#   tools/dev/linux_build.sh <repo> <commit> <preset> [alvo...]
# Saida: $BS_LINUX_DIR/<commit>/{src,log} (padrao ~/bs-linux). Fontes do SDL3, do toml++ e do FreeType: $BS_LINUX_DIR/deps, se
# existirem (senao o CMake baixa as versoes fixadas).
set -euo pipefail
repo=${1:?repositorio}; commit=${2:?commit}; preset=${3:?preset}; shift 3
base=${BS_LINUX_DIR:-$HOME/bs-linux}
git() { command git -c safe.directory='*' "$@"; }
sha=$(git -C "$repo" rev-parse --short=10 "$commit")
dir=$base/$sha; src=$dir/src; log=$dir/log; mkdir -p "$src" "$log"
if [ ! -f "$src/CMakeLists.txt" ]; then git -C "$repo" archive "$sha" | tar -x -C "$src"; fi
if [ -d "$repo/generated" ] && [ ! -f "$src/generated/generated.cmake" ]; then
    mkdir -p "$src/generated"
    find "$repo/generated" -maxdepth 1 -type f -exec cp -t "$src/generated" {} +
fi
args=(); for d in sdl3 tomlplusplus freetype; do
    u=$(echo $d | tr a-z A-Z); [ -d "$base/deps/$d" ] && args+=("-DFETCHCONTENT_SOURCE_DIR_$u=$base/deps/$d")
done
cd "$src"
{ echo "commit $sha, preset $preset, $(clang++ --version | head -1), $(uname -r)"; } | tee "$log/$preset.ambiente.txt"
cmake --preset "$preset" "${args[@]}" > "$log/$preset.configure.txt" 2>&1 || { tail -20 "$log/$preset.configure.txt"; exit 1; }
if [ $# -gt 0 ]; then t=(--target "$@"); else t=(); fi
cmake --build --preset "$preset" "${t[@]}" -- -k 0 > "$log/$preset.build.txt" 2>&1 || { rc=$?; grep -E 'error|FAILED' "$log/$preset.build.txt" | head -40 | cut -c1-170; echo "build rc=$rc"; exit $rc; }
tail -1 "$log/$preset.build.txt" | cut -c1-120
echo "$dir"
