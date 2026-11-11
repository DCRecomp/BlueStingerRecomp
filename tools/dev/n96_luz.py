"""N9.6 fase 1 (docs/N9-6-AUDITORIA.md): a oclusao ambiente da Remastered sem janela (Vulkan offscreen).

Mesmo arranjo do n95_headless (pasta temporaria por execucao, copia do exe e do instalacao.toml da edicao, copia do cartao
de teste, roteiro passo_load_slot1: LOAD do slot 1 e o corredor RCG02; capturas nos VBlanks 3800 e 4640). Casos:
  L1 recusas: a Original com --ssao ligado e a Remastered no software com --ssao ligado saem com 2;
  L2 Remastered 4:3 na escala 1, AO desligado x ligado x diagnosticos (so o exe de validacao: BS_AO_DIAGNOSTICO=1 a cor da
     cena vira o AO, para as imagens de prova; =2 preta e =3 branca, as mascaras): estado por passo e efeitos sonoros
     identicos; "AO em N renders" com N > 0 so no ligado; nas capturas, o AO so escurece (nenhum canal maior que o
     desligado), muda uma parte da cena, e todo pixel cuja cor nao depende da cena 3D por baixo (igual com a cena preta e
     com a branca: o HUD opaco, as telas 2D, o fundo) fica bit a bit. Um HUD translucido sobre a cena mostra o AO por
     baixo, como deve (uma mascara so, cinza ou magenta, coincidia com a cor original pela quantizacao do RGB555);
     N9.6 fase 2b: as translucidas do catalogo de materiais (o AO do opaco por baixo) seguem as mascaras (preta: o fator
     0; branca: 1), entao os pixels que elas podem mudar ficam do lado "depende da cena"; o catalogo envia texturas na
     Remastered e o AO so chega a elas com o AO ligado;
  L3 Remastered 16:9 desligado x ligado: estado e efeitos identicos; a imagem larga (slot) so escurece e muda;
  L4 Remastered 16:9 em 1080 linhas desligado x ligado: idem na imagem larga em alta resolucao.

  python tools/dev/n96_luz.py --exe <produto> --exe-validacao <validacao> --data <data> --cartao <pasta de saves sintetica>
                              [--casos L1,L2,L3,L4] [--qualidade baixa|media|alta] [--manter]
--qualidade (padrao media) vai em --ssao-qualidade de todo caso ligado; o log diz quantos renders foram em meia
resolucao (todos na media e na baixa, nenhum na alta).
Saida 1 se algum criterio falhar.
"""
import argparse
import os
import shutil
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n95_headless import Corrida  # noqa: E402

VBS = ("003800", "004640")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--exe-validacao", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--cartao", required=True)
    ap.add_argument("--casos", default="L1,L2,L3,L4")
    ap.add_argument("--manter", action="store_true")
    ap.add_argument("--qualidade", default="media", choices=("baixa", "media", "alta"))
    a = ap.parse_args()
    a.exe, a.exe_validacao = os.path.abspath(a.exe), os.path.abspath(a.exe_validacao)
    casos = set(a.casos.split(","))
    base = tempfile.mkdtemp(prefix="n96_luz_")
    falhas = []

    def check(nome, ok, det=""):
        print("[%s] %s %s" % ("ok" if ok else "FALHA", nome, det), flush=True)
        if not ok:
            falhas.append(nome + " " + det)

    try:
        r = {}
        nat = ["--render", "nativo"]
        lig = ["--ssao", "ligado", "--ssao-qualidade", a.qualidade]  # N9.6 fase 1: a qualidade em teste
        if "L1" in casos:
            r["l1o"] = Corrida(base, "l1o", a, "original", nat + ["--ssao", "ligado"], False)
            r["l1s"] = Corrida(base, "l1s", a, "remastered", ["--render", "software", "--ssao", "ligado"], False)
        if "L2" in casos:
            r["off"] = Corrida(base, "off", a, "remastered", nat, exe=a.exe_validacao)
            r["on"] = Corrida(base, "on", a, "remastered", nat + lig, exe=a.exe_validacao)
            r["diag"] = Corrida(base, "diag", a, "remastered", nat + lig, exe=a.exe_validacao)
            r["diag"].env["BS_AO_DIAGNOSTICO"] = "1"
            for nome, val in (("preta", "2"), ("branca", "3")):
                r[nome] = Corrida(base, nome, a, "remastered", nat + lig, exe=a.exe_validacao)
                r[nome].env["BS_AO_DIAGNOSTICO"] = val
            r["prod"] = Corrida(base, "prod", a, "remastered", nat + lig)  # o produto liga igual
        if "L3" in casos:
            r["w_off"] = Corrida(base, "w_off", a, "remastered", nat + ["--proporcao", "16:9"])
            r["w_on"] = Corrida(base, "w_on", a, "remastered", nat + ["--proporcao", "16:9"] + lig)
        if "L4" in casos:
            hi = ["--proporcao", "16:9", "--resolucao-interna", "1080"]
            r["h_off"] = Corrida(base, "h_off", a, "remastered", nat + hi)
            r["h_on"] = Corrida(base, "h_on", a, "remastered", nat + hi + lig)
        with ThreadPoolExecutor(3) as ex:
            list(ex.map(lambda c: c.rodar(), r.values()))

        def ao_renders(c):
            import re
            m = re.search(r"AO em (\d+) renders", c.log)
            return int(m.group(1)) if m else -1

        def meia(c):  # os renders em meia resolucao (o resumo do Vulkan)
            import re
            m = re.search(r"AO em \d+ renders \(meia resolucao (\d+)\)", c.log)
            return int(m.group(1)) if m else -1

        def catalogo(c):  # N9.6 fase 2b: (envios de texturas do catalogo, renders com o AO nas translucidas dele)
            import re
            m = re.search(r"catalogo de materiais: (\d+) envios, AO nas translucidas em (\d+) renders", c.log)
            return (int(m.group(1)), int(m.group(2))) if m else (-1, -1)

        def qualidade_certa(c, nome):
            n, h = ao_renders(c), meia(c)
            ok = n > 0 and (h == 0 if a.qualidade == "alta" else h == n) and ("qualidade %s" % a.qualidade) in c.log
            check("%s qualidade %s" % (nome, a.qualidade), ok, "AO em %d renders, %d em meia resolucao" % (n, h))

        def igual_estado(x, y, nome):
            check(nome + ": estado por passo identico", x.rc == 0 and y.rc == 0 and x.passos() == y.passos() and len(x.passos()) > 100,
                  "rc %d/%d, %d/%d passos" % (x.rc, y.rc, len(x.passos()) if x.rc == 0 else 0, len(y.passos()) if y.rc == 0 else 0))
            check(nome + ": efeitos sonoros identicos", x.rc == 0 and y.rc == 0 and np.array_equal(x.wav(), y.wav()))

        def escurece(off, on, nome, sufixo):
            for v in VBS:
                g0, g1 = off.img("v%s%s.bmp" % (v, sufixo)), on.img("v%s%s.bmp" % (v, sufixo))
                if g0 is None or g1 is None or g0.shape != g1.shape:
                    check("%s %s: capturas" % (nome, v), False, "ausente ou de outro tamanho")
                    continue
                mais = int((g1 > g0).any(axis=2).sum())
                mudou = float((g1 != g0).any(axis=2).mean())
                queda = float(1 - g1.mean() / max(1e-9, g0.mean()))
                check("%s %s: o AO so escurece" % (nome, v), mais == 0, "%d pixels mais claros" % mais)
                check("%s %s: o AO muda a cena" % (nome, v), 0.02 < mudou < 0.95, "%.1f %% dos pixels, %.1f %% mais escuro" % (100 * mudou, 100 * queda))

        if "L1" in casos:
            check("L1 Original com --ssao ligado recusada", r["l1o"].rc == 2, "rc %d" % r["l1o"].rc)
            check("L1 Remastered no software com --ssao ligado recusada", r["l1s"].rc == 2, "rc %d" % r["l1s"].rc)
        if "L2" in casos:
            off, on, diag, prod = r["off"], r["on"], r["diag"], r["prod"]
            igual_estado(off, on, "L2 4:3 desligado x ligado")
            igual_estado(off, diag, "L2 4:3 desligado x diagnostico")
            igual_estado(off, prod, "L2 4:3 validacao desligado x produto ligado")
            check("L2 AO aplicado so no ligado", ao_renders(off) == 0 and ao_renders(on) > 1000 and ao_renders(prod) == ao_renders(on),
                  "renders: desligado %d, ligado %d, produto %d" % (ao_renders(off), ao_renders(on), ao_renders(prod)))
            check("L2 log da troca", "[render] oclusao ambiente: ligada" in on.log and "oclusao ambiente" not in off.log)
            qualidade_certa(on, "L2")
            # N9.6 fase 2b: as texturas do catalogo (o piso e os racks do quarto) chegam ao Vulkan nas duas execucoes da
            # Remastered; o AO so chega a elas com o AO ligado
            cat_off, cat_on = catalogo(off), catalogo(on)
            check("L2 catalogo de materiais: texturas enviadas na Remastered", cat_off[0] > 0 and cat_on[0] > 0,
                  "desligado %d, ligado %d envios" % (cat_off[0], cat_on[0]))
            check("L2 catalogo: o AO nas translucidas so com o AO ligado", cat_off[1] == 0 and cat_on[1] > 0,
                  "desligado %d, ligado %d renders" % (cat_off[1], cat_on[1]))
            qualidade_certa(prod, "L2 produto")
            escurece(off, on, "L2", "")
            igual_estado(off, r["preta"], "L2 4:3 desligado x mascara preta")
            igual_estado(off, r["branca"], "L2 4:3 desligado x mascara branca")
            for v in VBS:
                g0, g1, gd, gp, gb, gw = (c.img("v%s.bmp" % v) for c in (off, on, diag, prod, r["preta"], r["branca"]))
                if any(g is None for g in (g0, g1, gd, gp, gb, gw)):
                    check("L2 %s: capturas do diagnostico" % v, False, "ausente")
                    continue
                # a cor nao depende da cena 3D por baixo: igual com a cena preta e com a branca (a mistura e a quantizacao
                # sao monotonas na cor de baixo, entao qualquer cor no meio, a do AO inclusive, da o mesmo resultado)
                fora = ~(gb != gw).any(axis=2)
                igual = bool((g1[fora] == g0[fora]).all())
                check("L2 %s: fora da cena 3D bit a bit" % v, igual and 0.0 < fora.mean() < 1.0, "%.1f %% fora da cena" % (100 * fora.mean()))
                check("L2 %s: o produto igual a validacao" % v, np.array_equal(g1, gp))
                os.makedirs(os.path.join(base, "provas"), exist_ok=True)
                for nome, g in (("desligado", g0), ("ligado", g1), ("ao", gd)):
                    from PIL import Image
                    Image.fromarray(g.astype(np.uint8)).save(os.path.join(base, "provas", "%s_%s.png" % (v, nome)))
        if "L3" in casos:
            igual_estado(r["w_off"], r["w_on"], "L3 16:9 desligado x ligado")
            escurece(r["w_off"], r["w_on"], "L3", "_hd")
            qualidade_certa(r["w_on"], "L3")
        if "L4" in casos:
            igual_estado(r["h_off"], r["h_on"], "L4 16:9 1080 desligado x ligado")
            escurece(r["h_off"], r["h_on"], "L4", "_hd")
            qualidade_certa(r["h_on"], "L4")
    finally:
        print("%d falha(s) (%s)" % (len(falhas), base))
        if not a.manter and not falhas:
            shutil.rmtree(base, ignore_errors=True)
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
