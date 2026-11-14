"""N9.8 fase 8.4 (docs/N9-8-AUDITORIA.md secao 11): o pos-processamento da Remastered com a mascara do HUD.

A nitidez e o bloom so na cena 3D: o dispositivo grava no ALFA do slot da apresentacao a mascara (hudmask.comp: a cena 3D
fora dos desenhos translucidos 2D) e os passes internos a usam. So validacao: BS_HUD_MASCARA=1 liga a mascara tambem sem a
janela e grava o alfa em vNNNNNN_mascara.bmp; BS_HUD_MASCARA=0 a desliga na janela (os efeitos como antes, para comparar).

Sem janela (Vulkan offscreen), rota passo_cgate ate o cais RCG00, resolucao interna de 1440 linhas:
  P1 a validacao com BS_HUD_MASCARA=1 x o produto: estado por passo e efeitos sonoros identicos; a imagem do jogo e o RGB da
     imagem em 1440 iguais bit a bit (a mascara so muda o alfa do slot); a mascara em slots > 0; no 030700 (a explosao e a
     fumaca: sprites da cena) tudo na cena; no 034200 o HUD do canto fora e o resto dentro; o produto com a variavel nao a
     liga (0 slots, sem vNNNNNN_mascara.bmp);
  P2 a mascara com o 16:9 em 1080, a anisotropica, o AO, a luz, as sombras aprimoradas, a agua alta e as bordas: estado e
     som identicos aos mesmos sem ela; os efeitos rodam; a mascara em slots > 0;
  P3 recuperacao (validacao, BS_VK_FALHAR=32000, transitoria, com BS_HUD_MASCARA=1): o dispositivo recriado; no 34200 a
     imagem em 1440 e a mascara iguais as da execucao sem a falha.

Na janela (--cartao; abre a janela do Blue Stinger, ~1 min por execucao; BS_JANELA_CAPTURA grava a imagem APRESENTADA,
nao a area de trabalho), rota passo_load_slot1 no modo espectador, o exe de validacao, interna de 720 numa janela de
960x720 (1:1: o pixel apresentado e o do slot):
  P4 sem efeitos: a mascara em 0 slots (nenhum processamento a mais); com so a correcao de cor: 0 slots, a imagem muda;
  P5 nitidez 1,0: a mascara em slots > 0; um pixel mudado so com a mascara so na rampa dela (a ate 16 px de um protegido,
     no maximo 0,01 %: a apresentacao mistura duas linhas, 714 -> 720); os pixels do HUD do canto intactos com a mascara e
     realcados sem ela (BS_HUD_MASCARA=0); nos VBlanks em que a janela e o jogo sem janela mostram o mesmo render, todo
     protegido dentro da mascara gravada sem janela (vNNNNNN_mascara.bmp);
  P6 bloom: com a mascara em slots > 0; o bloom roda; a diferenca para o sem a mascara so e relatada (o brilho do HUD e do
     que a mascara tira nao se espalha);
  P7 tudo junto (cor, tonemapping, nitidez, bloom, area): roda sem erro do Vulkan, a mascara em slots > 0;
  P8 a Original com a nitidez 1,0: a mascara em 0 slots (os efeitos como antes) e a imagem igual a da Original com
     BS_HUD_MASCARA=0;
  P9 recuperacao na janela (BS_VK_FALHAR=4400): o dispositivo e a apresentacao recriados, a mascara continua (slots > 0) e
     as capturas depois da falha iguais as da nitidez sem a falha.

  python tools/dev/n98_pos.py --exe <produto> --exe-validacao <validacao> --data <data> [--cartao <saves>] [--casos P1,...]
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
VBS_CENA = ("030700", "034200")
CAPTURAS = ",".join("%d-%d" % (int(v), int(v)) for v in VBS)
J_INI, J_FIM, J_CADA = 4300, 4640, 85  # o corredor RCG02 (o mesmo do n95_janela)
J_FALHA = 4400

GRAFICOS = """preset = "personalizado"
[imagem]
resolucao = "720p"
antialiasing = "{aa}"
[efeitos]
cor = {cor}
contraste = 1.2
saturacao = 1.3
tonemap = {tonemap}
nitidez = {nitidez}
bloom = {bloom}
bloom_limiar = 0.6
bloom_intensidade = 1.0
[tela]
modo = "janela"
largura = 960
altura = 720
proporcao = "4:3"
renderizador = "vulkan"
"""


def slots(log):
    m = re.search(r"mascara do HUD em (\d+) slots", log)
    return int(m[1]) if m else -1


def janela(a, nome, edicao="remastered", env=None, aa="original", cor="false", tonemap="false", nitidez="0", bloom="false"):
    tmp = os.path.join(a.base, "j_" + nome)
    os.makedirs(tmp)
    exe = os.path.join(tmp, os.path.basename(a.exe_validacao))
    shutil.copy2(a.exe_validacao, exe)
    open(os.path.join(tmp, "instalacao.toml"), "w").write('[edicao]\nperfil = "%s"\n' % edicao)
    open(os.path.join(tmp, "graficos.toml"), "w").write(GRAFICOS.format(aa=aa, cor=cor, tonemap=tonemap, nitidez=nitidez, bloom=bloom))
    shutil.copytree(a.cartao, os.path.join(tmp, "saves"))
    cap = os.path.join(tmp, "cap")
    e = dict(os.environ)
    for k in ("BS_INPUT_SCRIPT", "BS_GRAVAR", "BS_ASSISTIDO", "BS_RENDER", "BS_APRESENTACAO", "BS_CONTROLE_VIRTUAL",
              "BS_HUD_MASCARA", "BS_VK_FALHAR"):
        e.pop(k, None)
    e.update({"BS_RELOGIO_FIXO": "1700000000", "BS_INPUT_SCRIPT": ROTEIRO_J, "BS_JANELA_CAPTURA": cap,
              "BS_JANELA_CAPTURA_CADA": str(J_CADA), "BS_JANELA_CAPTURA_VBLANKS": "%d-%d" % (J_INI, J_FIM),
              "BS_JANELA_SAIR_VBLANK": str(J_FIM + 30)})  # so validacao: fecha pelo caminho normal (o resumo do render)
    e.update(env or {})
    cmd = [exe, "--data", os.path.abspath(a.data), "--saves-pasta", os.path.join(tmp, "saves"), "--apresentacao", "vulkan"]
    log = os.path.join(tmp, "jogo.txt")
    with open(log, "w", encoding="utf-8") as out:
        p = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env=e, cwd=tmp)
        try:  # BS_JANELA_SAIR_VBLANK fecha a janela; o limite so para um travamento (e o caso falha: sem o resumo)
            p.wait(J_FIM / 40 + 150)
        except subprocess.TimeoutExpired:  # so o processo deste caso
            p.terminate()
            try:
                p.wait(30)
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
    ap.add_argument("--cartao", help="pasta de saves com o slot 1 (so os casos da janela)")
    ap.add_argument("--casos", default="P1,P2,P3,P4,P5,P6,P7,P8,P9")
    ap.add_argument("--manter", action="store_true")
    a = ap.parse_args()
    a.exe, a.exe_validacao = os.path.abspath(a.exe), os.path.abspath(a.exe_validacao)
    casos = set(a.casos.split(","))
    a.base = base = tempfile.mkdtemp(prefix="n98_pos_")
    a.cartao_vazio = os.path.join(base, "_cartao_vazio")
    os.makedirs(a.cartao_vazio)
    falhas = []

    def check(nome, ok, det=""):
        print("[%s] %s %s" % ("ok" if ok else "FALHA", nome, det), flush=True)
        if not ok:
            falhas.append(nome + " " + det)

    def corrida(nome, extra, capturar=True, exe=None, env=None):
        b = copy.copy(a)
        b.cartao = a.cartao_vazio  # a rota passo_cgate comeca com o cartao vazio (NEW GAME)
        c = Corrida(base, nome, b, "remastered", extra, capturar, exe=exe, capturas=CAPTURAS)
        c.env["BS_INPUT_SCRIPT"] = ROTEIRO
        c.env.pop("BS_HUD_MASCARA", None)
        c.cmd[c.cmd.index("--headless") + 1] = str(VBLANKS)
        c.env.update(env or {})
        return c

    def igual_estado(x, y, nome):
        check(nome + ": estado por passo identico", x.rc == 0 and y.rc == 0 and x.passos() == y.passos() and len(x.passos()) > 1000,
              "rc %d/%d" % (x.rc, y.rc))
        check(nome + ": efeitos sonoros identicos", x.rc == 0 and y.rc == 0 and np.array_equal(x.wav(), y.wav()))

    def imgs(c, sufixo, vbs):
        return [c.img("v%s%s.bmp" % (v, sufixo)) for v in vbs]

    def iguais(p, q):
        return all(i is not None for i in p + q) and len(p) == len(q) and all(np.array_equal(i, j) for i, j in zip(p, q))

    try:
        val = a.exe_validacao
        nat = ["--render", "nativo", "--resolucao-interna", "1440"]
        efeitos = ["--render", "nativo", "--proporcao", "16:9", "--resolucao-interna", "1080", "--filtragem", "anisotropica",
                   "--ssao", "ligado", "--iluminacao", "ligado", "--sombras", "aprimoradas", "--agua", "alta",
                   "--antialiasing", "bordas"]
        mask = {"BS_HUD_MASCARA": "1"}
        r = {}
        if casos & {"P1", "P3"}:
            r["prod"] = corrida("prod", nat, env=mask)  # o produto recebe a variavel: tem de ignora-la
            r["val"] = corrida("val", nat, exe=val, env=mask)
        if "P2" in casos:
            r["p2ref"] = corrida("p2ref", efeitos, exe=val)
            r["p2"] = corrida("p2", efeitos, exe=val, env=mask)
        if "P3" in casos:
            r["falha"] = corrida("falha", nat, exe=val, env=dict(mask, BS_VK_FALHAR="32000"))
        with ThreadPoolExecutor(3) as ex:
            list(ex.map(lambda c: c.rodar(), r.values()))
        if casos & {"P1", "P3"}:
            prod, v = r["prod"], r["val"]
        if "P1" in casos:
            igual_estado(prod, v, "P1 mascara (validacao) x produto")
            check("P1 a imagem do jogo igual bit a bit", iguais(imgs(prod, "", VBS), imgs(v, "", VBS)))
            check("P1 o RGB da imagem em 1440 igual bit a bit", iguais(imgs(prod, "_hd", VBS_CENA), imgs(v, "_hd", VBS_CENA)))
            check("P1 a mascara em slots > 0 (validacao)", slots(v.log) > 0 and "BS_HUD_MASCARA=1" in v.log, "%d" % slots(v.log))
            check("P1 o produto ignora BS_HUD_MASCARA", slots(prod.log) == 0 and all(i is None for i in imgs(prod, "_mascara", VBS_CENA)),
                  "%d" % slots(prod.log))
            ms = imgs(v, "_mascara", VBS_CENA)
            ok = all(i is not None for i in ms)
            cena = [float((m[:, :, 0] == 255).mean()) for m in ms] if ok else []
            zero = [float((m[:, :, 0] == 0).mean()) for m in ms] if ok else []
            rampa = [int(((m[:, :, 0] > 0) & (m[:, :, 0] < 255)).sum()) for m in ms] if ok else []
            print("  P1 mascara: cena %s, fora %s, rampa %s pixels" % (["%.1f%%" % (100 * x) for x in cena],
                                                                     ["%.1f%%" % (100 * x) for x in zero], rampa))
            # 030700: o cais com a explosao e a fumaca (sprites da cena, 1/w ~6,6e-05) e sem HUD: tudo cena. 034200: o HUD do canto
            # (o nome e a barra de vida, embaixo a esquerda) fora da cena e o resto dentro (conferido na imagem, 2026-11-13)
            hud = [float((m[int(0.86 * m.shape[0]):int(0.94 * m.shape[0]), int(0.04 * m.shape[1]):int(0.10 * m.shape[1]), 0] == 0).mean())
                   for m in ms] if ok else []
            check("P1 030700: a explosao e a fumaca na cena (>= 99 %)", ok and cena[0] >= 0.99, "%s" % cena[:1])
            check("P1 034200: o HUD do canto fora (0,5 % a 10 % fora; a regiao dele > 90 % em 0)",
                  ok and 0.005 < zero[1] < 0.10 and hud[1] > 0.9 and cena[1] > 0.85, "fora %s, regiao do HUD %s" % (zero[1:], hud[1:]))
            check("P1 a mascara e cinza (R = G = B)", ok and all(np.array_equal(m[:, :, 0], m[:, :, 1]) and np.array_equal(m[:, :, 1], m[:, :, 2]) for m in ms))
        if "P2" in casos:
            ref, c = r["p2ref"], r["p2"]
            igual_estado(ref, c, "P2 mascara com os efeitos (16:9 em 1080)")
            m = lambda s, k: int(re.search(s, k.log)[1]) if re.search(s, k.log) else -1  # noqa: E731
            ef = {"AO": m(r"AO em (\d+) renders", c), "borda suave": m(r"borda suave dos volumes em (\d+) renders", c),
                  "agua": m(r"agua em (\d+) renders", c), "filtragem": m(r"filtragem: (\d+) desenhos", c),
                  "bordas": m(r"antialiasing das bordas em (\d+) renders", c), "mascara": slots(c.log)}
            print("  P2: %s" % ef)
            check("P2 os efeitos e a mascara rodam", all(x > 0 for x in ef.values()))
            check("P2 o RGB da imagem larga igual bit a bit", iguais(imgs(ref, "_hd", VBS_CENA), imgs(c, "_hd", VBS_CENA)))
        if "P3" in casos:
            f = r["falha"]
            igual_estado(v, f, "P3 recuperacao")
            check("P3 o dispositivo recriado", "falha do Vulkan injetada no VBlank 32000" in f.log and "dispositivo recriado 1 vez" in f.log)
            for suf in ("_hd", "_mascara"):
                check("P3 034200%s igual ao sem a falha" % suf, iguais(imgs(v, suf, ("034200",)), imgs(f, suf, ("034200",))))
        if casos & {"P4", "P5", "P6", "P7", "P8", "P9"}:
            if not a.cartao:
                check("P4-P9 precisam de --cartao", False)
                raise SystemExit
            sem = {"BS_HUD_MASCARA": "0"}
            plano = {"P4": [("w0", {}, {}), ("cor", {}, {"cor": "true"})],
                     "P5": [("w0", {}, {}), ("n1", {}, {"nitidez": "1.0"}), ("n1s", sem, {"nitidez": "1.0"})],
                     "P6": [("w0", {}, {}), ("b1", {}, {"bloom": "true"}), ("b1s", sem, {"bloom": "true"})],
                     "P7": [("tudo", {}, {"cor": "true", "tonemap": "true", "nitidez": "1.0", "bloom": "true", "aa": "area"})],
                     "P8": [("o1", {}, {"nitidez": "1.0", "edicao": "original"}), ("o1s", sem, {"nitidez": "1.0", "edicao": "original"})],
                     "P9": [("n1", {}, {"nitidez": "1.0"}), ("n1f", {"BS_VK_FALHAR": str(J_FALHA)}, {"nitidez": "1.0"})]}
            res = {}
            for caso in ("P4", "P5", "P6", "P7", "P8", "P9"):
                if caso not in casos:
                    continue
                for nome, env, kw in plano[caso]:
                    if nome not in res:
                        kw = dict(kw)
                        ed = kw.pop("edicao", "remastered")
                        res[nome] = janela(a, nome, ed, env, **kw)
                        print("  %s: %d capturas, mascara em %d slots" % (nome, len(res[nome][0]), slots(res[nome][1])), flush=True)

            def comuns(*ns):
                s = set(res[ns[0]][0])
                for n in ns[1:]:
                    s &= set(res[n][0])
                return sorted(s)

            def dif(x, y, vb):
                return (res[x][0][vb] != res[y][0][vb]).any(axis=2)

            if "P4" in casos:
                check("P4 sem efeitos: a mascara em 0 slots", slots(res["w0"][1]) == 0, "%d" % slots(res["w0"][1]))
                cs = comuns("w0", "cor")
                check("P4 so a cor: 0 slots e a imagem muda", slots(res["cor"][1]) == 0 and len(cs) >= 2 and
                      all(dif("w0", "cor", vb).mean() > 0.5 for vb in cs), "%d slots, %d capturas" % (slots(res["cor"][1]), len(cs)))
            if "P5" in casos:
                cs = comuns("w0", "n1", "n1s")
                check("P5 capturas", len(cs) >= 2, "%d" % len(cs))
                # A apresentacao nao e 1:1 na vertical (a imagem do jogo tem 714 linhas na interna de 720): ela mistura duas
                # linhas. Sem a mascara, as mudancas de sinal oposto da nitidez em linhas vizinhas podem se cancelar na mistura;
                # na rampa da mascara (ate 16 px do HUD) a fracao delas nao se cancela. Um pixel mudado so COM a mascara so pode
                # estar nessa rampa: a ate 16 px de um pixel protegido (mudado so sem ela), e poucos (2026-11-14, investigacao
                # registrada em docs/N9-8-AUDITORIA.md secao 11)
                def perto(m, r):
                    o = m.copy()
                    for k in range(1, r + 1):
                        o[k:] |= m[:-k]
                        o[:-k] |= m[k:]
                    p = o.copy()
                    for k in range(1, r + 1):
                        p[:, k:] |= o[:, :-k]
                        p[:, :-k] |= o[:, k:]
                    return p

                sub, prot, d1n, viol, longe = True, 0, 0, 0, 0
                for vb in cs:
                    d1, d2 = dif("w0", "n1", vb), dif("w0", "n1s", vb)
                    so = d1 & ~d2
                    viol += int(so.sum())
                    longe += int((so & ~perto(d2 & ~d1, 16)).sum())
                    prot += int((d2 & ~d1).sum())
                    d1n += int(d1.sum())
                print("  P5: %d pixels realcados com a mascara, %d protegidos (realcados so sem ela); %d so com a mascara "
                      "(%d longe da rampa)" % (d1n, prot, viol, longe))
                check("P5 a mascara em slots > 0 (e 0 com BS_HUD_MASCARA=0)", slots(res["n1"][1]) > 0 and slots(res["n1s"][1]) == 0)
                check("P5 a nitidez com a mascara so onde ela tambem age sem (fora da rampa do HUD; na rampa < 0,01 %)",
                      d1n > 0 and longe == 0 and viol <= d1n * 1e-4, "%d na rampa, %d longe" % (viol, longe))
                check("P5 ha pixels protegidos (o HUD e as telas 2D)", prot > 0)
                # revisao Codex 20261114-010850: os protegidos tem de ser o HUD (ou o que a mascara tira), nao a cena.
                # (a) os pixels do HUD do canto (o verde do nome, da barra de vida e do canto, embaixo a esquerda da janela de
                # 960x720; a altura dele muda de um quadro para outro): a nitidez com a mascara nao muda nenhum e a sem a mascara
                # muda (medido 2026-11-14: 0 com e 1 337 a 2 995 sem)
                hud = []
                for vb in cs:
                    j = res["w0"][0][vb].astype(int)
                    g = (j[:, :, 1] > 150) & (j[:, :, 0] < 120)
                    g[:600] = False
                    g[:, 520:] = False
                    hud.append((int(g.sum()), int((dif("w0", "n1", vb) & g).sum()), int((dif("w0", "n1s", vb) & g).sum())))
                print("  P5 os pixels do HUD (quantos, mudados com a mascara, sem ela): %s" % hud)
                check("P5 o HUD intacto com a mascara e realcado sem ela", all(n > 1000 and c == 0 and s > 0 for n, c, s in hud), str(hud))
                # (b) a mascara gravada pelo jogo sem janela (a mesma rota e interna, BS_HUD_MASCARA=1) nos mesmos VBlanks: todo
                # protegido dentro dela (a < 255, com 2 px da mistura das linhas). A captura da janela de um VBlank pode mostrar
                # outro render que a sem janela (o personagem andou): so os VBlanks em que as duas imagens coincidem contam (a
                # correlacao das bordas >= 0,5; medida 0,71 e 0,59 quando coincidem e 0,36 quando nao, 2026-11-14)
                b = copy.copy(a)
                hm = Corrida(base, "p5_mascara", b, "remastered", ["--render", "nativo", "--resolucao-interna", "720"], True,
                             exe=val, capturas=",".join("%d-%d" % (vb, vb) for vb in cs))
                hm.env.update({"BS_INPUT_SCRIPT": ROTEIRO_J, "BS_HUD_MASCARA": "1"})
                hm.cmd[hm.cmd.index("--headless") + 1] = str(max(cs) + 10)
                hm.rodar()

                def bordas(g):
                    g = g.astype(np.float64)
                    return np.abs(np.diff(g, axis=0))[:, :-1] + np.abs(np.diff(g, axis=1))[:-1, :]

                usados, fora = [], 0
                for vb in cs:
                    m, hd = hm.img("v%06d_mascara.bmp" % vb), hm.img("v%06d_hd.bmp" % vb)
                    if m is None or hd is None:
                        continue
                    jan = res["w0"][0][vb]
                    hdj = np.asarray(Image.fromarray(hd.astype(np.uint8)).convert("L").resize((jan.shape[1], jan.shape[0]), Image.BILINEAR))
                    cor = float(np.corrcoef(bordas(np.asarray(Image.fromarray(jan).convert("L"))).ravel(), bordas(hdj).ravel())[0, 1])
                    if cor < 0.5:
                        print("  P5 %d: a janela mostra outro render (correlacao %.2f): fora da comparacao com a mascara" % (vb, cor))
                        continue
                    mm = np.asarray(Image.fromarray(m[:, :, 0].astype(np.uint8)).resize((jan.shape[1], jan.shape[0]), Image.NEAREST)) < 255
                    d1, d2 = dif("w0", "n1", vb), dif("w0", "n1s", vb)
                    f = int(((d2 & ~d1) & ~perto(mm, 2)).sum())
                    usados.append((vb, round(cor, 2), f))
                    fora += f
                print("  P5 os protegidos x a mascara sem janela (VBlank, correlacao, fora): %s" % usados)
                check("P5 todo protegido dentro da mascara do jogo (>= 1 VBlank coincidente)", hm.rc == 0 and len(usados) >= 1 and fora == 0,
                      "rc %d, %d VBlanks, %d fora" % (hm.rc, len(usados), fora))
            if "P6" in casos:
                cs = comuns("w0", "b1", "b1s")
                check("P6 a mascara em slots > 0 com o bloom", slots(res["b1"][1]) > 0 and len(cs) >= 2)
                d = [int(dif("b1", "b1s", vb).sum()) for vb in cs]
                g = [int(dif("w0", "b1", vb).sum()) for vb in cs]
                print("  P6: pixels do bloom com a mascara %s; diferentes da sem a mascara %s" % (g, d))
                check("P6 o bloom roda com a mascara", all(x > 0 for x in g))
            if "P7" in casos:
                t = res["tudo"][1]
                check("P7 tudo junto: capturas, mascara e nenhum erro do Vulkan", len(res["tudo"][0]) >= 2 and slots(t) > 0 and
                      "VK_ERROR" not in t and "perda do dispositivo" not in t, "%d slots" % slots(t))
            if "P8" in casos:
                cs = comuns("o1", "o1s")
                check("P8 a Original: 0 slots e a imagem igual a sem a mascara", slots(res["o1"][1]) == 0 and len(cs) >= 2 and
                      all(np.array_equal(res["o1"][0][vb], res["o1s"][0][vb]) for vb in cs), "%d slots" % slots(res["o1"][1]))
            if "P9" in casos:
                t = res["n1f"][1]
                cs = [vb for vb in comuns("n1", "n1f") if vb > J_FALHA + 60]
                check("P9 o dispositivo recriado na janela", "falha do Vulkan injetada no VBlank %d" % J_FALHA in t and
                      "dispositivo recriado" in t)
                check("P9 a mascara continua depois da falha", slots(t) > 0 and len(cs) >= 1, "%d slots, %d capturas" % (slots(t), len(cs)))
                check("P9 as capturas depois da falha iguais as sem ela",
                      len(cs) >= 1 and all(np.array_equal(res["n1"][0][vb], res["n1f"][0][vb]) for vb in cs))
    except SystemExit:
        pass
    finally:
        print("%d falha(s) (%s)" % (len(falhas), base))
        if not a.manter and not falhas:
            shutil.rmtree(base, ignore_errors=True)
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
