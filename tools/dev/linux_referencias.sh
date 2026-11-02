#!/usr/bin/env bash
# N9-L (L2): confere o executavel Linux de um commit contra as referencias gravadas no Windows, com a mesma ferramenta
# (tools/dev/n8_referencias.py). Os artefatos do jogo vem do Windows e ficam so dentro desta maquina, fora do repositorio.
#   tools/dev/linux_referencias.sh <repo> <commit> <n80|n87|ambas> [--casos a,b]
# Resultados: $BS_LINUX_DIR/<commit>/refs/<conjunto>.txt
set -euo pipefail
repo=${1:?repositorio}; commit=${2:?commit}; quais=${3:?n80, n87 ou ambas}; shift 3
base=${BS_LINUX_DIR:-$HOME/bs-linux}
sha=$(git -c safe.directory='*' -C "$repo" rev-parse --short=10 "$commit")
dir=$base/$sha; src=$dir/src; inst=$dir/inst; refs=$dir/refs
winla=${BS_WIN_LOCALAPPDATA:?caminho do LOCALAPPDATA do Windows, como /mnt/c/Users/<usuario>/AppData/Local}
export LOCALAPPDATA=$base/la TMPDIR=$base/tmp; mkdir -p "$TMPDIR" "$LOCALAPPDATA/BlueStingerRecomp/n6f/jogo/g1" "$refs" "$inst"
[ -d "$LOCALAPPDATA/BlueStingerRecomp/gravacoes" ] || cp -r "$winla/BlueStingerRecomp/gravacoes" "$LOCALAPPDATA/BlueStingerRecomp/"
cp -n "$winla/BlueStingerRecomp/n6f/jogo/g1/BLUESTIN.000" "$LOCALAPPDATA/BlueStingerRecomp/n6f/jogo/g1/" 2>/dev/null || true
for c in referencias referencias_n87; do [ -d "$base/ref/$c" ] || { mkdir -p "$base/ref"; cp -r "$repo/analysis/n8/$c" "$base/ref/"; }; done
cp "$src/build/linux-release/app/bluestinger" "$inst/BlueStinger"
[ -d "$inst/mods" ] || cp -r "$repo/dist/BlueStinger-Preview/mods" "$inst/mods"
{ echo "commit $sha"; sha256sum "$inst/BlueStinger"; sha256sum "$base/data/disc.manifest"; clang++ --version | head -1; uname -r; } | tee "$refs/entradas.txt"
cd "$repo"; export PYTHONDONTWRITEBYTECODE=1
rodar() {
    local nome=$1; shift
    python3 -u tools/dev/n8_referencias.py conferir --exe "$inst/BlueStinger" --data "$base/data" --filmes-outra-fonte "$@" > "$refs/$nome.txt" 2>&1
    local rc=$?
    echo "$nome rc=$rc: $(tail -1 "$refs/$nome.txt" | cut -c1-120)"
    return $rc
}
rc=0
if [ "$quais" != n87 ]; then
    rodar n80 --ref "$base/ref/referencias" --excecoes config/n8_1_excecoes_spc.toml --paralelo 4 --ambiente BS_RENDER_SEM_FOG_VOLUMES=1 "$@" || rc=1
fi
if [ "$quais" != n80 ]; then
    rodar n87 --ref "$base/ref/referencias_n87" --paralelo 4 "$@" || rc=1
fi
exit $rc
