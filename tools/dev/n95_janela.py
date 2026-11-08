"""N9.5 fase 1: o quadro largo (16:9 hor+) APRESENTADO na janela, com o Vulkan e a apresentacao direta.

Cada caso roda o executavel numa pasta temporaria propria (copia do exe, instalacao.toml da edicao e graficos.toml do
caso; nada da pasta do usuario e tocado), no modo espectador com o roteiro sintetico passo_load_slot1 (LOAD do slot 1 do
cartao de teste e o corredor RCG02), e captura a imagem APRESENTADA pelo proprio jogo (BS_JANELA_CAPTURA: a imagem da
swapchain, nao a area de trabalho). So o processo do teste e encerrado no fim.

Casos (todos com a janela do Blue Stinger aberta durante ~1,5 min cada):
  J1 Remastered, 16:9, janela 1280x720, resolucao interna 1080: a imagem ocupa a janela inteira e as laterais tem cena;
  J2 Remastered, 16:9, janela 1024x768 (4:3): faixas pretas em cima e embaixo, a imagem larga na largura inteira;
  J3 Original com o graficos.toml pedindo 16:9, janela 1280x720: continua 4:3 (faixas pretas nas laterais), com o motivo;
  J4 Remastered, 4:3, janela 1280x720: igual a Original (faixas pretas nas laterais);
  J5 Remastered pedindo 16:9 com a apresentacao SDL (sem slots): fica 4:3, sem esticar a imagem do jogo.

  python tools/dev/n95_janela.py --exe build/msvc-release/app/bluestinger.exe --data dist/BlueStinger-Preview/data
                                 --cartao <pasta de saves sintetica> [--casos J1,J2] [--manter]
Saida 1 se algum criterio falhar.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(AQUI))
ROTEIRO = os.path.join(RAIZ, "tools", "dist", "roteiros", "passo_load_slot1.txt")
INI, FIM, CADA = 4300, 4640, 85  # VBlanks do corredor RCG02 no roteiro (passos ~2100 a 2290)

GRAFICOS = """preset = "personalizado"
[imagem]
resolucao = "{res}"
[tela]
modo = "janela"
largura = {w}
altura = {h}
proporcao = "{prop}"
renderizador = "vulkan"
"""

CASOS = {
    "J1": dict(edicao="remastered", prop="16:9", janela=(1280, 720), res="1080p", largo=True),
    "J2": dict(edicao="remastered", prop="16:9", janela=(1024, 768), res="original", largo=True),
    "J3": dict(edicao="original", prop="16:9", janela=(1280, 720), res="original", largo=False),
    "J4": dict(edicao="remastered", prop="4:3", janela=(1280, 720), res="original", largo=False),
    "J5": dict(edicao="remastered", prop="16:9", janela=(1280, 720), res="original", largo=False, apres="sdl"),
}


def ler_bmp(p):
    b = open(p, "rb").read()
    off, w, h, bpp = struct.unpack_from("<I", b, 10)[0], struct.unpack_from("<i", b, 18)[0], struct.unpack_from("<i", b, 22)[0], \
        struct.unpack_from("<H", b, 28)[0]
    stride = (w * (bpp // 8) + 3) & ~3
    top = h < 0
    h = abs(h)

    def px(x, y):
        yy = y if top else h - 1 - y
        o = off + yy * stride + x * (bpp // 8)
        return b[o + 2], b[o + 1], b[o]
    return w, h, px


def fit(w, h, fw, fh):  # a mesma conta de native/render/wide_frame (fit_area)
    aw, ah = w, w * fh // fw
    if ah > h:
        ah, aw = h, h * fw // fh
    return (w - aw) // 2, (h - ah) // 2, aw, ah


def lum(px, x, y):
    r, g, b = px(x, y)
    return (r + g + b) / 3


def media(px, x0, x1, y0, y1, passo=7):
    v = [lum(px, x, y) for x in range(x0, x1, passo) for y in range(y0, y1, passo)]
    return sum(v) / len(v) if v else 0.0


def caso(nome, c, a, falhas):
    tmp = tempfile.mkdtemp(prefix="n95_janela_%s_" % nome)
    try:
        exe = os.path.join(tmp, os.path.basename(a.exe))
        shutil.copy2(a.exe, exe)
        if c["edicao"] == "remastered":
            open(os.path.join(tmp, "instalacao.toml"), "w").write('[edicao]\nperfil = "remastered"\n')
        w, h = c["janela"]
        open(os.path.join(tmp, "graficos.toml"), "w").write(GRAFICOS.format(res=c["res"], w=w, h=h, prop=c["prop"]))
        shutil.copytree(a.cartao, os.path.join(tmp, "saves"))
        cap = os.path.join(tmp, "cap")
        env = dict(os.environ)
        for k in ("BS_INPUT_SCRIPT", "BS_GRAVAR", "BS_ASSISTIDO", "BS_RENDER", "BS_APRESENTACAO", "BS_CONTROLE_VIRTUAL"):
            env.pop(k, None)
        env.update({"BS_RELOGIO_FIXO": "1700000000", "BS_INPUT_SCRIPT": ROTEIRO, "BS_JANELA_CAPTURA": cap,
                    "BS_JANELA_CAPTURA_CADA": str(CADA), "BS_JANELA_CAPTURA_VBLANKS": "%d-%d" % (INI, FIM)})
        cmd = [exe, "--data", os.path.abspath(a.data), "--saves-pasta", os.path.join(tmp, "saves"), "--apresentacao", c.get("apres", "vulkan")]
        log = os.path.join(tmp, "jogo.txt")
        with open(log, "w", encoding="utf-8") as out:
            p = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env=env, cwd=tmp)
            limite = time.time() + FIM / 40 + 90
            while p.poll() is None and time.time() < limite:
                caps = os.listdir(cap) if os.path.isdir(cap) else []
                if any(int(re.match(r"janela_v(\d+)_", x).group(1)) >= FIM - CADA for x in caps):
                    break
                time.sleep(2)
            if p.poll() is None:  # so o processo deste caso
                p.terminate()
                try:
                    p.wait(20)
                except subprocess.TimeoutExpired:
                    p.kill()
        texto = open(log, encoding="utf-8", errors="replace").read()
        caps = sorted(os.listdir(cap)) if os.path.isdir(cap) else []
        largo = "[render] quadro: largo 854x480" in texto
        print("[%s] %s, %s, janela %dx%d, interna %s: %d capturas; quadro largo no log: %s" %
              (nome, c["edicao"], c["prop"], w, h, c["res"], len(caps), "sim" if largo else "nao"))
        for l in re.findall(r"\[(?:apresentacao|menu)\][^\n]*(?:proporcao|16:9|Vulkan direta)[^\n]*", texto)[:3]:
            print("    " + l)
        f0 = len(falhas)
        comCena = []
        if not caps:
            falhas.append("%s: nenhuma captura" % nome)
        if largo != c["largo"]:
            falhas.append("%s: quadro largo %s no log" % (nome, "ligado" if largo else "desligado"))
        for x in caps:
            m = re.match(r"janela_v(\d+)_(\d+)x(\d+)\.bmp", x)
            cw, ch = int(m.group(2)), int(m.group(3))
            iw, ih, px = ler_bmp(os.path.join(cap, x))
            if (iw, ih) != (w, h):
                falhas.append("%s %s: captura %dx%d em vez de %dx%d" % (nome, x, iw, ih, w, h))
                continue
            if c["largo"]:
                x0, y0, aw, ah = fit(w, h, 854, 480)
                faixa = max(8, aw * 107 // 854 - 8)  # as faixas laterais do quadro largo (fora do 4:3 central)
                esq = media(px, x0 + 4, x0 + faixa, y0 + ah // 4, y0 + 3 * ah // 4)
                dir_ = media(px, x0 + aw - faixa, x0 + aw - 4, y0 + ah // 4, y0 + 3 * ah // 4)
                cima = media(px, 0, w, 0, y0) if y0 > 4 else 0.0
                print("    %s: area %dx%d em (%d,%d); laterais %.1f / %.1f; acima da area %.1f" % (x, aw, ah, x0, y0, esq, dir_, cima))
                # o fim do corredor mostra o vazio fora da sala numa lateral (B2, fase 4): a maioria das capturas tem de ter
                # cena nas duas
                comCena.append(esq >= 4 and dir_ >= 4)
                if y0 > 4 and cima > 1:
                    falhas.append("%s %s: a faixa de cima deveria ser preta (%.1f)" % (nome, x, cima))
            else:
                x0, y0, aw, ah = fit(w, h, 640, 480)
                fora = media(px, 0, max(1, x0 - 2), 0, h) if x0 > 4 else 0.0
                dentro = media(px, x0 + 8, x0 + aw - 8, y0 + 8, y0 + ah - 8)
                print("    %s: area 4:3 %dx%d em (%d,%d); fora %.1f, dentro %.1f" % (x, aw, ah, x0, y0, fora, dentro))
                if fora > 1:
                    falhas.append("%s %s: faixas laterais nao pretas no 4:3 (%.1f)" % (nome, x, fora))
                if dentro < 4:
                    falhas.append("%s %s: area do jogo vazia" % (nome, x))
        if c["largo"] and caps:
            print("    capturas com cena nas duas laterais: %d de %d (as outras: vazio fora da sala, B2)" % (sum(comCena), len(comCena)))
            if sum(comCena) * 2 <= len(comCena):
                falhas.append("%s: so %d de %d capturas com cena nas laterais" % (nome, sum(comCena), len(comCena)))
        print("    %s" % ("ok" if len(falhas) == f0 else "FALHOU"))
    finally:
        if a.manter:
            print("    pasta mantida: " + tmp)
        else:
            shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--cartao", required=True, help="pasta de saves SINTETICA com o slot 1 (copiada para a pasta do caso)")
    ap.add_argument("--casos", default="J1,J2,J3,J4,J5")
    ap.add_argument("--manter", action="store_true")
    a = ap.parse_args()
    a.exe = os.path.abspath(a.exe)
    falhas = []
    for n in a.casos.split(","):
        caso(n, CASOS[n], a, falhas)
    print("\n%d falha(s)" % len(falhas))
    for f in falhas:
        print("  " + f)
    sys.exit(1 if falhas else 0)


if __name__ == "__main__":
    main()
