"""N9.8 fase 8.1 (docs/N9-8-AUDITORIA.md): a filtragem das texturas da Remastered sem janela (Vulkan offscreen), na rota
passo_cgate ate o cais RCG00, com a resolucao interna de 1440 linhas (a filtragem so muda a amostragem: o estado do jogo e o
som nao podem mudar).

Mesmo arranjo do n97_agua (pasta temporaria por execucao, copia do exe e do instalacao.toml da edicao, cartao vazio). Capturas
nos VBlanks 4000, 12000, 30700 e 34200 (a imagem do jogo) e a imagem na resolucao interna (`_hd`) nos dois da cena 3D (30700
e 34200; 4000 e 12000 sao telas desenhadas pela CPU, sem slot). Casos:
  F1 recusas: a Original com --filtragem anisotropica e a Remastered no software com --filtragem bilinear saem com 2;
  F2 Remastered 4:3 em 1440: sem a opcao x --filtragem original x bilinear (trilinear) x anisotropica: estado por passo e
     efeitos sonoros identicos; "filtragem: N desenhos" = 0 sem ela e na original, > 0 nas outras; a original igual bit a bit
     a sem a opcao; a trilinear e a anisotropica mudam a imagem e diferem entre si; o produto igual a validacao; a energia de
     alta frequencia das imagens `_hd` (media do modulo do laplaciano) cai com a filtragem (a cintilacao das texturas longe);
  F3 com o N9.6 e a agua (16:9, 1080): anisotropica + AO + luz + sombras + agua alta x os mesmos sem a filtragem: estado
     identico; os cinco rodaram;
  F4 recuperacao (validacao, BS_VK_FALHAR=32000, transitoria): o dispositivo recriado; no 34200 o quadro da anisotropica sem a
     falha (a filtragem e a cadeia de mipmaps voltam no dispositivo novo).

  python tools/dev/n98_filtragem.py --exe <produto> --exe-validacao <validacao> --data <data> [--casos F1,...] [--manter]
Saida 1 se algum criterio falhar.
"""
import argparse
import os
import re
import shutil
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n95_headless import Corrida  # noqa: E402

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROTEIRO = os.path.join(RAIZ, "tools", "dist", "roteiros", "passo_cgate.txt")
VBLANKS = 34300
VBS = ("004000", "012000", "030700", "034200")
CAPTURAS = ",".join("%d-%d" % (int(v), int(v)) for v in VBS)


def laplaciano(img):
    """a media do modulo do laplaciano do verde: a energia de alta frequencia (cintilacao, serrilhado das texturas)."""
    g = img[:, :, 1]
    return float(np.abs(4 * g[1:-1, 1:-1] - g[:-2, 1:-1] - g[2:, 1:-1] - g[1:-1, :-2] - g[1:-1, 2:]).mean())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--exe-validacao", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--casos", default="F1,F2,F3,F4")
    ap.add_argument("--manter", action="store_true")
    a = ap.parse_args()
    a.exe, a.exe_validacao = os.path.abspath(a.exe), os.path.abspath(a.exe_validacao)
    casos = set(a.casos.split(","))
    base = tempfile.mkdtemp(prefix="n98_filtragem_")
    a.cartao = os.path.join(base, "_cartao_vazio")  # a rota passo_cgate comeca com o cartao vazio (NEW GAME)
    os.makedirs(a.cartao)
    falhas = []

    def check(nome, ok, det=""):
        print("[%s] %s %s" % ("ok" if ok else "FALHA", nome, det), flush=True)
        if not ok:
            falhas.append(nome + " " + det)

    def corrida(nome, edicao, extra, capturar=True, exe=None, env=None):
        c = Corrida(base, nome, a, edicao, extra, capturar, exe=exe, capturas=CAPTURAS)
        c.env["BS_INPUT_SCRIPT"] = ROTEIRO
        c.cmd[c.cmd.index("--headless") + 1] = str(VBLANKS)
        c.env.update(env or {})
        return c

    try:
        r = {}
        nat = ["--render", "nativo", "--resolucao-interna", "1440"]
        wide = ["--render", "nativo", "--proporcao", "16:9", "--resolucao-interna", "1080"]
        fx = ["--ssao", "ligado", "--iluminacao", "ligado", "--sombras", "aprimoradas", "--agua", "alta"]
        val = a.exe_validacao
        if "F1" in casos:
            r["f1o"] = corrida("f1o", "original", ["--render", "nativo", "--filtragem", "anisotropica"], False)
            r["f1s"] = corrida("f1s", "remastered", ["--render", "software", "--filtragem", "bilinear"], False)
        if "F2" in casos or "F4" in casos:
            r["off"] = corrida("off", "remastered", nat)
            r["vani"] = corrida("vani", "remastered", nat + ["--filtragem", "anisotropica"], exe=val)
        if "F2" in casos:
            r["orig"] = corrida("orig", "remastered", nat + ["--filtragem", "original"])
            r["tri"] = corrida("tri", "remastered", nat + ["--filtragem", "bilinear"])
            r["ani"] = corrida("ani", "remastered", nat + ["--filtragem", "anisotropica"])
        if "F3" in casos:
            r["fx"] = corrida("fx", "remastered", wide + fx)
            r["fxf"] = corrida("fxf", "remastered", wide + fx + ["--filtragem", "anisotropica"])
        if "F4" in casos:
            r["falha"] = corrida("falha", "remastered", nat + ["--filtragem", "anisotropica"], exe=val, env={"BS_VK_FALHAR": "32000"})
        with ThreadPoolExecutor(3) as ex:
            list(ex.map(lambda c: c.rodar(), r.values()))

        def filt(c):  # (desenhos, texturas com mipmaps ate, promovidas) do resumo do Vulkan
            m = re.search(r"filtragem: (\d+) desenhos, texturas com mipmaps ate (\d+) \((\d+) promovidas\)", c.log)
            return tuple(int(x) for x in m.groups()) if m else (-1, -1, -1)

        def igual_estado(x, y, nome):
            check(nome + ": estado por passo identico", x.rc == 0 and y.rc == 0 and x.passos() == y.passos() and len(x.passos()) > 1000,
                  "rc %d/%d, %d/%d passos" % (x.rc, y.rc, len(x.passos()) if x.rc == 0 else 0, len(y.passos()) if y.rc == 0 else 0))
            check(nome + ": efeitos sonoros identicos", x.rc == 0 and y.rc == 0 and np.array_equal(x.wav(), y.wav()))

        # a imagem na resolucao interna (`_hd`) so existe nos renders da cena (o slot): 4000 e 12000 sao telas da CPU
        VBS_HD = ("030700", "034200")

        def capturas(c, sufixo):
            return [c.img("v%s%s.bmp" % (v, sufixo)) for v in (VBS_HD if sufixo == "_hd" else VBS)]

        if "F1" in casos:
            check("F1 Original com --filtragem anisotropica recusada", r["f1o"].rc == 2, "rc %d" % r["f1o"].rc)
            check("F1 Remastered no software com --filtragem bilinear recusada", r["f1s"].rc == 2, "rc %d" % r["f1s"].rc)
        if "F2" in casos:
            off, orig, tri, ani, vani = r["off"], r["orig"], r["tri"], r["ani"], r["vani"]
            for c, nome in ((orig, "original"), (tri, "trilinear"), (ani, "anisotropica"), (vani, "anisotropica (validacao)")):
                igual_estado(off, c, "F2 sem a opcao x %s" % nome)
            fo, fg, ft, fa = filt(off), filt(orig), filt(tri), filt(ani)
            check("F2 a variante filtrada so com a filtragem", fo[0] == 0 and fg[0] == 0 and ft[0] > 0 and fa[0] > 0,
                  "sem %s, original %s, trilinear %s, anisotropica %s" % (fo, fg, ft, fa))
            check("F2 texturas com mipmaps so com a filtragem", fo[1] == 0 and fg[1] == 0 and ft[1] > 0 and fa[1] > 0)
            check("F2 log da filtragem", "[render] filtragem: anisotropica 16x" in ani.log and "[render] filtragem: trilinear" in tri.log
                  and "[render] filtragem:" not in off.log)
            for sufixo, nome in (("", "imagem do jogo"), ("_hd", "imagem em 1440")):
                co, cg, ct, ca, cv = capturas(off, sufixo), capturas(orig, sufixo), capturas(tri, sufixo), capturas(ani, sufixo), capturas(vani, sufixo)
                presentes = all(x is not None for x in co + cg + ct + ca + cv)
                check("F2 %s: capturas" % nome, presentes)
                if not presentes:
                    continue
                check("F2 %s: a original igual a sem a opcao" % nome, all(np.array_equal(x, y) for x, y in zip(co, cg)))
                mt = sum(int((x != y).any(axis=2).sum()) for x, y in zip(co, ct))
                ma = sum(int((x != y).any(axis=2).sum()) for x, y in zip(ct, ca))
                check("F2 %s: a trilinear muda a imagem e a anisotropica difere dela" % nome, mt > 0 and ma > 0, "%d e %d pixels" % (mt, ma))
                check("F2 %s: o produto igual a validacao" % nome, all(np.array_equal(x, y) for x, y in zip(ca, cv)))
                if sufixo == "_hd":  # uma captura sem textura minificada (tela de menu, filme) fica igual
                    so, st_ = 0.0, 0.0
                    for v, x, y, z in zip(VBS_HD, co, ct, ca):
                        lo, lt, la = laplaciano(x), laplaciano(y), laplaciano(z)
                        so, st_ = so + lo, st_ + lt
                        print("  %s: alta frequencia sem %.2f, trilinear %.2f, anisotropica %.2f" % (v, lo, lt, la))
                        check("F2 %s: a trilinear nao aumenta a alta frequencia" % v, lt <= lo + 0.01, "%.2f -> %.2f" % (lo, lt))
                    check("F2 a trilinear reduz a alta frequencia no conjunto", st_ < so, "%.2f -> %.2f" % (so, st_))
        if "F3" in casos:
            fxr, fxf = r["fx"], r["fxf"]
            igual_estado(fxr, fxf, "F3 AO + luz + sombras + agua x com a filtragem")
            check("F3 os cinco rodaram", filt(fxf)[0] > 0 and re.search(r"AO em [1-9]\d* renders", fxf.log) is not None and
                  re.search(r"agua em [1-9]\d* renders", fxf.log) is not None, "filtragem %s" % (filt(fxf),))
        if "F4" in casos:
            f, off, vani = r["falha"], r["off"], r["vani"]
            igual_estado(off, f, "F4 recuperacao")
            check("F4 o dispositivo recriado", "falha do Vulkan injetada no VBlank 32000" in f.log and "dispositivo recriado 1 vez" in f.log)
            for sufixo in ("", "_hd"):
                check("F4 034200%s: o quadro da anisotropica sem a falha" % sufixo, f.img("v034200%s.bmp" % sufixo) is not None and
                      np.array_equal(f.img("v034200%s.bmp" % sufixo), vani.img("v034200%s.bmp" % sufixo)))
    finally:
        print("%d falha(s) (%s)" % (len(falhas), base))
        if not a.manter and not falhas:
            shutil.rmtree(base, ignore_errors=True)
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
