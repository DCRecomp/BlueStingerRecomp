"""N9.8 fase 8.3 (docs/N9-8-AUDITORIA.md secao 10): o antialiasing da Remastered.

Sem janela (Vulkan offscreen), na rota passo_cgate ate o cais RCG00, resolucao interna de 1440 linhas:
  A1 recusas: a Original com --antialiasing area e com bordas, e a Remastered no software com bordas, saem com 2;
  A2 bordas (produto) x sem: estado por passo e efeitos sonoros identicos; o antialiasing em renders > 0; a imagem em 1440
     muda pouco (so as bordas: menos de 8 % dos pixels) nos quadros da cena; nas telas da CPU (4000, 12000) igual bit a bit;
  A3 a validacao com bordas igual bit a bit ao produto com bordas (imagem do jogo e em 1440); com BS_AA_DIAGNOSTICO=1
     (so validacao) os pixels misturados aparecem em magenta e o estado nao muda; o produto ignora a variavel;
  A4 bordas com o 16:9, a anisotropica, o AO, a luz, as sombras aprimoradas e a agua alta: estado e som identicos aos mesmos
     sem as bordas; os seis efeitos rodam.

Na janela (--cartao; abre a janela do Blue Stinger, ~1 min por execucao; BS_JANELA_CAPTURA grava a imagem APRESENTADA,
nao a area de trabalho), rota passo_load_slot1 no modo espectador, Remastered 4:3:
  A5 a reducao por area: janela 640x480 com a interna de 1440 (3:1): "area" difere do "original" e tem menos alta
     frequencia; janela 1280x960 com a interna de 1440 (1,5:1): "area" igual bit a bit ao "original".

  python tools/dev/n98_aa.py --exe <produto> --exe-validacao <validacao> --data <data> [--cartao <saves>] [--casos A1,...]
Saida 1 se algum criterio falhar.
"""
import argparse
import copy
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n95_headless import Corrida  # noqa: E402

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROTEIRO = os.path.join(RAIZ, "tools", "dist", "roteiros", "passo_cgate.txt")
ROTEIRO_J = os.path.join(RAIZ, "tools", "dist", "roteiros", "passo_load_slot1.txt")
VBLANKS = 34300
VBS = ("004000", "012000", "030700", "034200")
VBS_CENA = ("030700", "034200")  # a imagem na resolucao interna so existe nos renders da cena (4000 e 12000: telas da CPU)
CAPTURAS = ",".join("%d-%d" % (int(v), int(v)) for v in VBS)
J_INI, J_FIM, J_CADA = 4300, 4640, 85  # o corredor RCG02 (o mesmo do n95_janela)

GRAFICOS = """preset = "personalizado"
[imagem]
resolucao = "1440p"
antialiasing = "{aa}"
[tela]
modo = "janela"
largura = {w}
altura = {h}
proporcao = "4:3"
renderizador = "vulkan"
"""


def lap(img):  # alta frequencia: a media do modulo do laplaciano do verde
    g = img[:, :, 1].astype(np.float64)
    return float(np.abs(4 * g[1:-1, 1:-1] - g[:-2, 1:-1] - g[2:, 1:-1] - g[1:-1, :-2] - g[1:-1, 2:]).mean())


def janela(a, nome, aa, w, h):
    tmp = os.path.join(a.base, "j_" + nome)
    os.makedirs(tmp)
    exe = os.path.join(tmp, os.path.basename(a.exe))
    shutil.copy2(a.exe, exe)
    open(os.path.join(tmp, "instalacao.toml"), "w").write('[edicao]\nperfil = "remastered"\n')
    open(os.path.join(tmp, "graficos.toml"), "w").write(GRAFICOS.format(aa=aa, w=w, h=h))
    shutil.copytree(a.cartao, os.path.join(tmp, "saves"))
    cap = os.path.join(tmp, "cap")
    env = dict(os.environ)
    for k in ("BS_INPUT_SCRIPT", "BS_GRAVAR", "BS_ASSISTIDO", "BS_RENDER", "BS_APRESENTACAO", "BS_CONTROLE_VIRTUAL"):
        env.pop(k, None)
    env.update({"BS_RELOGIO_FIXO": "1700000000", "BS_INPUT_SCRIPT": ROTEIRO_J, "BS_JANELA_CAPTURA": cap,
                "BS_JANELA_CAPTURA_CADA": str(J_CADA), "BS_JANELA_CAPTURA_VBLANKS": "%d-%d" % (J_INI, J_FIM)})
    cmd = [exe, "--data", os.path.abspath(a.data), "--saves-pasta", os.path.join(tmp, "saves"), "--apresentacao", "vulkan"]
    log = os.path.join(tmp, "jogo.txt")
    with open(log, "w", encoding="utf-8") as out:
        p = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env=env, cwd=tmp)
        limite = time.time() + J_FIM / 40 + 90
        while p.poll() is None and time.time() < limite:
            caps = os.listdir(cap) if os.path.isdir(cap) else []
            if any(int(re.match(r"janela_v(\d+)_", x).group(1)) >= J_FIM - J_CADA for x in caps):
                break
            time.sleep(2)
        if p.poll() is None:  # so o processo deste caso
            p.terminate()
            try:
                p.wait(20)
            except subprocess.TimeoutExpired:
                p.kill()
    imgs = {}
    for x in sorted(os.listdir(cap)) if os.path.isdir(cap) else []:
        m = re.match(r"janela_v(\d+)_", x)
        imgs[int(m.group(1))] = np.asarray(Image.open(os.path.join(cap, x)).convert("RGB"))
    return imgs, open(log, encoding="utf-8", errors="replace").read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--exe-validacao", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--cartao", help="pasta de saves com o slot 1 (so o A5, na janela)")
    ap.add_argument("--casos", default="A1,A2,A3,A4,A5,A6")
    ap.add_argument("--manter", action="store_true")
    a = ap.parse_args()
    a.exe, a.exe_validacao = os.path.abspath(a.exe), os.path.abspath(a.exe_validacao)
    casos = set(a.casos.split(","))
    a.base = base = tempfile.mkdtemp(prefix="n98_aa_")
    a.cartao_vazio = os.path.join(base, "_cartao_vazio")
    os.makedirs(a.cartao_vazio)
    falhas = []

    def check(nome, ok, det=""):
        print("[%s] %s %s" % ("ok" if ok else "FALHA", nome, det), flush=True)
        if not ok:
            falhas.append(nome + " " + det)

    def corrida(nome, edicao, extra, capturar=True, exe=None, env=None):
        b = copy.copy(a)
        b.cartao = a.cartao_vazio  # a rota passo_cgate comeca com o cartao vazio (NEW GAME)
        c = Corrida(base, nome, b, edicao, extra, capturar, exe=exe, capturas=CAPTURAS)
        c.env["BS_INPUT_SCRIPT"] = ROTEIRO
        c.cmd[c.cmd.index("--headless") + 1] = str(VBLANKS)
        c.env.update(env or {})
        return c

    def aa_renders(c):
        m = re.search(r"antialiasing das bordas em (\d+) renders", c.log)
        return int(m[1]) if m else -1

    def igual_estado(x, y, nome):
        check(nome + ": estado por passo identico", x.rc == 0 and y.rc == 0 and x.passos() == y.passos() and len(x.passos()) > 1000,
              "rc %d/%d" % (x.rc, y.rc))
        check(nome + ": efeitos sonoros identicos", x.rc == 0 and y.rc == 0 and np.array_equal(x.wav(), y.wav()))

    def imgs(c, sufixo, vbs):
        return [c.img("v%s%s.bmp" % (v, sufixo)) for v in vbs]

    try:
        nat = ["--render", "nativo", "--resolucao-interna", "1440"]
        bordas = ["--antialiasing", "bordas"]
        efeitos = ["--render", "nativo", "--proporcao", "16:9", "--resolucao-interna", "1080", "--filtragem", "anisotropica",
                   "--ssao", "ligado", "--iluminacao", "ligado", "--sombras", "aprimoradas", "--agua", "alta"]
        r = {"off": corrida("off", "remastered", nat)}
        if "A1" in casos:
            r["a1a"] = corrida("a1a", "original", ["--render", "nativo", "--antialiasing", "area"], False)
            r["a1b"] = corrida("a1b", "original", ["--render", "nativo"] + bordas, False)
            r["a1s"] = corrida("a1s", "remastered", ["--render", "software"] + bordas, False)
        if "A2" in casos or "A3" in casos:
            # o produto com bordas recebe tambem a variavel do diagnostico: tem de ignora-la (A3)
            r["on"] = corrida("on", "remastered", nat + bordas, env={"BS_AA_DIAGNOSTICO": "1"})
        if "A3" in casos:
            r["val"] = corrida("val", "remastered", nat + bordas, exe=a.exe_validacao)
            r["diag"] = corrida("diag", "remastered", nat + bordas, exe=a.exe_validacao, env={"BS_AA_DIAGNOSTICO": "1"})
        if "A4" in casos:
            r["a4ref"] = corrida("a4ref", "remastered", efeitos)
            r["a4"] = corrida("a4", "remastered", efeitos + bordas)
        with ThreadPoolExecutor(3) as ex:
            list(ex.map(lambda c: c.rodar(), r.values()))
        off = r["off"]
        check("sem a opcao: nenhum render com as bordas", off.rc == 0 and aa_renders(off) == 0, "%d" % aa_renders(off))
        if "A1" in casos:
            for k, rot in (("a1a", "Original com area"), ("a1b", "Original com bordas"), ("a1s", "Remastered no software com bordas")):
                check("A1 %s recusada" % rot, r[k].rc == 2, "rc %d" % r[k].rc)
        if "A2" in casos:
            on = r["on"]
            igual_estado(off, on, "A2 bordas x sem")
            check("A2 as bordas em renders > 0", aa_renders(on) > 0 and "[render] antialiasing das bordas: ligado" in on.log,
                  "%d" % aa_renders(on))
            x, y = imgs(off, "_hd", VBS_CENA), imgs(on, "_hd", VBS_CENA)
            ok = all(i is not None for i in x + y)
            fr = [float((p != q).any(axis=2).mean()) for p, q in zip(x, y)] if ok else []
            print("  A2: pixels mudados em 1440 %s; alta frequencia sem %s, com %s" %
                  (["%.2f%%" % (100 * f) for f in fr], ["%.2f" % lap(p) for p in x] if ok else [], ["%.2f" % lap(q) for q in y] if ok else []))
            check("A2 a imagem em 1440 muda so nas bordas (0 < pixels < 8 %)", ok and all(0 < f < 0.08 for f in fr))
            check("A2 a alta frequencia nao sobe", ok and all(lap(q) <= lap(p) for p, q in zip(x, y)))
            x, y = imgs(off, "", ("004000", "012000")), imgs(on, "", ("004000", "012000"))
            check("A2 as telas da CPU iguais bit a bit", all(i is not None for i in x + y) and all(np.array_equal(p, q) for p, q in zip(x, y)))
        if "A3" in casos:
            on, val, diag = r["on"], r["val"], r["diag"]
            igual_estado(on, val, "A3 validacao x produto")
            for suf, vbs, rot in (("", VBS, "imagem do jogo"), ("_hd", VBS_CENA, "imagem em 1440")):
                p, q = imgs(on, suf, vbs), imgs(val, suf, vbs)
                check("A3 validacao x produto: %s igual bit a bit" % rot,
                      all(i is not None for i in p + q) and all(np.array_equal(i, j) for i, j in zip(p, q)))
            igual_estado(val, diag, "A3 diagnostico x validacao")
            d = imgs(diag, "_hd", VBS_CENA)
            mag = [int(((i[:, :, 0] == 255) & (i[:, :, 1] == 0) & (i[:, :, 2] == 255)).sum()) for i in d if i is not None]
            check("A3 o diagnostico pinta os pixels misturados", len(mag) == len(VBS_CENA) and all(m > 0 for m in mag), str(mag))
            o = imgs(on, "_hd", VBS_CENA)
            magp = [int(((i[:, :, 0] == 255) & (i[:, :, 1] == 0) & (i[:, :, 2] == 255)).sum()) for i in o if i is not None]
            check("A3 o produto ignora BS_AA_DIAGNOSTICO", len(magp) == len(VBS_CENA) and all(m < mm for m, mm in zip(magp, mag)),
                  "%s x %s" % (magp, mag))
        if "A4" in casos:
            ref, c = r["a4ref"], r["a4"]
            igual_estado(ref, c, "A4 bordas com os efeitos (16:9 em 1080)")
            m = lambda s, k: int(re.search(s, k.log)[1]) if re.search(s, k.log) else -1  # noqa: E731
            ef = {"AO": m(r"AO em (\d+) renders", c), "luz": m(r"luz das fontes em (\d+) renders", c),
                  "borda suave": m(r"borda suave dos volumes em (\d+) renders", c), "agua": m(r"agua em (\d+) renders", c),
                  "filtragem": m(r"filtragem: (\d+) desenhos", c), "bordas": aa_renders(c)}
            luz_ref = m(r"luz das fontes em (\d+) renders", ref)
            print("  A4: %s (luz sem as bordas: %d)" % (ef, luz_ref))
            # a rota passo_cgate nao tem fontes do catalogo (a luz liga e fica em 0 renders, como no F3 do n98_filtragem; as
            # luzes sao provadas pelo n96_luz noutra rota): a luz ligada e a mesma contagem com e sem as bordas
            check("A4 os efeitos rodam", all(v > 0 for k, v in ef.items() if k != "luz") and
                  "[render] luz das fontes: ligada" in c.log and ef["luz"] == luz_ref)
        if "A5" in casos:
            if not a.cartao:
                check("A5 precisa de --cartao", False)
            else:
                res = {}
                for nome, aa, w, h in (("o3", "original", 640, 480), ("a3", "area", 640, 480),
                                       ("o15", "original", 1280, 960), ("a15", "area", 1280, 960)):
                    res[nome] = janela(a, nome, aa, w, h)
                    print("  A5 %s: %d capturas" % (nome, len(res[nome][0])))
                comuns = sorted(set(res["o3"][0]) & set(res["a3"][0]))
                check("A5 capturas em 3:1", len(comuns) >= 2, "%d" % len(comuns))
                if comuns:
                    lo = [lap(res["o3"][0][v]) for v in comuns]
                    la = [lap(res["a3"][0][v]) for v in comuns]
                    print("  A5 3:1: alta frequencia original %s, area %s" % (["%.2f" % v for v in lo], ["%.2f" % v for v in la]))
                    check("A5 3:1: a area difere do original", all((res["o3"][0][v] != res["a3"][0][v]).any() for v in comuns))
                    check("A5 3:1: a area tem menos alta frequencia", all(q < p for p, q in zip(lo, la)))
                comuns = sorted(set(res["o15"][0]) & set(res["a15"][0]))
                check("A5 capturas em 1,5:1", len(comuns) >= 2, "%d" % len(comuns))
                if comuns:
                    check("A5 1,5:1: a area igual bit a bit ao original",
                          all(np.array_equal(res["o15"][0][v], res["a15"][0][v]) for v in comuns))
        if "A6" in casos:
            # revisao Codex 20261113-135423: o --antialiasing vale so nesta execucao ATE o jogador mudar a linha no menu; dai a
            # escolha do menu vai para o graficos.toml. O arquivo com o Alto ("area"), --antialiasing bordas e, no menu da tela
            # inicial, Antialiasing duas vezes para a esquerda (bordas -> area -> original): o arquivo grava "original"
            from n95_persistencia import ATE_GRAFICOS, REM_INICIO, preparar, rodar  # noqa: E402
            a.saida = os.path.join(base, "a6")
            tela = '[tela]\nmodo = "janela"\nlargura = 1280\naltura = 720\nrenderizador = "vulkan"\n'
            d = preparar(a, "A6", "remastered", 'preset = "alta"\n' + tela)
            menos2 = ATE_GRAFICOS + ["dpdown"] * 8 + ["dpleft", "dpleft", "b", "b"]  # Graficos: ... Filtragem 7, Antialiasing 8
            rc, t, toml = rodar(a, d, "1", menos2, REM_INICIO, args=["--antialiasing", "bordas"])
            liga, desl = t.find("[render] antialiasing das bordas: ligado"), t.find("[render] antialiasing das bordas: desligado")
            check("A6 --antialiasing bordas e depois o menu desliga", rc == 0 and 0 <= liga < desl, "rc %d" % rc)
            m = re.search(r'^antialiasing = "([^"]*)"$', toml or "", re.M)
            check("A6 a escolha do menu vai para o arquivo", m is not None and m.group(1) == "original", m.group(1) if m else "ausente")
            # sem mexer na linha: o --antialiasing nao vai para o arquivo
            d = preparar(a, "A6b", "remastered", 'preset = "alta"\n' + tela)
            rc, t, toml = rodar(a, d, "1", [], REM_INICIO, fim_ms=REM_INICIO, args=["--antialiasing", "bordas"])
            check("A6 sem mexer na linha: o arquivo fica", rc == 0 and toml == 'preset = "alta"\n' + tela, "rc %d" % rc)
    finally:
        print("%d falha(s) (%s)" % (len(falhas), base))
        if not a.manter and not falhas:
            shutil.rmtree(base, ignore_errors=True)
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
