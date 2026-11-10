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
  J6 (fase 5B) Remastered, a troca 4:3 -> 16:9 -> 4:3 pelo menu da pausa durante o gameplay (controle virtual), ~2 min.
  J7 (fase 5B, --exe-validacao) a altura junto com a largura, com o limite da GPU simulado (ver j7), ~3 min.

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


def j6(a, falhas, nome="J6", exe_origem=None, res="original", env_extra=None, conferir=None):
    """N9.5 fase 5B: a troca 4:3 -> 16:9 -> 4:3 pelo menu da pausa, durante o gameplay, na janela real (1280x720). Controle
    virtual (so a janela do jogo recebe os botoes): no menu inicial, Continuar (o perfil do cartao de teste: o corredor RCG02),
    pausa, Configuracoes > Graficos > Proporcao, Direita, volta ao jogo; mais tarde o mesmo para voltar ao 4:3. As capturas da
    imagem APRESENTADA (BS_JANELA_CAPTURA, nao a area de trabalho) de cada trecho: 4:3 com as laterais pretas, 16:9 com a cena
    na largura inteira, e de novo 4:3 sem nenhum quadro largo velho."""
    tmp = tempfile.mkdtemp(prefix="n95_janela_%s_" % nome)
    try:
        exe = os.path.join(tmp, os.path.basename(exe_origem or a.exe))
        shutil.copy2(exe_origem or a.exe, exe)
        open(os.path.join(tmp, "instalacao.toml"), "w").write('[edicao]\nperfil = "remastered"\n')
        open(os.path.join(tmp, "graficos.toml"), "w").write(GRAFICOS.format(res=res, w=1280, h=720, prop="4:3"))
        shutil.copytree(a.cartao, os.path.join(tmp, "saves"))
        # pausa (View/Back), Continuar 0 / Salvar (desativado) / Carregar 1 / Configuracoes 2; principal: Graficos; Proporcao 3
        troca = ["back", "dpdown", "dpdown", "a", "a"] + ["dpdown"] * 3 + ["dpright", "b", "b", "b"]
        linhas, t = ["0 conectar"], 32000
        for marca, botoes in ((32000, ["a"]), (62000, troca), (92000, troca)):
            t = marca
            for b in botoes:
                linhas += ["%d %s 1" % (t, b), "%d %s 0" % (t + 100, b)]
                t += 600
        linhas.append("%d sair" % (t + 20000))
        pad = os.path.join(tmp, "pad.txt")
        open(pad, "w").write("\n".join(linhas) + "\n")
        cap = os.path.join(tmp, "cap")
        env = {k: v for k, v in os.environ.items() if not k.startswith("BS_")}
        env.update({"BS_RELOGIO_FIXO": "1700000000", "BS_CONTROLE_VIRTUAL": pad, "BS_JANELA_CAPTURA": cap,
                    "BS_JANELA_CAPTURA_CADA": "60"})
        env.update(env_extra or {})
        log = os.path.join(tmp, "jogo.txt")
        with open(log, "w", encoding="utf-8") as out:
            p = subprocess.Popen([exe, "--data", os.path.abspath(a.data), "--saves-pasta", os.path.join(tmp, "saves"),
                                  "--apresentacao", "vulkan"], stdout=out, stderr=subprocess.STDOUT, env=env, cwd=tmp)
            try:
                p.wait(200)
            except subprocess.TimeoutExpired:  # so o processo deste caso
                p.kill()
        texto = open(log, encoding="utf-8", errors="replace").read()
        pausas = [int(x) for x in re.findall(r"\[remaster\] pausa no VBlank (\d+)", texto)]
        trocas = re.findall(r"\[menu\] proporcao: ([^\n]+)", texto)
        caps = sorted(os.listdir(cap)) if os.path.isdir(cap) else []
        print("[%s] remastered, troca pelo menu da pausa, janela 1280x720: pausas nos VBlanks %s; trocas %s; %d capturas" %
              (nome, pausas, trocas, len(caps)))
        f0 = len(falhas)
        if p.returncode != 0:
            falhas.append("%s: rc=%s" % (nome, p.returncode))
        if len(pausas) != 2 or trocas != ["16:9 (quadro largo)", "4:3"]:
            falhas.append("%s: esperava 2 pausas e as trocas 16:9 e 4:3 (%s, %s)" % (nome, pausas, trocas))
        else:
            fases = {"4:3 antes": [], "16:9": [], "4:3 depois": []}
            for x in caps:
                v = int(re.match(r"janela_v(\d+)_", x).group(1))
                # longe das pausas (a interface e o fade); o jogo nao anda pausado, entao o VBlank e o do jogo. Antes da
                # primeira pausa, so os 12 s de gameplay que a antecedem (o log nao da o VBlank do fim do carregamento; antes
                # dele vem o boot, o titulo e a tela de espera, sem o corredor: a primeira execucao, de 2026-11-10, os contou)
                if pausas[0] - 30 - 12 * 60 <= v < pausas[0] - 30:
                    fases["4:3 antes"].append(x)
                elif pausas[0] + 60 < v < pausas[1] - 30:
                    fases["16:9"].append(x)
                elif v > pausas[1] + 60:
                    fases["4:3 depois"].append(x)
            x0, y0, aw, ah = fit(1280, 720, 640, 480)
            for fase, lista in fases.items():
                if len(lista) < 2:
                    falhas.append("%s: so %d capturas no trecho %s" % (nome, len(lista), fase))
                    continue
                comCena = 0
                for x in lista:
                    iw, ih, px = ler_bmp(os.path.join(cap, x))
                    fora = media(px, 0, max(1, x0 - 2), 0, ih)  # as faixas do 4:3 na janela 16:9
                    dentro = media(px, x0 + 8, x0 + aw - 8, y0 + 8, y0 + ah - 8)
                    if dentro < 4:
                        falhas.append("%s %s: area do jogo vazia (%s)" % (nome, x, fase))
                    if fase == "16:9":
                        comCena += fora >= 4
                    elif fora > 1:
                        falhas.append("%s %s: laterais nao pretas no %s (%.1f): quadro largo fora de hora" % (nome, x, fase, fora))
                print("    %s: %d capturas%s" % (fase, len(lista), (", %d com cena nas laterais" % comCena) if fase == "16:9" else ""))
                if fase == "16:9" and comCena * 2 <= len(lista):
                    falhas.append("%s: so %d de %d capturas do 16:9 com cena nas laterais" % (nome, comCena, len(lista)))
        if conferir:
            falhas.extend("%s: %s" % (nome, f) for f in conferir(texto))
        print("    %s" % ("ok" if len(falhas) == f0 else "FALHOU"))
    finally:
        if a.manter:
            print("    pasta mantida: " + tmp)
        else:
            shutil.rmtree(tmp, ignore_errors=True)


def tamanhos(texto):
    """Os alvos de cada render (BS_VK_LENTOS com limite minimo: uma linha por render, com o tamanho do alvo)."""
    return [(int(w), int(h)) for w, h in re.findall(r"\[render-vk\] render lento \d+: [^;]*;[^;]*;[^;]*;[^;]*; (\d+)x(\d+);", texto)]


def j7(a, falhas):
    """N9.5 fase 5B (revisao Codex 20261110-011045; exe de validacao): a altura e a largura juntas, com o limite da GPU
    simulado (BS_VK_ALTURA_MAX=2160: no quadro largo, 1618, isto e, 1440 em uso).
      J7a 2160p no 4:3, troca pelo menu para o 16:9 e de volta: nenhum render largo acima do limite (3843x2160), 1440 em uso
          no 16:9 (2562x1440) e a escolha inteira de volta no 4:3 (2880x2160);
      J7b 2160p no 16:9 (1440 em uso) e o recuo para o SDL (BS_APRESENTACAO_FALHAR): a altura escolhida volta com o 4:3;
      J7c 4:3 e o Vulkan desistindo (BS_VK_FALHAR=...:sempre): o 16:9 sai do menu."""
    if not a.exe_validacao:
        falhas.append("J7: precisa do --exe-validacao")
        return
    ev = os.path.abspath(a.exe_validacao)
    base = {"BS_VK_ALTURA_MAX": "2160", "BS_VK_LENTOS": "0.0001"}

    def conferir_a(texto):
        f, ts = [], tamanhos(texto)
        if "[validacao] limite da GPU simulado: 2160" not in texto:
            f.append("sem o limite simulado")
        if "[menu] resolucao interna: 2160 pedida, 1440 em uso (limite da GPU para o quadro largo)" not in texto:
            f.append("sem a reducao para 1440 no 16:9")
        acima = sorted({t for t in ts if t[0] > 2880})
        if acima:
            f.append("renders acima do limite: %s" % acima)
        for t in ((2562, 1440), (2880, 2160)):
            if t not in ts:
                f.append("nenhum render %dx%d" % t)
        # a ordem: 2160 (4:3), 1440 largo, 2160 de novo
        seq = [t for i, t in enumerate(ts) if i == 0 or t != ts[i - 1]]
        if [t for t in seq if t in ((2880, 2160), (2562, 1440))][-3:] != [(2880, 2160), (2562, 1440), (2880, 2160)]:
            f.append("sequencia de alvos inesperada: %s" % seq[-6:])
        print("    alvos (na ordem, sem repeticao): %s" % seq[-6:])
        return f

    j6(a, falhas, nome="J7a", exe_origem=ev, res="2160p", env_extra=base, conferir=conferir_a)
    for nome, graf, env, esperado in (
            ("J7b", "16:9", {"BS_APRESENTACAO_FALHAR": "600"},
             ["[menu] resolucao interna: 2160 pedida, 1440 em uso", "[menu] proporcao: 4:3 (a apresentacao SDL nao desenha o quadro largo)",
              "[render] resolucao interna: escala 4.5 (2880x2160)"]),
            ("J7c", "4:3", {"BS_VK_FALHAR": "900:sempre"},
             ["[validacao] falha do Vulkan injetada no VBlank 900 (permanente)",
              "[menu] proporcao: 4:3 (o renderizador ficou no software: a recuperacao do Vulkan falhou)"])):
        tmp = tempfile.mkdtemp(prefix="n95_janela_%s_" % nome)
        try:
            exe = os.path.join(tmp, os.path.basename(ev))
            shutil.copy2(ev, exe)
            open(os.path.join(tmp, "instalacao.toml"), "w").write('[edicao]\nperfil = "remastered"\n')
            open(os.path.join(tmp, "graficos.toml"), "w").write(GRAFICOS.format(res="2160p", w=1280, h=720, prop=graf))
            pad = os.path.join(tmp, "pad.txt")
            open(pad, "w").write("0 conectar\n30000 sair\n")
            e = {k: v for k, v in os.environ.items() if not k.startswith("BS_")}
            e.update(base), e.update(env), e.update({"BS_RELOGIO_FIXO": "1700000000", "BS_CONTROLE_VIRTUAL": pad})
            log = os.path.join(tmp, "jogo.txt")
            with open(log, "w", encoding="utf-8") as out:
                pr = subprocess.Popen([exe, "--data", os.path.abspath(a.data), "--saves-pasta", os.path.join(tmp, "saves"),
                                       "--apresentacao", "vulkan"], stdout=out, stderr=subprocess.STDOUT, env=e, cwd=tmp)
                try:
                    pr.wait(120)
                except subprocess.TimeoutExpired:  # so o processo deste caso
                    pr.kill()
            texto = open(log, encoding="utf-8", errors="replace").read()
            pos, f0 = -1, len(falhas)
            for linha in esperado:  # na ordem
                q = texto.find(linha, pos + 1)
                if q < 0:
                    falhas.append("%s: sem a linha (na ordem) %r" % (nome, linha))
                    break
                pos = q
            ts = tamanhos(texto)
            if nome == "J7b" and sorted({t for t in ts if t[0] > 2880}):
                falhas.append("%s: renders acima do limite: %s" % (nome, sorted({t for t in ts if t[0] > 2880})))
            print("[%s] %s: rc=%s; %s" % (nome, "recuo para o SDL com 1440 em uso" if nome == "J7b" else "o Vulkan desiste no 4:3",
                                          pr.returncode, "ok" if len(falhas) == f0 else "FALHOU"))
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
    ap.add_argument("--exe-validacao", help="exe de validacao (para o J7)")
    a = ap.parse_args()
    a.exe = os.path.abspath(a.exe)
    falhas = []
    for n in a.casos.split(","):
        if n == "J6":
            j6(a, falhas)
        elif n == "J7":
            j7(a, falhas)
        else:
            caso(n, CASOS[n], a, falhas)
    print("\n%d falha(s)" % len(falhas))
    for f in falhas:
        print("  " + f)
    sys.exit(1 if falhas else 0)


if __name__ == "__main__":
    main()
