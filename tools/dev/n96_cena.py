"""N9.6 fase 2a (so validacao; docs/N9-6-AUDITORIA.md secao 8): o alarme da marca de cena.

O AO da Remastered so age nos desenhos opacos e punch-through cujo 1/w varia no desenho (native/render/frame_prep.cpp,
is_scene3d). A revisao Codex 20261110-113738 apontou o caso de uma superficie 3D de frente exata para a camera (1/w
constante), que ficaria fora como o HUD. Medido na fase 2a: nas cenas capturadas, nenhum desenho opaco de cena tem 1/w
constante, e a marca pela proveniencia nao entrou no produto (decisao do mantenedor, 2026-11-10). Este alarme mantem a
pergunta aberta: le os TSVs de proveniencia (<nome>.ir.origem.tsv do comando `ir` do assistido, n95_proveniencia.py
capturar) e falha se algum desenho opaco ou punch-through vindo das entradas 3D do Ninja (as linhas de cena de
runtime/kamui/hud_table.h) tiver 1/w constante. Se o alarme tocar, a marca volta a ser avaliada.

  python tools/dev/n96_cena.py <pasta com *.ir.origem.tsv> [<pasta> ...]
Saida 1 se o alarme tocar (ou se nenhum TSV for lido); 0 caso contrario.
"""
import csv
import glob
import os
import re
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(AQUI))
# as entradas do Ninja que so desenham geometria projetada (hud_table.h: DrawClass::Scene, HudRole::None, "modelos" e
# "poligonos projetados"); os sprites e particulas de cena (8C16938C, 8C1692CC) ficam de fora de proposito: o AO nao os quer
NINJA = (0x8C15F856, 0x8C15F8B8, 0x8C161738, 0x8C160C12, 0x8C1601FE)
OPACAS = ("0", "4")  # native::render::ListType Opaque e PunchThrough


def confere_tabela():
    """Cada entrada usada aqui tem de ser uma linha de cena do hud_table.h (a tabela medida e a fonte)."""
    t = open(os.path.join(RAIZ, "runtime", "kamui", "hud_table.h"), encoding="utf-8").read()
    cena = {int(m.group(1), 16) for m in re.finditer(r"\{0x([0-9A-F]{8}), 0x[0-9A-F]{8}, [^,]+, DrawClass::Scene, HudRole::None", t)}
    falta = [e for e in NINJA if e not in cena]
    if falta:
        sys.exit("hud_table.h sem as entradas de cena %s" % ", ".join("%08X" % e for e in falta))


def main(pastas):
    confere_tabela()
    tsvs = sorted(t for p in pastas for t in glob.glob(os.path.join(p, "**", "*.ir.origem.tsv"), recursive=True))
    if not tsvs:
        print("FALHA: nenhum TSV de proveniencia em %s" % ", ".join(pastas))
        return 1
    ninja = constantes = 0
    alarmes = []
    for tsv in tsvs:
        for r in csv.DictReader(open(tsv, encoding="utf-8"), delimiter="\t"):
            if r["lista"] not in OPACAS or r["cadeia"] == "-":
                continue
            cadeia = {int(x, 16) for x in r["cadeia"].split(">")}
            if not cadeia & set(NINJA):
                continue
            ninja += 1
            lo, hi = float(r["invw_min"]), float(r["invw_max"])
            if not hi > lo:
                constantes += 1
                alarmes.append("%s desenho %s (lista %s, 1/w %s..%s, cadeia %s)" % (
                    os.path.relpath(tsv), r["desenho"], r["lista"], r["invw_min"], r["invw_max"], r["cadeia"]))
    for a in alarmes[:20]:
        print("ALARME " + a)
    print("%d TSVs; %d desenhos opacos das entradas 3D do Ninja; %d com 1/w constante (fora do AO)" % (len(tsvs), ninja, constantes))
    if not ninja:
        print("FALHA: nenhum desenho das entradas do Ninja (a captura nao tem gameplay?)")
        return 1
    return 1 if alarmes else 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1:]))
