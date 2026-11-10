"""N9.5 fase 4 (docs/N9-5-AUDITORIA.md secao 15): o levantamento das bordas do widescreen por sala.

  python tools/dev/n95_bordas.py --exe <bluestinger.exe do produto> --data <pasta data> --cartao <pasta de saves sintetica>
                                 --saida <pasta> [--rotas heliporto,load] [--cada 180]

Roda o produto sem janela na Remastered 16:9 (Vulkan, o quadro largo da fase 1) em cada rota por passo, capturando a imagem
larga (a do slot, 854 colunas) a cada --cada VBlanks. A sala de cada captura vem do registro de arquivos (BS_FILE_LOG: o
ultimo R*.BIN lido antes do VBlank). Em cada captura, a fracao de vazio (pixels pretos, o mesmo criterio do n95_headless:
o fundo de limpeza fora de uma sala e preto) nas faixas novas (x < 107 e x >= 747) e no centro (o 4:3).

Saida em <saida>/: capturas.tsv (uma linha por captura), salas.tsv (por sala: capturas, vazio medio e maximo de cada faixa e
do centro, a captura com mais vazio) e miniaturas/ (a pior captura de cada sala, reduzida, para a revisao).
"""
import argparse
import collections
import csv
import os
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(AQUI))
ROT = os.path.join(RAIZ, "tools", "dist", "roteiros")
# rota -> (roteiro, VBlanks, usa o cartao): o heliporto vai do NEW GAME ao GAME OVER (cais, C-GATE, RCG02, exterior, menu,
# combate); o LOAD comeca no RCG02 pelo save
ROTAS = {
    "heliporto": ("passo_heliporto.txt", 56000, False),
    "load": ("passo_load_slot1.txt", 4700, True),
    "combate": ("passo_combate.txt", 52000, False),
}
FAIXA = 107  # as colunas novas de cada lado no quadro de 854


def vazio(img):
    return float((img.max(axis=2) <= 10).mean()) if img.size else 0.0


def salas_por_vblank(log):
    """[(vblank, sala)] na ordem, do registro de arquivos (o R*.BIN lido com deslocamento 0)."""
    out = []
    if not os.path.exists(log):
        return out
    for l in open(log, encoding="utf-8", errors="replace").read().splitlines()[1:]:
        c = l.split("\t")
        if len(c) < 6 or c[3] != "dado" or c[5] != "0":
            continue
        base = os.path.basename(c[4].replace("\\", "/"))
        if base.startswith("R") and base.upper().endswith(".BIN"):
            out.append((int(c[1]), base[:-4]))
    return out


def sala_em(trocas, vb):
    s = "-"
    for v, nome in trocas:
        if v > vb:
            break
        s = nome
    return s


def rodar(a, rota):
    roteiro, vblanks, cartao = ROTAS[rota]
    d = os.path.join(a.saida, rota)
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    exe = os.path.join(d, os.path.basename(a.exe))
    shutil.copy2(a.exe, exe)
    open(os.path.join(d, "instalacao.toml"), "w").write('[edicao]\nperfil = "remastered"\n')
    saves = os.path.join(d, "saves")
    shutil.copytree(a.cartao, saves)
    cap = os.path.join(d, "cap")
    env = dict(os.environ)
    for k in ("BS_INPUT_SCRIPT", "BS_GRAVAR", "BS_ASSISTIDO", "BS_RENDER", "BS_TELA_TESTE"):
        env.pop(k, None)
    vbs = list(range(a.cada, vblanks, a.cada))
    env.update({"BS_RELOGIO_FIXO": "1700000000", "BS_INPUT_SCRIPT": os.path.join(ROT, roteiro),
                "BS_FILE_LOG": os.path.join(d, "arquivos.tsv"),
                "BS_CAPTURA_VBLANKS": ",".join("%d-%d" % (v, v) for v in vbs)})
    cmd = [exe, "--data", os.path.abspath(a.data), "--render", "nativo", "--proporcao", "16:9", "--headless", str(vblanks),
           "--dump-every", "1", "--capture", cap]
    if cartao:
        cmd[3:3] = ["--saves-pasta", saves]
    print("[%s] %d VBlanks, captura a cada %d" % (rota, vblanks, a.cada), flush=True)
    with open(os.path.join(d, "saida.txt"), "w", encoding="utf-8") as out:
        rc = subprocess.run(cmd, stdout=out, stderr=subprocess.STDOUT, env=env, cwd=d, timeout=7200).returncode
    print("[%s] rc=%d" % (rota, rc), flush=True)
    trocas = salas_por_vblank(os.path.join(d, "arquivos.tsv"))
    linhas = []
    for v in vbs:
        p = os.path.join(cap, "v%06d_hd.bmp" % v)
        if not os.path.exists(p):
            continue
        img = np.asarray(Image.open(p).convert("RGB"), dtype=np.uint8)
        w = img.shape[1]
        if w != 854:
            continue  # sem quadro largo neste VBlank (filme, render de outro tamanho)
        linhas.append(dict(rota=rota, vblank=v, sala=sala_em(trocas, v), esq=vazio(img[:, :FAIXA]),
                           dir=vazio(img[:, w - FAIXA:]), centro=vazio(img[:, FAIXA:w - FAIXA]), arquivo=p))
    return rc, linhas


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--cartao", required=True)
    ap.add_argument("--saida", required=True)
    ap.add_argument("--rotas", default="heliporto,load")
    ap.add_argument("--cada", type=int, default=180)
    a = ap.parse_args()
    os.makedirs(a.saida, exist_ok=True)
    todas = []
    for rota in a.rotas.split(","):
        rc, l = rodar(a, rota)
        if rc != 0:
            sys.exit("a rota %s terminou com %d" % (rota, rc))
        todas += l
    with open(os.path.join(a.saida, "capturas.tsv"), "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["rota", "vblank", "sala", "vazio_esq", "vazio_dir", "vazio_centro", "arquivo"])
        for x in todas:
            w.writerow([x["rota"], x["vblank"], x["sala"], "%.3f" % x["esq"], "%.3f" % x["dir"], "%.3f" % x["centro"], x["arquivo"]])
    por = collections.defaultdict(list)
    for x in todas:
        por[x["sala"]].append(x)
    mini = os.path.join(a.saida, "miniaturas")
    os.makedirs(mini, exist_ok=True)
    with open(os.path.join(a.saida, "salas.tsv"), "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["sala", "capturas", "esq_medio", "esq_max", "dir_medio", "dir_max", "centro_medio", "pior_captura"])
        for sala, xs in sorted(por.items()):
            pior = max(xs, key=lambda x: max(x["esq"], x["dir"]) - x["centro"])
            img = Image.open(pior["arquivo"]).convert("RGB")
            nome = "%s_%s_v%06d.png" % (sala, pior["rota"], pior["vblank"])
            img.resize((img.width // 2, img.height // 2)).save(os.path.join(mini, nome))
            w.writerow([sala, len(xs), "%.3f" % np.mean([x["esq"] for x in xs]), "%.3f" % max(x["esq"] for x in xs),
                        "%.3f" % np.mean([x["dir"] for x in xs]), "%.3f" % max(x["dir"] for x in xs),
                        "%.3f" % np.mean([x["centro"] for x in xs]), nome])
            print("%-10s %3d capturas | esq medio %.2f max %.2f | dir medio %.2f max %.2f | centro %.2f | %s" % (
                sala, len(xs), np.mean([x["esq"] for x in xs]), max(x["esq"] for x in xs),
                np.mean([x["dir"] for x in xs]), max(x["dir"] for x in xs), np.mean([x["centro"] for x in xs]), nome))


if __name__ == "__main__":
    main()
