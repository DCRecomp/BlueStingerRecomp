#!/usr/bin/env python3
"""N8.8 (diagnostico do H9): linha do tempo do BS_TRAVAS em volta de cada falta de dados de audio.

Uso: python tools/dev/travas_linha.py <travas.tsv> [--janela-ms 400] [--todos]

Para cada evento audio-falta (ou, sem nenhum, para o maior campo), lista em ordem de inicio os eventos que se
sobrepoem a janela anterior (espera de trava, present, cercas, campos, fila de audio), com o fim de cada um, para
correlacionar quem segurava a fila quando o jogo parou.
"""
import argparse
import csv
import sys


def ler(caminho):
    with open(caminho, encoding="utf-8") as f:
        r = csv.DictReader((l for l in f if not l.startswith("#")), delimiter="\t")  # a 1a linha e a epoca
        ev = []
        for x in r:
            t = float(x["t_s"])
            d = float(x["dur_ms"]) / 1000.0
            ev.append({"t": t, "fim": t + d, "dur_ms": float(x["dur_ms"]), "tipo": x["tipo"], "thread": x["thread"],
                       "vblank": int(x["vblank"]), "render": int(x["render"]), "extra": int(x["extra"])})
    return sorted(ev, key=lambda e: e["t"])


def extra_txt(e):
    if e["tipo"].startswith("audio-"):
        return f"fila {e['extra'] / 10:.1f} ms"
    if e["tipo"] == "campo":
        return f"campo {e['extra']}"
    return ""


def janela(ev, centro, antes_s, depois_s, minimo_ms, todos):
    a, b = centro - antes_s, centro + depois_s
    sel = [e for e in ev if e["fim"] >= a and e["t"] <= b and (todos or e["dur_ms"] >= minimo_ms or e["tipo"].startswith("audio-") and e["tipo"] != "audio-intervalo")]
    for e in sel:
        rel = (e["t"] - centro) * 1000
        print(f"  {rel:+9.1f} ms  dur {e['dur_ms']:8.2f}  fim {(e['fim'] - centro) * 1000:+9.1f}  {e['tipo']:<17} "
              f"thread {e['thread']:<10} vblank {e['vblank']:<6} render {e['render']:<6} {extra_txt(e)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tsv")
    ap.add_argument("--janela-ms", type=float, default=400)
    ap.add_argument("--minimo-ms", type=float, default=8)
    ap.add_argument("--todos", action="store_true", help="inclui os eventos abaixo do minimo (intervalos do audio etc.)")
    a = ap.parse_args()
    ev = ler(a.tsv)
    faltas = [e for e in ev if e["tipo"] == "audio-falta"]
    if not faltas:
        campos = [e for e in ev if e["tipo"] == "campo"]
        if not campos:
            print("sem faltas e sem campos no arquivo")
            return 0
        m = max(campos, key=lambda e: e["dur_ms"])
        print(f"sem faltas de dados; maior campo: {m['dur_ms']:.2f} ms (campo {m['extra']}, t={m['t']:.3f} s)")
        janela(ev, m["fim"], a.janela_ms / 1000, 0.05, a.minimo_ms, a.todos)
        return 0
    for i, f in enumerate(faltas, 1):
        print(f"falta {i}: t={f['t']:.6f} s, vblank {f['vblank']}, render {f['render']}, fila {f['extra'] / 10:.1f} ms")
        janela(ev, f["t"], a.janela_ms / 1000, 0.1, a.minimo_ms, a.todos)
    return 0


if __name__ == "__main__":
    sys.exit(main())
