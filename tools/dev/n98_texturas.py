"""N9.8 fase 8.2 (docs/N9-8-AUDITORIA.md secao 9): as texturas HD da Remastered sem janela (Vulkan offscreen), na rota
passo_cgate ate o cais RCG00, com a resolucao interna de 1440 linhas. As texturas so mudam a imagem: o estado do jogo e o som
nao podem mudar.

Nenhuma arte vem com o projeto. A suite exporta as texturas originais desta execucao (o exe de validacao,
BS_TEXTURAS_EXPORTAR) para a pasta temporaria e monta com elas os pacotes de teste, locais (apagados no fim sem --manter):
  - "identidade": cada textura no seu proprio tamanho (fator 1, os mesmos texels): o oraculo do carregador; a imagem tem de
    ficar igual bit a bit a sem pacote (a ordem dos canais, o alfa, o material da original e o adiamento pelo limite do quadro);
  - "x4": cada textura ampliada 4x por repeticao (sem arte nova, o tamanho de um pacote HD de fator 4);
  - "recuo": metade das texturas da identidade e, no lugar das outras, arquivos que tem de ser recusados (outra proporcao,
    PNG cortado, um que nao e PNG) e nomes que nao sao texturas.

Casos:
  T1 recusas: a Original com --texturas-hd ligado e a Remastered no software saem com 2;
  T2 identidade: estado por passo e efeitos sonoros identicos a sem pacote; substituicoes > 0 e nenhuma recusada; adiamentos
     pelo limite do quadro > 0 (a primeira cena pede mais texturas que o limite) e mesmo assim a imagem do jogo e a de 1440
     iguais bit a bit a sem pacote;
  T3 x4 com a filtragem anisotropica e a agua alta (16:9 em 1080) x os mesmos sem pacote: estado e som identicos; fator 4
     carregado; a memoria das texturas (16x a da identidade); a agua nos mesmos desenhos (o material e o da original); o
     custo da GPU por quadro (medido, registrado);
  T4 recuo: as validas carregam, as outras sao recusadas com o motivo (nenhuma decodificada como imagem errada); a imagem igual
     a sem pacote; os nomes ignorados avisados.

  python tools/dev/n98_texturas.py --exe <produto> --exe-validacao <validacao> --data <data> [--casos T1,...] [--manter]
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
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n95_headless import Corrida  # noqa: E402

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROTEIRO = os.path.join(RAIZ, "tools", "dist", "roteiros", "passo_cgate.txt")
VBLANKS = 34300
VBS = ("004000", "012000", "030700", "034200")
VBS_HD = ("030700", "034200")  # a imagem na resolucao interna so existe nos renders da cena (4000 e 12000: telas da CPU)
CAPTURAS = ",".join("%d-%d" % (int(v), int(v)) for v in VBS)
NOME = re.compile(r"^(\d+)x(\d+)-[0-9a-f]{16}\.png$")
MOD = 'formato = 1\nid = "%s"\nnome = "Pacote de teste %s (local)"\nversao = "0"\nprioridade = %d\njogo = "T13001N V1.000"\n'


def pacote(mods, ident, prioridade=10):
    d = os.path.join(mods, ident)
    os.makedirs(os.path.join(d, "texturas"))
    open(os.path.join(d, "mod.toml"), "w", encoding="utf-8").write(MOD % (ident, ident, prioridade))
    return os.path.join(d, "texturas")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--exe-validacao", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--casos", default="T1,T2,T3,T4")
    ap.add_argument("--manter", action="store_true")
    a = ap.parse_args()
    a.exe, a.exe_validacao = os.path.abspath(a.exe), os.path.abspath(a.exe_validacao)
    casos = set(a.casos.split(","))
    base = tempfile.mkdtemp(prefix="n98_texturas_")
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

    def resumo(c):  # (substituidas, incompativeis, adiadas, exportadas) do resumo do Vulkan
        m = re.search(r"HD (\d+), HD incompativeis (\d+), HD adiadas (\d+), exportadas (\d+)\)", c.log)
        return tuple(int(x) for x in m.groups()) if m else (-1, -1, -1, -1)

    def provedor(c):  # (carregadas, MiB, fator, recusadas, adiamentos) do [texturas-hd]
        m = re.search(r"\[texturas-hd\] pacotes \d+, no indice \d+; carregadas (\d+) \(([\d.]+) MiB, fator ate (\d+)\); recusadas (\d+); "
                      r"adiamentos pelo limite do quadro (\d+)", c.log)
        return (int(m[1]), float(m[2]), int(m[3]), int(m[4]), int(m[5])) if m else (-1, -1.0, -1, -1, -1)

    def gpu_ms(c):
        m = re.search(r"GPU por quadro: media ([\d.]+) ms", c.log)
        return float(m[1]) if m else -1.0

    def agua(c):
        m = re.search(r"agua em (\d+) renders \((\d+) desenhos", c.log)
        return (int(m[1]), int(m[2])) if m else (-1, -1)

    def igual_estado(x, y, nome):
        check(nome + ": estado por passo identico", x.rc == 0 and y.rc == 0 and x.passos() == y.passos() and len(x.passos()) > 1000,
              "rc %d/%d" % (x.rc, y.rc))
        check(nome + ": efeitos sonoros identicos", x.rc == 0 and y.rc == 0 and np.array_equal(x.wav(), y.wav()))

    def iguais(x, y, nome):
        for sufixo, vbs, rot in (("", VBS, "imagem do jogo"), ("_hd", VBS_HD, "imagem em 1440")):
            ix = [x.img("v%s%s.bmp" % (v, sufixo)) for v in vbs]
            iy = [y.img("v%s%s.bmp" % (v, sufixo)) for v in vbs]
            ok = all(i is not None for i in ix + iy)
            check("%s: %s igual bit a bit" % (nome, rot), ok and all(np.array_equal(p, q) for p, q in zip(ix, iy)),
                  "" if ok else "capturas ausentes")

    def moverPacote(c, mods):  # o pacote fica em <pasta da corrida>\mods (ao lado do exe)
        shutil.copytree(mods, os.path.join(c.dir, "mods"))

    try:
        nat = ["--render", "nativo", "--resolucao-interna", "1440"]
        hd = ["--texturas-hd", "ligado"]
        wide = ["--render", "nativo", "--proporcao", "16:9", "--resolucao-interna", "1080", "--filtragem", "anisotropica",
                "--agua", "alta"]
        r = {}
        # 1) a exportacao (validacao) e a referencia do produto, juntas
        orig = os.path.join(base, "_originais")
        exp = corrida("exportar", "remastered", nat, exe=a.exe_validacao, env={"BS_TEXTURAS_EXPORTAR": orig})
        # o produto recebe a mesma variavel e tem de ignora-la (o exportador so existe na validacao; revisao Codex 2026-11-13)
        semexp = os.path.join(base, "_produto_nao_exporta")
        r["off"] = corrida("off", "remastered", nat, env={"BS_TEXTURAS_EXPORTAR": semexp})
        if "T1" in casos:
            r["t1o"] = corrida("t1o", "original", ["--render", "nativo"] + hd, False)
            r["t1s"] = corrida("t1s", "remastered", ["--render", "software"] + hd, False)
        if "T3" in casos:
            r["t3ref"] = corrida("t3ref", "remastered", wide)
        with ThreadPoolExecutor(3) as ex:
            list(ex.map(lambda c: c.rodar(), [exp] + list(r.values())))
        nomes = sorted(f for f in os.listdir(orig) if NOME.match(f)) if os.path.isdir(orig) else []
        check("exportacao: as texturas originais da rota", exp.rc == 0 and len(nomes) > 50 and resumo(exp)[3] == len(nomes),
              "rc %d, %d PNG, resumo %s" % (exp.rc, len(nomes), resumo(exp)))
        igual_estado(r["off"], exp, "exportacao (validacao) x produto")
        check("o produto ignora BS_TEXTURAS_EXPORTAR", r["off"].rc == 0 and not os.path.exists(semexp) and resumo(r["off"])[3] == 0,
              "rc %d, pasta %s, resumo %s" % (r["off"].rc, "criada" if os.path.exists(semexp) else "ausente", resumo(r["off"])))
        if not nomes:
            return 1
        mib = sum(int(NOME.match(f)[1]) * int(NOME.match(f)[2]) * 4 for f in nomes) / (1024.0 * 1024.0)
        # 2) os pacotes
        pk = os.path.join(base, "_pacotes")
        ident = os.path.join(pk, "identidade")
        t = pacote(ident, "identidade")
        for f in nomes:
            shutil.copy2(os.path.join(orig, f), os.path.join(t, f))
        x4 = os.path.join(pk, "x4")
        t = pacote(x4, "x4")
        for f in nomes:
            im = np.asarray(Image.open(os.path.join(orig, f)).convert("RGBA"))
            Image.fromarray(np.repeat(np.repeat(im, 4, axis=0), 4, axis=1)).save(os.path.join(t, f))
        rec = os.path.join(pk, "recuo")
        t = pacote(rec, "recuo")
        ruins = {}
        for i, f in enumerate(nomes):
            dst = os.path.join(t, f)
            if i % 2 == 0:
                shutil.copy2(os.path.join(orig, f), dst)
                continue
            w, h = map(int, NOME.match(f).groups()[:2])
            k = (i // 2) % 3
            if k == 0:  # outra proporcao: o dobro so na largura
                Image.new("RGBA", (2 * w, h), (255, 0, 255, 255)).save(dst)
                ruins[f[:-4]] = "fator inteiro"
            elif k == 1:  # cortado no meio
                b = open(os.path.join(orig, f), "rb").read()
                open(dst, "wb").write(b[:len(b) // 2])
                ruins[f[:-4]] = "ilegivel"
            else:  # nao e PNG
                open(dst, "wb").write(b"nao e um PNG")
                ruins[f[:-4]] = "ilegivel"
        open(os.path.join(t, "textura-sem-id.png"), "wb").write(open(os.path.join(orig, nomes[0]), "rb").read())
        open(os.path.join(t, "LEIA-ME.txt"), "w").write("pacote de teste")
        # 3) as corridas com os pacotes
        r2 = {}
        if "T2" in casos:
            r2["t2"] = corrida("t2", "remastered", nat + hd)
            moverPacote(r2["t2"], ident)
        if "T3" in casos:
            r2["t3"] = corrida("t3", "remastered", wide + hd)
            moverPacote(r2["t3"], x4)
        if "T4" in casos:
            r2["t4"] = corrida("t4", "remastered", nat + hd)
            moverPacote(r2["t4"], rec)
        with ThreadPoolExecutor(3) as ex:
            list(ex.map(lambda c: c.rodar(), r2.values()))

        if "T1" in casos:
            check("T1 Original com --texturas-hd ligado recusada", r["t1o"].rc == 2, "rc %d" % r["t1o"].rc)
            check("T1 Remastered no software com --texturas-hd ligado recusada", r["t1s"].rc == 2, "rc %d" % r["t1s"].rc)
        off = r["off"]
        check("sem pacote: nenhuma substituicao", resumo(off)[:3] == (0, 0, 0) and "[texturas-hd]" not in off.log, str(resumo(off)))
        if "T2" in casos:
            c = r2["t2"]
            igual_estado(off, c, "T2 identidade")
            s, p = resumo(c), provedor(c)
            print("  T2: resumo %s, provedor %s" % (s, p))
            check("T2 o pacote achado", "[texturas-hd] pacote identidade 0 (prioridade 10): %d texturas" % len(nomes) in c.log and
                  "[render] texturas HD: ligadas" in c.log)
            check("T2 substituidas, nenhuma recusada", s[0] > 0 and s[1] == 0 and p[0] > 0 and p[3] == 0 and p[2] == 1)
            check("T2 adiadas pelo limite do quadro e pedidas de novo", s[2] > 0 and p[4] > 0)
            check("T2 todas as da rota carregadas, a memoria e a dos texels", p[0] == len(nomes) and abs(p[1] - mib) <= 0.05,
                  "%d de %d, %.2f MiB (texels: %.2f MiB)" % (p[0], len(nomes), p[1], mib))
            iguais(off, c, "T2 identidade x sem pacote")
        if "T3" in casos:
            c, ref = r2["t3"], r["t3ref"]
            igual_estado(ref, c, "T3 x4 (16:9, anisotropica, agua alta)")
            s, p = resumo(c), provedor(c)
            print("  T3: resumo %s, provedor %s; GPU por quadro %.2f ms sem pacote, %.2f ms com o x4" % (s, p, gpu_ms(ref), gpu_ms(c)))
            check("T3 fator 4 carregado, nenhuma recusada", s[0] > 0 and p[2] == 4 and p[3] == 0)
            # a memoria: no maximo 16x a de todas as originais (as que a rota larga pede sao as da rota 4:3 ou menos)
            check("T3 a memoria das texturas (fator 4)", 0 < p[1] <= 16 * mib + 0.1 and p[0] <= len(nomes),
                  "%d carregadas, %.1f MiB (todas no fator 4: %.1f MiB)" % (p[0], p[1], 16 * mib))
            check("T3 a agua nos mesmos desenhos (o material da original)", agua(c) == agua(ref) and agua(ref)[1] > 0,
                  "%s x %s" % (agua(c), agua(ref)))
            d = [c.img("v%s_hd.bmp" % v) for v in VBS_HD]
            e = [ref.img("v%s_hd.bmp" % v) for v in VBS_HD]
            ok = all(x is not None for x in d + e)
            check("T3 a imagem muda com o x4", ok and any((x != y).any() for x, y in zip(d, e)))
        if "T4" in casos:
            c = r2["t4"]
            igual_estado(off, c, "T4 recuo")
            s, p = resumo(c), provedor(c)
            recusadas = re.findall(r"\[texturas-hd\] recusada (\S+): (.*)", c.log)
            print("  T4: resumo %s, provedor %s; %d recusas (%d listadas)" % (s, p, p[3], len(recusadas)))
            check("T4 as ruins recusadas, todas e so elas", p[3] == len(ruins) and s[1] == 0, "%d de %d" % (p[3], len(ruins)))
            motivos = all(ident_ in ruins and ruins[ident_] in why for ident_, why in recusadas)
            check("T4 cada recusa com o motivo certo", len(recusadas) > 0 and motivos)
            check("T4 os nomes ignorados avisados", "pacote recuo: 1 PNG com nome que nao e uma textura (textura-sem-id.png)" in c.log)
            check("T4 as validas carregadas", p[0] == len(nomes) - len(ruins) and s[0] > 0, "%d" % p[0])
            iguais(off, c, "T4 recuo x sem pacote")
    finally:
        print("%d falha(s) (%s)" % (len(falhas), base))
        if not a.manter and not falhas:
            shutil.rmtree(base, ignore_errors=True)
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
