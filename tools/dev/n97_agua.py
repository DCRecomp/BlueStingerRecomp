"""N9.7 (docs/N9-7-PLANO.md, A4): a agua aprimorada da Remastered sem janela (Vulkan offscreen), na rota passo_cgate ate o
cais RCG00 (o mar aparece nos VBlanks ~30 700 a ~34 200; levantamento de 2026-11-11 com BS_AGUA_DIAGNOSTICO).

Mesmo arranjo do n96_luz (pasta temporaria por execucao, copia do exe e do instalacao.toml da edicao, cartao vazio).
Capturas nos VBlanks 30700, 33500 e 34200. Casos:
  W1 recusas: a Original com --agua alta e a Remastered no software com --agua alta saem com 2;
  W2 Remastered 4:3: agua original x aprimorada x alta (produto) x alta (validacao) x as duas mascaras (validacao,
     BS_AGUA_DIAGNOSTICO=1 magenta e =2 verde, e =3 e =4 o mesmo sem o movimento da textura: o pixel diferente entre magenta
     e verde, numa das duas, e o dominio dos desenhos de agua, a pegada animada ou a parada): estado por
     passo e efeitos sonoros identicos; "agua em N renders" > 0 so com a agua, com desenhos > 0 e a imagem da cena do tamanho
     do alvo (640 x 480 x 4); nas capturas, a agua muda pixels e so dentro do dominio; aprimorada e alta diferem; o produto e
     a validacao dao o mesmo quadro (reprodutivel entre processos);
  W3 Remastered 16:9 em 1080 linhas: original x alta, com as mascaras no mesmo arranjo: estado e efeitos identicos; so o
     dominio muda na imagem larga; a imagem da cena do tamanho do alvo largo;
  W4 com o AO, a luz e as sombras do N9.6 (16:9, 1080): os tres x os tres com a agua: estado identico; so o dominio muda;
  W5 transicoes (validacao, BS_AGUA_TROCA): alta, desligada no VBlank 32000 e alta de novo no 34000: o estado identico;
     no 30700 o quadro da alta, no 33500 o da original, no 34200 o da alta (a fase e o numero do render: sem salto);
  W6 recuperacao (validacao, BS_VK_FALHAR=32000, transitoria): o dispositivo recriado; no 33500 e no 34200 os quadros da
     alta sem a falha (a fase sobrevive a recriacao).

  python tools/dev/n97_agua.py --exe <produto> --exe-validacao <validacao> --data <data> [--casos W1,...] [--manter]
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
VBS = ("030700", "033500", "034200")
CAPTURAS = ",".join("%d-%d" % (int(v), int(v)) for v in VBS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--exe-validacao", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--casos", default="W1,W2,W3,W4,W5,W6")
    ap.add_argument("--manter", action="store_true")
    a = ap.parse_args()
    a.exe, a.exe_validacao = os.path.abspath(a.exe), os.path.abspath(a.exe_validacao)
    casos = set(a.casos.split(","))
    base = tempfile.mkdtemp(prefix="n97_agua_")
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
        nat = ["--render", "nativo"]
        wide = ["--proporcao", "16:9", "--resolucao-interna", "1080"]
        fx = ["--ssao", "ligado", "--iluminacao", "ligado", "--sombras", "aprimoradas"]
        val = a.exe_validacao
        if "W1" in casos:
            r["w1o"] = corrida("w1o", "original", nat + ["--agua", "alta"], False)
            r["w1s"] = corrida("w1s", "remastered", ["--render", "software", "--agua", "alta"], False)
        if "W2" in casos or "W5" in casos or "W6" in casos:
            r["off"] = corrida("off", "remastered", nat, exe=val)
            r["vhigh"] = corrida("vhigh", "remastered", nat + ["--agua", "alta"], exe=val)
        if "W2" in casos:
            r["enh"] = corrida("enh", "remastered", nat + ["--agua", "aprimorada"])
            r["high"] = corrida("high", "remastered", nat + ["--agua", "alta"])
            for nome, v in (("m1", "1"), ("m2", "2"), ("m3", "3"), ("m4", "4")):
                r[nome] = corrida(nome, "remastered", nat + ["--agua", "alta"], exe=val, env={"BS_AGUA_DIAGNOSTICO": v})
        if "W3" in casos or "W4" in casos:
            for nome, v in (("wm1", "1"), ("wm2", "2"), ("wm3", "3"), ("wm4", "4")):
                r[nome] = corrida(nome, "remastered", nat + wide + ["--agua", "alta"], exe=val, env={"BS_AGUA_DIAGNOSTICO": v})
        if "W3" in casos:
            r["woff"] = corrida("woff", "remastered", nat + wide)
            r["whigh"] = corrida("whigh", "remastered", nat + wide + ["--agua", "alta"])
        if "W4" in casos:
            r["fx"] = corrida("fx", "remastered", nat + wide + fx)
            r["fxw"] = corrida("fxw", "remastered", nat + wide + fx + ["--agua", "alta"])
        if "W5" in casos:
            r["troca"] = corrida("troca", "remastered", nat + ["--agua", "alta"], exe=val, env={"BS_AGUA_TROCA": "32000:0,34000:2"})
        if "W6" in casos:
            r["falha"] = corrida("falha", "remastered", nat + ["--agua", "alta"], exe=val, env={"BS_VK_FALHAR": "32000"})
        with ThreadPoolExecutor(3) as ex:
            list(ex.map(lambda c: c.rodar(), r.values()))

        def agua(c):  # (renders, desenhos, KiB da imagem da cena) do resumo do Vulkan
            m = re.search(r"agua em (\d+) renders \((\d+) desenhos, imagem da cena ate (\d+) KiB\)", c.log)
            return tuple(int(x) for x in m.groups()) if m else (-1, -1, -1)

        def igual_estado(x, y, nome):
            check(nome + ": estado por passo identico", x.rc == 0 and y.rc == 0 and x.passos() == y.passos() and len(x.passos()) > 1000,
                  "rc %d/%d, %d/%d passos" % (x.rc, y.rc, len(x.passos()) if x.rc == 0 else 0, len(y.passos()) if y.rc == 0 else 0))
            check(nome + ": efeitos sonoros identicos", x.rc == 0 and y.rc == 0 and np.array_equal(x.wav(), y.wav()))

        # o dominio da agua: os pixels em que os desenhos de agua aparecem com o movimento da textura (mascaras 1 e 2) ou sem
        # ele (3 e 4: onde a agua desligada aparecia; o alfa da textura muda de lugar com o movimento)
        def dominio(ms, sufixo, v):
            gs = [m.img("v%s%s.bmp" % (v, sufixo)) for m in ms]
            if any(g is None or g.shape != gs[0].shape for g in gs):
                return None
            return (gs[0] != gs[1]).any(axis=2) | (gs[2] != gs[3]).any(axis=2)

        def so_no_dominio(off, on, ms, nome, sufixo=""):
            mudou_total = 0
            for v in VBS:
                g0, g1, dom = off.img("v%s%s.bmp" % (v, sufixo)), on.img("v%s%s.bmp" % (v, sufixo)), dominio(ms, sufixo, v)
                if g0 is None or g1 is None or dom is None or g0.shape != g1.shape or dom.shape != g0.shape[:2]:
                    check("%s %s: capturas" % (nome, v), False, "ausente ou de outro tamanho")
                    continue
                mudou = (g0 != g1).any(axis=2)
                fora = int((mudou & ~dom).sum())
                mudou_total += int(mudou.sum())
                check("%s %s: so o dominio da agua muda" % (nome, v), fora == 0,
                      "%d pixels mudaram, %d fora do dominio (%d pixels de agua)" % (int(mudou.sum()), fora, int(dom.sum())))
            check("%s: a agua muda a imagem" % nome, mudou_total > 0, "%d pixels nas capturas" % mudou_total)

        if "W1" in casos:
            check("W1 Original com --agua alta recusada", r["w1o"].rc == 2, "rc %d" % r["w1o"].rc)
            check("W1 Remastered no software com --agua alta recusada", r["w1s"].rc == 2, "rc %d" % r["w1s"].rc)
        if "W2" in casos:
            off, enh, high, vhigh = r["off"], r["enh"], r["high"], r["vhigh"]
            for c, nome in ((enh, "aprimorada"), (high, "alta"), (vhigh, "alta (validacao)")):
                igual_estado(off, c, "W2 4:3 original x %s" % nome)
            ao, ae, ah = agua(off), agua(enh), agua(high)
            check("W2 agua so com a agua ligada", ao == (0, 0, 0) and ae[0] > 0 and ae[1] > 0 and ah[0] > 0 and ah[1] > 0,
                  "original %s, aprimorada %s, alta %s" % (ao, ae, ah))
            check("W2 a imagem da cena do tamanho do alvo (640 x 480 x 4)", ae[2] == 1200 and ah[2] == 1200, "%s KiB" % ah[2])
            check("W2 log da troca", "[render] agua: alta" in high.log and "[render] agua: aprimorada" in enh.log and
                  "[render] agua:" not in off.log)
            ms = [r[k] for k in ("m1", "m2", "m3", "m4")]
            so_no_dominio(off, enh, ms, "W2 aprimorada")
            so_no_dominio(off, high, ms, "W2 alta")
            dif = sum(int((enh.img("v%s.bmp" % v) != high.img("v%s.bmp" % v)).any(axis=2).sum()) for v in VBS
                      if enh.img("v%s.bmp" % v) is not None and high.img("v%s.bmp" % v) is not None)
            check("W2 aprimorada e alta diferem", dif > 0, "%d pixels" % dif)
            for v in VBS:
                check("W2 %s: o produto igual a validacao" % v, high.img("v%s.bmp" % v) is not None and
                      np.array_equal(high.img("v%s.bmp" % v), vhigh.img("v%s.bmp" % v)))
        if "W3" in casos:
            woff, whigh = r["woff"], r["whigh"]
            igual_estado(woff, whigh, "W3 16:9 1080 original x alta")
            aw = agua(whigh)
            esc = 1080 / 480  # o alvo largo em 1080 linhas (scaled_size: arredondado)
            kib = int(854 * esc + 0.5) * int(480 * esc + 0.5) * 4 // 1024
            check("W3 agua no quadro largo", agua(woff) == (0, 0, 0) and aw[0] > 0 and aw[1] > 0, "%s" % (aw,))
            check("W3 a imagem da cena do tamanho do alvo largo", aw[2] == kib, "%s KiB, alvo %s KiB" % (aw[2], kib))
            so_no_dominio(woff, whigh, [r[k] for k in ("wm1", "wm2", "wm3", "wm4")], "W3 alta (imagem larga)", "_hd")
        if "W4" in casos:
            fxr, fxw = r["fx"], r["fxw"]
            igual_estado(fxr, fxw, "W4 AO + luz + sombras x com a agua")
            check("W4 os quatro rodaram", agua(fxw)[0] > 0 and re.search(r"AO em [1-9]\d* renders", fxw.log) is not None,
                  "agua %s" % (agua(fxw),))
            so_no_dominio(fxr, fxw, [r[k] for k in ("wm1", "wm2", "wm3", "wm4")], "W4 com o N9.6 (imagem larga)", "_hd")
        if "W5" in casos:
            t, off, vhigh = r["troca"], r["off"], r["vhigh"]
            igual_estado(off, t, "W5 transicoes")
            check("W5 as trocas no log", "[validacao] agua no VBlank 32000: 0" in t.log and "[validacao] agua no VBlank 34000: 2" in t.log)
            for v, ref, nome in ((VBS[0], vhigh, "alta"), (VBS[1], off, "original"), (VBS[2], vhigh, "alta")):
                check("W5 %s: o quadro da %s" % (v, nome), t.img("v%s.bmp" % v) is not None and
                      np.array_equal(t.img("v%s.bmp" % v), ref.img("v%s.bmp" % v)))
        if "W6" in casos:
            f, off, vhigh = r["falha"], r["off"], r["vhigh"]
            igual_estado(off, f, "W6 recuperacao")
            check("W6 o dispositivo recriado", "falha do Vulkan injetada no VBlank 32000" in f.log and "dispositivo recriado 1 vez" in f.log)
            for v in VBS[1:]:
                check("W6 %s: o quadro da alta sem a falha" % v, f.img("v%s.bmp" % v) is not None and
                      np.array_equal(f.img("v%s.bmp" % v), vhigh.img("v%s.bmp" % v)))
    finally:
        print("%d falha(s) (%s)" % (len(falhas), base))
        if not a.manter and not falhas:
            shutil.rmtree(base, ignore_errors=True)
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
