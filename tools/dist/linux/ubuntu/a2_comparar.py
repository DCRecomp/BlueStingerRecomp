#!/usr/bin/env python3
"""N9-L A2 na sessao em Ubuntu nativo: o jogo que o A1 gerou (HOME real do usuario) contra o do Windows, por SHA-256.

    python3 -I a2_comparar.py <pasta do jogo gerado> <win-data> <win-generated> <builder.log da execucao>

Os mesmos criterios do B3 e do B11 da tools/dist/linux/test_builder_linux.py: dados (disc.manifest, disc_meta.*, disc/**)
iguais aos do Windows; C++ gerado no cache que ESTA execucao usou (a linha "cache de compilacao:" do builder.log) igual ao do
Windows; os 16 filmes do manifesto com os mesmos campos da fonte e sem "arquivo" no Linux; NEWGAME, E193 e ENDING2 com o fim
de video arredondado (diferente do de audio). Imprime [ok]/[FALHA] e sai com 0 so sem falhas. So le.
"""
import hashlib
import sys
import tomllib
from pathlib import Path

falhas = 0


def check(nome, cond, detalhe=""):
    global falhas
    print(("[ok] " if cond else "[FALHA] ") + nome + ("" if cond or not detalhe else ": " + detalhe))
    falhas += 0 if cond else 1
    return cond


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def arquivos(raiz):
    return sorted(str(p.relative_to(raiz)) for p in Path(raiz).rglob("*") if p.is_file())


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    jogo, wd, wg, blog = (Path(a) for a in sys.argv[1:])
    data = jogo / "data"
    rels = ["disc.manifest", "disc_meta.bin", "disc_meta.idx"] + ["disc/" + f for f in arquivos(wd / "disc")]
    diffs = [r for r in rels if not (data / r).is_file() or sha(data / r) != sha(wd / r)]
    extras = sorted(set("disc/" + f for f in arquivos(data / "disc")) - set(rels))
    check(f"A2 {len(rels)} arquivos de dados iguais aos do Windows (SHA-256)", not diffs, " ".join(diffs[:10]))
    check("A2 nenhum arquivo de dados a mais no Linux", not extras, " ".join(extras[:10]))

    linhas = [l for l in blog.read_text(errors="replace").splitlines() if l.startswith("cache de compilacao: ")]
    lg = Path(linhas[-1].split(": ", 1)[1].strip()) / "generated" if linhas else None
    if check("A2 C++ gerado no cache desta execucao (builder.log)", lg is not None and lg.is_dir(), "sem 'cache de compilacao:' no log"):
        files = [f for f in arquivos(wg) if f.endswith((".cpp", ".h", ".cmake"))]
        d = [f for f in files if not (lg / f).is_file() or sha(lg / f) != sha(wg / f)]
        check(f"A2 C++ gerado no Linux igual ao do Windows ({len(files)} arquivos)", files and not d, " ".join(d[:10]))

    lin = tomllib.loads((data / "video" / "manifest.toml").read_text())
    win = tomllib.loads((wd / "video" / "manifest.toml").read_text())
    L = {e["original"]: e for e in lin["filme"]}
    W = {e["original"]: e for e in win["filme"]}
    check("A2 manifesto dos filmes: os mesmos 17 IDs", sorted(L) == sorted(W) and len(L) == 17)
    keys = ["quadros", "fps", "largura", "altura", "fim_video_100ns", "fim_audio_100ns", "amostras_audio", "fonte_sha256"]
    diffs, n = [], 0
    for k, w in W.items():
        l = L.get(k, {})
        if w.get("ausente") or l.get("ausente"):
            if bool(w.get("ausente")) != bool(l.get("ausente")):
                diffs.append(k + ":ausente")
            continue
        n += 1
        diffs += [f"{k}:{c}" for c in keys if w.get(c) != l.get(c)]
        if "arquivo" in l:
            diffs.append(k + ":arquivo")
    check(f"A2 {n} filmes com os mesmos campos que no Windows (inclusive os fins arredondados)", n == 16 and not diffs,
          " ".join(diffs[:10]))
    tres = [k for k in ("NEWGAME.SFD", "E193.SFD", "ENDING2.SFD") if k in L and L[k].get("fim_video_100ns") != L[k].get("fim_audio_100ns")]
    check("A2 os 3 filmes do arredondamento (NEWGAME, E193, ENDING2) com o fim de video arredondado", len(tres) == 3, str(tres))
    print(f"{'0 falhas' if not falhas else str(falhas) + ' falha(s)'}")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
