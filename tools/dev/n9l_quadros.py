"""N9-L (L2): onde os quadros de duas execucoes do mesmo caso diferem, separados por trecho: fora do filme tem de ser igual.
A coluna do passo de jogo ajuda a situar; um filme toca dentro de um unico passo, entao aparece como um trecho de quadros com o
mesmo passo. So leitura.
    python tools/dev/n9l_quadros.py <ref/caso> <nova/caso>
"""
import os
import sys


def ler(pasta):
    with open(os.path.join(pasta, "quadros.tsv"), encoding="utf-8") as f:
        return [ln.rstrip("\n").split("\t") for ln in f if ln.strip()]


def main():
    a, b = ler(sys.argv[1]), ler(sys.argv[2])
    print("linhas", len(a), len(b), "exemplo", a[len(a) // 2], b[len(b) // 2])
    h = 1  # vblank, hash, ..., passo
    ha, hb = {}, {}
    for n, x in ((ha, a), (hb, b)):
        for ln in x:
            n[ln[h]] = n.get(ln[h], 0) + 1
    so_a = sum(v for k, v in ha.items() if k not in hb)
    so_b = sum(v for k, v in hb.items() if k not in ha)
    print("hashes distintos", len(ha), len(hb), "quadros com hash so na ref", so_a, "so na nova", so_b)
    if len(a) == len(b):  # mesmo VBlank nas duas: em quais passos a imagem difere
        por = {}
        for x, y in zip(a, b):
            d = por.setdefault(x[3], [0, 0])
            d[0] += 1
            d[1] += x[h] != y[h]
        print("passos", len(por), "com diferenca:", {k: tuple(v) for k, v in por.items() if v[1]}, "fora de ordem:", sum(x[3] != y[3] for x, y in zip(a, b)))
    # alinhamento pelas pontas: prefixo e sufixo iguais
    p = 0
    while p < min(len(a), len(b)) and a[p][h] == b[p][h]:
        p += 1
    s = 0
    while s < min(len(a), len(b)) - p and a[-1 - s][h] == b[-1 - s][h]:
        s += 1
    print("prefixo igual", p, "sufixo igual", s, "miolo ref", len(a) - p - s, "miolo nova", len(b) - p - s)
    print("ref  nas pontas do miolo:", a[max(p - 1, 0)], a[p] if p < len(a) else None, a[-1 - s] if s < len(a) else None, a[len(a) - s] if s else None)
    print("nova nas pontas do miolo:", b[max(p - 1, 0)], b[p] if p < len(b) else None, b[-1 - s] if s < len(b) else None, b[len(b) - s] if s else None)


if __name__ == "__main__":
    main()
