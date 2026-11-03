"""N9-L (L4): compara a medida do texto do Windows (DirectWrite, Segoe UI) com a do Linux (FreeType, Noto Sans), gravada pelo teste
text_geometry_same_rules_both_platforms com BS_TEXT_GEOM=<pasta> nas duas plataformas.

A fonte e outra, entao a largura muda; o resto do layout tem de ser o mesmo. Limites fixados (2026-11-03):
  - mesmo numero de linhas em cada caso;
  - centro horizontal da tinta: diferenca <= 1 % da largura da camada;
  - base (ultima linha com tinta): diferenca <= 1 % da altura da camada;
  - topo: diferenca <= 3 % da altura (a altura das letras muda com a fonte);
  - largura da tinta: Linux / Windows entre 0,85 e 1,20, menos nos casos "*quebra*" (com outra largura de fonte a quebra cai em
    outra palavra e as linhas tem outro conteudo: so informada);
  - areas do mouse do menu (menu_linhas): identicas.
Os textos dos casos nao tem letras que descem (g, j, p, q, y) salvo o de acentos, entao a base medida e a linha de base.

    python tools/dev/texto_geometria.py <windows/texto_geometria.tsv> <linux/texto_geometria.tsv>
"""
import sys


def ler(p):
    casos, linhas_menu = {}, None
    with open(p, encoding="utf-8") as f:
        next(f)
        for l in f:
            c = l.rstrip("\n").split("\t")
            if c[0] == "menu_linhas":
                linhas_menu = c[1].strip()
            else:
                casos[c[0]] = dict(zip(["w", "h", "x0", "y0", "x1", "y1", "linhas", "pixels"], map(int, c[1:9])))
    return casos, linhas_menu


def main():
    win, mw = ler(sys.argv[1])
    lin, ml = ler(sys.argv[2])
    falhas = 0
    print("caso\tlinhas\tcentro_dx_pct\tbase_dy_pct\ttopo_dy_pct\tlargura_razao\tresultado")
    for nome in win:
        a, b = win[nome], lin.get(nome)
        if b is None:
            print(f"{nome}\tausente no Linux\t\t\t\t\tFALHA")
            falhas += 1
            continue
        cx = abs((a["x0"] + a["x1"]) - (b["x0"] + b["x1"])) / 2 / a["w"] * 100
        base = abs(a["y1"] - b["y1"]) / a["h"] * 100
        topo = abs(a["y0"] - b["y0"]) / a["h"] * 100
        razao = (b["x1"] - b["x0"] + 1) / (a["x1"] - a["x0"] + 1)
        ok = a["linhas"] == b["linhas"] and cx <= 1 and base <= 1 and topo <= 3 and ("quebra" in nome or 0.85 <= razao <= 1.20)
        falhas += not ok
        print(f"{nome}\t{a['linhas']}/{b['linhas']}\t{cx:.2f}\t{base:.2f}\t{topo:.2f}\t{razao:.3f}\t{'ok' if ok else 'FALHA'}")
    menu_ok = mw == ml and mw is not None
    falhas += not menu_ok
    print(f"menu_linhas\t{'iguais' if menu_ok else 'DIFERENTES'}")
    print(f"{len(win)} casos, {falhas} falhas")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
