"""N9.5 fase 1: o quadro largo (16:9 hor+) sem janela (Vulkan offscreen), com dados sinteticos.

Cada execucao usa uma pasta temporaria propria (copia do exe e instalacao.toml da edicao) e uma copia do cartao de teste.
Roteiro passo_load_slot1 (LOAD do slot 1 e corredor RCG02). Casos:
  H1 Original com --proporcao 16:9: recusado (saida 2), o quadro largo e so da Remastered;
  H2 Remastered com --proporcao 16:9 no software: recusado (saida 2), o software e 4:3;
  H3 Remastered 4:3 x 16:9 (Vulkan, escala 1): estado por passo e efeitos sonoros identicos; a imagem larga (slot) tem
     854 colunas, as laterais com cena e o centro igual ao quadro do jogo (o recorte central) ate a borda de triangulo;
  H4 Remastered 16:9 em 1080, 1440 e 2160 linhas: a imagem larga com round(854 x e) colunas, o centro reduzido igual ao
     quadro do jogo (PSNR) e as laterais com cena; estado identico ao H3;
  H5 Remastered 4:3 na escala 1: nenhuma imagem larga (o caminho do N7c como antes).

  python tools/dev/n95_headless.py --exe build/msvc-release/app/bluestinger.exe --data dist/BlueStinger-Preview/data
                                   --cartao <pasta de saves sintetica> [--casos H1,H3] [--manter]
Saida 1 se algum criterio falhar.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

import numpy as np
from PIL import Image

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(AQUI))
ROTEIRO = os.path.join(RAIZ, "tools", "dist", "roteiros", "passo_load_slot1.txt")
VBLANKS, CAPTURAS = 4700, "3800-3800,4640-4640"


class Corrida:
    def __init__(self, base, nome, a, edicao, extra, capturar=True):
        self.dir = os.path.join(base, nome)
        os.makedirs(self.dir)
        self.exe = os.path.join(self.dir, os.path.basename(a.exe))
        shutil.copy2(a.exe, self.exe)
        if edicao == "remastered":
            open(os.path.join(self.dir, "instalacao.toml"), "w").write('[edicao]\nperfil = "remastered"\n')
        shutil.copytree(a.cartao, os.path.join(self.dir, "saves"))
        self.cap = os.path.join(self.dir, "cap")
        env = dict(os.environ)
        for k in ("BS_INPUT_SCRIPT", "BS_GRAVAR", "BS_ASSISTIDO", "BS_RENDER", "BS_STATE_PASSO", "BS_EFEITOS_WAV"):
            env.pop(k, None)
        env.update({"BS_RELOGIO_FIXO": "1700000000", "BS_INPUT_SCRIPT": ROTEIRO, "BS_STATE_PASSO_CADA": "1",
                    "BS_STATE_PASSO": os.path.join(self.dir, "passos.tsv"), "BS_EFEITOS_WAV": os.path.join(self.dir, "efeitos.wav"),
                    "BS_CAPTURA_VBLANKS": CAPTURAS})
        self.cmd = [self.exe, "--data", os.path.abspath(a.data), "--saves-pasta", os.path.join(self.dir, "saves")] + extra + \
                   ["--headless", str(VBLANKS)]
        if capturar:
            self.cmd += ["--dump-every", "1", "--capture", self.cap]
        self.env = env

    def rodar(self):
        with open(os.path.join(self.dir, "saida.txt"), "w", encoding="utf-8") as out:
            self.rc = subprocess.run(self.cmd, stdout=out, stderr=subprocess.STDOUT, env=self.env, cwd=self.dir, timeout=1800).returncode
        self.log = open(os.path.join(self.dir, "saida.txt"), encoding="utf-8", errors="replace").read()
        return self

    def passos(self):
        return open(os.path.join(self.dir, "passos.tsv")).read().splitlines()

    def wav(self):
        b = open(os.path.join(self.dir, "efeitos.wav"), "rb").read()
        i = b.find(b"data")
        return np.frombuffer(b[i + 8:], dtype="<i2")

    def img(self, nome):
        p = os.path.join(self.cap, nome)
        return np.asarray(Image.open(p).convert("RGB"), dtype=np.float64) if os.path.exists(p) else None


def ancoras(r):
    """N9.5 fase 3B: o retangulo (x0, y0, x1, y1) dos desenhos ancorados de todos os renders (BS_HUD_ANCORAS_LOG), ou None."""
    p = os.path.join(r.dir, "ancoras.tsv")
    if not os.path.exists(p):
        return None
    caixas = [list(map(float, l.split("\t")[2:6])) for l in open(p, encoding="utf-8").read().splitlines() if l.strip()]
    if not caixas:
        return None
    return (int(min(c[0] for c in caixas)), int(min(c[1] for c in caixas)),
            int(max(c[2] for c in caixas) + 0.999), int(max(c[3] for c in caixas) + 0.999))


def psnr(a, b):
    mse = ((a - b) ** 2).mean()
    return 99.0 if mse == 0 else 10 * np.log10(255.0 ** 2 / mse)


def q555(img):  # a imagem da GPU como o framebuffer do jogo a guarda (RGB555 neste trecho, bits replicados na leitura)
    a = img.astype(np.int64) >> 3
    return ((a << 3) | (a >> 2)).astype(np.float64)


def cena(img):  # fracao dos pixels com cena (nao pretos): o vazio fora de uma sala e preto
    return float((img.max(axis=2) > 10).mean())


def reduzir(img, w, h):
    return np.asarray(Image.fromarray(img.astype(np.uint8)).resize((w, h), Image.BOX), dtype=np.float64)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--cartao", required=True, help="pasta de saves SINTETICA com o slot 1")
    ap.add_argument("--casos", default="H1,H2,H3,H4,H5")
    ap.add_argument("--manter", action="store_true")
    a = ap.parse_args()
    a.exe = os.path.abspath(a.exe)
    casos = set(a.casos.split(","))
    base = tempfile.mkdtemp(prefix="n95_headless_")
    falhas = []

    def check(nome, ok, det=""):
        print("[%s] %s %s" % ("ok" if ok else "FALHA", nome, det))
        if not ok:
            falhas.append(nome + " " + det)

    try:
        rodadas = {}
        if "H1" in casos:
            rodadas["h1"] = Corrida(base, "h1", a, "original", ["--render", "nativo", "--proporcao", "16:9"], False)
        if "H2" in casos:
            rodadas["h2"] = Corrida(base, "h2", a, "remastered", ["--render", "software", "--proporcao", "16:9"], False)
        if casos & {"H3", "H4"}:
            rodadas["r43"] = Corrida(base, "r43", a, "remastered", ["--render", "nativo"])
            rodadas["r169"] = Corrida(base, "r169", a, "remastered", ["--render", "nativo", "--proporcao", "16:9"])
            # N9.5 fase 3B: o retangulo dos desenhos ancorados (o HUD do canto), da IR, para o criterio do framebuffer
            rodadas["r169"].env["BS_HUD_ANCORAS_LOG"] = os.path.join(rodadas["r169"].dir, "ancoras.tsv")
        if "H4" in casos:
            for alt in (1080, 1440, 2160):
                rodadas["e%d" % alt] = Corrida(base, "e%d" % alt, a, "remastered",
                                               ["--render", "nativo", "--proporcao", "16:9", "--resolucao-interna", str(alt)])
        if "H5" in casos:
            rodadas["h5"] = Corrida(base, "h5", a, "remastered", ["--render", "nativo", "--proporcao", "4:3"])
        with ThreadPoolExecutor(max_workers=3) as ex:
            list(ex.map(lambda r: r.rodar(), rodadas.values()))

        if "H1" in casos:
            r = rodadas["h1"]
            check("H1 Original recusa 16:9", r.rc == 2 and "so na edicao Remastered" in r.log, "rc=%d" % r.rc)
        if "H2" in casos:
            r = rodadas["h2"]
            check("H2 software recusa 16:9", r.rc == 2 and "--render nativo" in r.log, "rc=%d" % r.rc)
        if casos & {"H3", "H4"}:
            r43, r169 = rodadas["r43"], rodadas["r169"]
            check("H3 execucoes", r43.rc == 0 and r169.rc == 0 and "quadro: largo 854x480" in r169.log and
                  "quadro: largo" not in r43.log, "rc=%d/%d" % (r43.rc, r169.rc))
            pa, pb = r43.passos(), r169.passos()
            check("H3 estado por passo identico", len(pa) > 2000 and pa == pb, "%d/%d linhas" % (len(pa), len(pb)))
            wa, wb = r43.wav(), r169.wav()
            check("H3 efeitos sonoros identicos", len(wa) > 0 and np.array_equal(wa, wb), "%d amostras" % len(wa))
        if "H3" in casos:
            for v in ("003800", "004640"):
                g, hd = r169.img("v%s.bmp" % v), r169.img("v%s_hd.bmp" % v)
                g43 = r43.img("v%s.bmp" % v)
                if g is None or hd is None or g43 is None:
                    check("H3 capturas %s" % v, False, "faltam")
                    continue
                check("H3 %s imagem larga 854 colunas" % v, hd.shape[1] == 854 and hd.shape[0] == g.shape[0], str(hd.shape))
                # o centro da imagem larga, no formato do framebuffer, e o quadro que o jogo recebeu (o mesmo render)
                centro = q555(hd[:, 107:747])
                difc = (np.abs(centro - g).max(axis=2) > 0).sum()
                check("H3 %s centro da larga = quadro do jogo (555)" % v, difc == 0,
                      "%d px" % difc)
                difg = np.abs(g - g43).max(axis=2)
                anc = ancoras(r169)
                if anc is None:
                    # revisao Codex 20261109-171852: a Remastered 16:9 desta rota mostra o HUD do canto; sem ancora nenhuma, a
                    # marca regrediu (o recorte central ficaria igual ao 4:3 e o criterio antigo passaria sem medir o HUD)
                    check("H3 %s HUD do canto ancorado (BS_HUD_ANCORAS_LOG)" % v, False, "nenhum desenho ancorado")
                    check("H3 %s quadro do jogo 16:9 x 4:3 (bordas de triangulo)" % v, (difg > 0).sum() <= 0.001 * difg.size,
                          "%d px, max %d" % ((difg > 0).sum(), difg.max()))
                else:
                    # N9.5 fase 3B (decisao do mantenedor, 2026-11-09): com o HUD do canto ancorado, o framebuffer (o recorte
                    # central do quadro largo) perde o HUD. O criterio continua inteiro fora dos retangulos do HUD, que vem da
                    # IR (BS_HUD_ANCORAS_LOG): onde o HUD esta no 4:3 e onde a parte dele que cai no recorte aparece (-107)
                    x0, y0, x1, y1 = anc
                    fora = np.ones(difg.shape, dtype=bool)
                    for dx in (0, -107):
                        fora[max(0, y0):y1 + 1, max(0, x0 + dx):max(0, x1 + 1 + dx)] = False
                    # o retangulo nao pode tomar o quadro (o criterio ficaria vazio): o HUD do canto ocupa uma faixa pequena
                    check("H3 %s retangulo do HUD ancorado pequeno" % v, (~fora).sum() <= 0.10 * fora.size,
                          "%.1f%% do quadro" % (100.0 * (~fora).sum() / fora.size))
                    d = difg[fora]
                    check("H3 %s quadro do jogo 16:9 x 4:3 fora do HUD ancorado (bordas de triangulo)" % v,
                          (d > 0).sum() <= 0.001 * d.size, "%d px, max %d; HUD x %d..%d y %d..%d" % (
                              (d > 0).sum(), d.max() if d.size else 0, x0, x1, y0, y1))
                    # o HUD ancorado no quadro largo, na posicao x do 4:3: os pixels do HUD (verdes) do 4:3 iguais no largo
                    hud = np.zeros(difg.shape, dtype=bool)
                    hud[max(0, y0):y1 + 1, max(0, x0):x1 + 1] = True
                    hud &= (g43[..., 1] > 150) & (g43[..., 0] < 120)
                    iguais = (np.abs(hd[:, :g43.shape[1]][hud] - g43[hud]).max(axis=1) <= 24).mean() if hud.any() else 0
                    check("H3 %s HUD ancorado na borda esquerda do quadro largo" % v, hud.sum() > 100 and iguais >= 0.95,
                          "%d px do HUD, %.1f%% iguais" % (hud.sum(), 100 * iguais))
                esq, dir_ = cena(hd[:, :100]), cena(hd[:, -100:])
                if v == "003800":  # o corredor de frente: as duas laterais tem a sala
                    check("H3 %s laterais com cena" % v, esq > 0.5 and dir_ > 0.5, "%.0f%% / %.0f%%" % (100 * esq, 100 * dir_))
                else:  # informativo (B2, fase 4): o vazio fora da sala aparece nas faixas onde o jogo nao tem geometria
                    print("[info] H3 %s laterais com cena: %.0f%% / %.0f%% (o resto: vazio fora da sala, B2)" % (v, 100 * esq, 100 * dir_))
                # H6 (N9.5 fase 4, o pilar dinamico): no fim do corredor (004640) a IR nao cobre a faixa esquerda (o vazio fora da
                # sala) e ela vira pilar: fora do HUD, nenhuma cena, borda reta em x = 107. No corredor de frente (003800), sem
                # pilar (as duas faixas com a sala, conferidas acima)
                faixa = hd[:, :107].copy()
                if anc is not None:
                    x0, y0, x1, y1 = anc
                    faixa[max(0, y0):y1 + 1, max(0, x0):min(107, x1 + 1)] = 0  # o HUD fica por cima do pilar
                # revisao Codex 20261109-203527: o estado do pilar desta captura (o produto o registra ao gravar o _hd)
                estado = "[captura] v%s pilar esquerda=%d direita=0" % (v, 1 if v == "004640" else 0)
                check("H6 %s estado do pilar na captura" % v, estado in r169.log, estado)
                if v == "004640":
                    check("H6 %s faixa esquerda so com o fundo fora do HUD" % v, cena(faixa) <= 0.01, "%.1f%% com cena" % (100 * cena(faixa)))
                    check("H6 %s centro intacto (o 4:3 do quadro largo)" % v, cena(hd[:, 107:747]) > 0.5, "%.0f%%" % (100 * cena(hd[:, 107:747])))
                else:
                    check("H6 %s sem pilar no corredor de frente" % v, cena(faixa) > 0.5, "%.0f%% com cena" % (100 * cena(faixa)))
        if "H4" in casos:
            for alt in (1080, 1440, 2160):
                r = rodadas["e%d" % alt]
                e = alt / 480.0
                ok = r.rc == 0 and r.passos() == r43.passos()
                check("H4 %d estado identico" % alt, ok, "rc=%d" % r.rc)
                for v in ("003800",):
                    g, hd = r.img("v%s.bmp" % v), r.img("v%s_hd.bmp" % v)
                    if g is None or hd is None:
                        check("H4 %d captura %s" % (alt, v), False, "faltam")
                        continue
                    wesp = int(854 * e + 0.5)
                    check("H4 %d largura %d" % (alt, wesp), hd.shape[1] == wesp, str(hd.shape))
                    x0 = int(round(107 * e))
                    c = reduzir(hd[:, x0:x0 + int(round(640 * e))], g.shape[1], g.shape[0])
                    p = psnr(c, g)
                    check("H4 %d centro reduzido ~ quadro do jogo" % alt, p >= 30, "PSNR %.1f dB" % p)
                    esq, dir_ = cena(hd[:, :int(90 * e)]), cena(hd[:, -int(90 * e):])
                    check("H4 %d laterais com cena" % alt, esq > 0.5 and dir_ > 0.5, "%.0f%% / %.0f%%" % (100 * esq, 100 * dir_))
        if "H5" in casos:
            r = rodadas["h5"]
            hds = [f for f in os.listdir(r.cap) if f.endswith("_hd.bmp")] if os.path.isdir(r.cap) else []
            check("H5 4:3 na escala 1 sem imagem larga", r.rc == 0 and not hds and "quadro: largo" not in r.log, "%d _hd" % len(hds))
    finally:
        if a.manter:
            print("pasta mantida: " + base)
        else:
            shutil.rmtree(base, ignore_errors=True)
    print("\n%d falha(s)" % len(falhas))
    sys.exit(1 if falhas else 0)


if __name__ == "__main__":
    main()
