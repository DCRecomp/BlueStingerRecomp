#!/usr/bin/env bash
# N9-L A1 e A2 na sessao em Ubuntu nativo: do pacote .tar.gz ao jogo, na HOME real do Live, SEM instalar pacote nenhum.
#   bash a1_a2.sh [original|remastered]     (padrao: original; a segunda execucao com remastered testa a troca de edicao)
# Grava tudo em evidencias/a1/ e anota A1 e A2 em resultados.tsv. Nao usa sudo. O CDI so e lido (hash antes e depois).
set -uo pipefail
source "$(dirname "$0")/comum.sh"
edicao=${1:-original}
case "$edicao" in original|remastered) ;; *) falha "edicao: original ou remastered" ;; esac
d="$BS_EVID/a1"; mkdir -p "$d"
log="$d/a1-$edicao.txt"
exec > >(tee -a >(privacidade >> "$log")) 2>&1
echo "== A1/A2 ($edicao) $(agora)"
[ -d "$BS_TRAB" ] && [ -w "$BS_TRAB" ] || falha "pasta de trabalho $BS_TRAB indisponivel: rode preparar_sessao.sh"
cdi="$BS_PRIVADO/Blue Stinger.cdi"
[ -f "$cdi" ] || falha "sem o CDI em $cdi"

# 0. nada de compilador, cmake ou wine do sistema (o A1 e sem pacotes)
echo "-- ferramentas do sistema que o A1 nao pode usar:"
for t in cc gcc g++ clang clang++ cmake ninja wine wine64 proton; do command -v "$t" >/dev/null && echo "   PRESENTE: $t ($(command -v "$t"))"; done
echo "   (fim da lista; vazia = nenhuma)"

pac=$(ls "$BS_PUBLICO"/BlueStinger-Builder-*-linux-x86_64.tar.gz 2>/dev/null | head -1)
[ -n "$pac" ] || falha "sem o pacote em $BS_PUBLICO"
if [ "$edicao" = original ]; then
    cdi0=$(sha "$cdi"); echo "CDI sha256 antes: $cdi0"
    # 1. integridade do pacote
    echo "-- 1. SHA-256 do pacote"
    ( cd "$BS_PUBLICO" && sha256sum -c "$(basename "$pac").sha256" ) || falha "pacote corrompido"
    # 2. extrair e instalar (destino padrao ~/.local/share/BlueStinger-Builder, atalho ~/.local/bin/bs-builder)
    echo "-- 2. extrair e instalar"
    rm -rf "$BS_TRAB/pacote"; mkdir -p "$BS_TRAB/pacote"
    tar -xzf "$pac" -C "$BS_TRAB/pacote" || falha "tar falhou"
    p=$(ls -d "$BS_TRAB/pacote"/BlueStinger-Builder-*-linux-x86_64 | head -1)
    ( cd "$p" && ./instalar.sh --atalho ) || falha "instalar.sh falhou"
    ( cd "$p" && ./instalar.sh --verificar ) || falha "instalar.sh --verificar falhou"
    echo "-- conteudo do pacote: o compilador portatil esta la (D-L2)"
    ls "$HOME/.local/share/BlueStinger-Builder/toolchain/zig/zig" "$HOME/.local/share/BlueStinger-Builder/toolchain/bin/" 2>&1
    "$HOME/.local/share/BlueStinger-Builder/toolchain/zig/zig" version 2>&1 | sed 's/^/   zig /'
fi
[ -x "$BS_BUILDER" ] || falha "sem $BS_BUILDER (instale com a1_a2.sh original primeiro)"
head -3 "$HOME/.local/share/BlueStinger-Builder/VERSAO.txt"

# 3. do CDI ao jogo
# arquivos do usuario (saves, mods, configuracoes): hashes antes e depois de gerar o jogo de novo
do_usuario() {
    { [ -d "$BS_JOGO" ] && find "$BS_JOGO" -maxdepth 1 -type f \( -name 'controles.toml' -o -name 'graficos.toml' -o -name 'legendas.toml' -o -name 'audio.toml' \) -print0
      for p in "$BS_JOGO/mods" "$BS_JOGO/saves" "$HOME/.local/share/DC-Recomp" "$HOME/.config/BlueStingerRecomp"; do
          [ -d "$p" ] && find "$p" -type f ! -name '*.log' -print0
      done; } 2>/dev/null | sort -z | xargs -0 -r sha256sum
}
antes=$(do_usuario)

echo "-- 3. bs-builder --disc <CDI> --out \"$BS_JOGO_DIR\" --edicao $edicao"
mkdir -p "$BS_TRAB/chamada"; ini=$(date +%s)
( cd "$BS_TRAB/chamada" && "$BS_BUILDER" --disc "$cdi" --out "$BS_JOGO_DIR" --edicao "$edicao" ) 2>&1 | tee "$BS_TRAB/builder-$edicao.txt"
rc=${PIPESTATUS[0]}; seg=$(( $(date +%s) - ini ))
echo "bs-builder rc=$rc em ${seg} s"
privacidade < "$BS_TRAB/builder-$edicao.txt" > "$d/builder-$edicao.txt"
[ -z "$(ls -A "$BS_TRAB/chamada")" ] && echo "nada gravado na pasta de onde o Builder foi chamado" || echo "ATENCAO: arquivos na pasta de chamada: $(ls -A "$BS_TRAB/chamada")"
[ "$rc" -eq 0 ] || { anotar "A1 Builder ($edicao)" falha "bs-builder rc=$rc; ver a1/builder-$edicao.txt"; exit 1; }
depois=$(do_usuario)
if [ -n "$antes" ]; then
    n=$(printf '%s\n' "$antes" | wc -l)
    if [ "$antes" = "$depois" ]; then
        anotar "A1.preserva-arquivos-do-usuario" ok "$n arquivos (saves, mods, configuracoes) iguais depois de gerar o jogo de novo ($edicao)"
    else
        diff <(printf '%s\n' "$antes") <(printf '%s\n' "$depois") | privacidade
        anotar "A1.preserva-arquivos-do-usuario" falha "arquivos do usuario mudaram ao gerar o jogo de novo ($edicao); diff no a1-$edicao.txt"
    fi
fi

# 4. o jogo gerado
echo "-- 4. jogo gerado"
file "$BS_EXE"
( cd "$BS_JOGO" && ./BlueStinger --version ) 2>&1
( cd "$BS_JOGO" && ./BlueStinger --verify-data ) 2>&1 | tail -3; vrc=${PIPESTATUS[0]}
echo "--verify-data rc=$vrc"
echo "-- dependencias do jogo (nada de Wine ou Proton):"
ldd "$BS_EXE"
nw=$(ldd "$BS_EXE" | grep -ciE 'wine|proton')
echo "-- edicao: instalacao.toml e o que o jogo le (boot curto sem janela)"; cat "$BS_JOGO/instalacao.toml" 2>/dev/null
perfil=$( cd "$BS_JOGO" && ./BlueStinger --headless 60 2>&1 | grep -E '^\[(edicao|camera)\]' | tr '\n' ' ')
echo "   $perfil"
echo "-- compilador usado (log do Builder):"
grep -iE 'zig|toolchain|compilador|CMAKE_(C|CXX)_COMPILER' "$BS_JOGO_DIR/.bs-build/builder.log" 2>/dev/null | head -8
usou_zig=$(grep -ciE 'BlueStinger-Builder/toolchain|toolchain/zig' "$BS_JOGO_DIR/.bs-build/builder.log" 2>/dev/null)
echo "-- pastas XDG depois do Builder:"
for p in "$HOME/.local/share/BlueStinger-Builder" "$HOME/.cache/BlueStingerRecomp" "$HOME/.config/BlueStingerRecomp" "$HOME/.local/share/DC-Recomp"; do
    printf '   %-60s %s\n' "$p" "$( [ -d "$p" ] && du -sh "$p" 2>/dev/null | cut -f1 || echo '(nao existe)')"
done
cdi1=$(sha "$cdi"); echo "CDI sha256 depois: $cdi1"

ok=1; [ "$vrc" -eq 0 ] && [ "$nw" -eq 0 ] && [ "${usou_zig:-0}" -gt 0 ] || ok=0
if [ "$edicao" = original ]; then
    [ "$cdi0" = "$cdi1" ] || { ok=0; echo "ATENCAO: o CDI mudou"; }
    [[ "$perfil" == *"perfil original"* && "$perfil" != *"modo livre"* ]] || { ok=0; echo "ATENCAO: o jogo nao leu a Original"; }
    anotar "A1 do CDI ao jogo, sem pacotes, sem Wine/Proton" "$([ $ok = 1 ] && echo ok || echo falha)" \
        "builder ${seg} s rc=$rc; verify-data rc=$vrc; wine/proton nas dependencias: $nw; zig do pacote no log: ${usou_zig:-0}; CDI inalterado"
    # 5. A2: dados e C++ gerado iguais aos do Windows, manifesto dos filmes
    echo "-- 5. A2"
    python3 -I "$BS_FERRAMENTAS/a2_comparar.py" "$BS_JOGO" "$BS_PRIVADO/win-data" "$BS_PRIVADO/win-generated" "$BS_JOGO_DIR/.bs-build/builder.log" \
        | privacidade | tee "$d/a2.txt"
    a2=${PIPESTATUS[0]}
    anotar "A2 dados, C++ e manifesto iguais aos do Windows" "$([ "$a2" = 0 ] && echo ok || echo falha)" "ver a1/a2.txt"
else
    [[ "$perfil" == *"perfil remastered"* && "$perfil" == *"modo livre"* ]] || { ok=0; echo "ATENCAO: o jogo nao leu a Remastered com a camera livre"; }
    anotar "A1 edicao Remastered pelo Builder" "$([ $ok = 1 ] && echo ok || echo falha)" \
        "builder ${seg} s rc=$rc; verify-data rc=$vrc; jogo: $perfil"
fi
echo "== fim A1/A2 ($edicao)"
