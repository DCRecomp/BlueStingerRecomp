"""Estabilizacao pos-N9.7 (docs/N9-7-AGUA.md secao 11): o reprodutor controlado da perda do dispositivo Vulkan na janela. A mesma cena (o inicio ate o menu
inicial, ~40 s de janela), o preset Alto (1440p interno), o mesmo exe e as mesmas condicoes; so muda o pos-processamento:
  A sem correcao de cor e sem nitidez; B so a correcao de cor; C so a nitidez; D as duas (o Alto como e).
As configuracoes se alternam (A, B, C, D, A, ...) para que uma deriva do estado da maquina nao caia so numa delas. Toda
execucao vai para o TSV, com ou sem perda. Uso:
  python tools/dev/n9_vulkan_perda.py <exe> <saida> <n> [env=valor ...]
Os dados: BS_PERDA_DATA (padrao: a Preview de tmp/sessao-20261109). Abre a janela do jogo (avisar antes); o controle virtual
so manda "sair". Com BS_VK_DIAG=1 (exe de validacao) o diagnostico do vk_internal.h; com BS_VK_FILA_UNICA=1, uma fila so.
Resultado de 2026-11-12 (RTX 5080, driver 621.01): antes da correcao ab28c5a, A 0/10, B 3/10, C 4/10, D 4/10; depois, 0/10
em cada uma."""
import csv
import os
import re
import shutil
import subprocess
import sys
import time

exe, base, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
extra = dict(a.split("=", 1) for a in sys.argv[4:])
DATA = os.environ.get("BS_PERDA_DATA", r"C:\Source\recomp-BStinger\tmp\sessao-20261109\preview\BlueStinger-Preview\data")
TELA = '[tela]\nmodo = "janela"\nlargura = 1280\naltura = 720\nrenderizador = "vulkan"\n'
CFG = {
    "A": '[efeitos]\ncor = false\ncontraste = 1\nsaturacao = 1\nnitidez = 0\n',
    "B": '[efeitos]\nnitidez = 0\n',
    "C": '[efeitos]\ncor = false\ncontraste = 1\nsaturacao = 1\n',
    "D": '',
}
os.makedirs(base, exist_ok=True)
tsv = os.path.join(base, "matriz.tsv")
novo = not os.path.exists(tsv)
with open(tsv, "a", newline="", encoding="utf-8") as fo:
    w = csv.writer(fo, delimiter="\t")
    if novo:
        w.writerow(["rodada", "config", "rc", "perda", "render", "chamada", "recriado", "escala", "passes_pos", "primeira_falha", "inicio"])
    for i in range(n):
        for c in "ABCD":
            d = os.path.join(base, "%s-%02d" % (c, i))
            if os.path.exists(d):
                shutil.rmtree(d)
            os.makedirs(os.path.join(d, "saves"))
            shutil.copy2(exe, os.path.join(d, "bluestinger.exe"))
            open(os.path.join(d, "instalacao.toml"), "w").write('[edicao]\nperfil = "remastered"\n')
            g = 'preset = "alta"\n' + (CFG[c].replace("[efeitos]\n", "[efeitos]\n") if CFG[c] else "") + TELA
            open(os.path.join(d, "graficos.toml"), "w").write(g)
            open(os.path.join(d, "pad.txt"), "w").write("0 conectar\n40000 sair\n")
            env = {k: v for k, v in os.environ.items() if not k.startswith("BS_")}
            env.update({"BS_CONTROLE_VIRTUAL": os.path.join(d, "pad.txt"), "BS_RELOGIO_FIXO": "1700000000"})
            env.update(extra)
            t0 = time.strftime("%H:%M:%S")
            with open(os.path.join(d, "jogo.txt"), "w", encoding="utf-8") as out:
                p = subprocess.Popen([os.path.join(d, "bluestinger.exe"), "--data", DATA, "--saves-pasta", os.path.join(d, "saves"),
                                      "--apresentacao", "vulkan"], stdout=out, stderr=subprocess.STDOUT, env=env, cwd=d)
                try:
                    rc = p.wait(240)
                except subprocess.TimeoutExpired:
                    p.kill()
                    rc = -1
            t = open(os.path.join(d, "jogo.txt"), encoding="utf-8", errors="replace").read()
            m = re.search(r"falha no render (\d+) \(([^(]*)\(VkResult (-?\d+)\)", t)
            rec = re.search(r"dispositivo recriado (\d+) vez", t)
            esc = re.search(r"resolucao interna: escala ([\d.]+)", t)
            pf = re.search(r"\[vk-diag\] PRIMEIRA FALHA detectada em ([^\n]*)", t)
            npos = re.findall(r"passes de pos (\d+)", t)
            w.writerow([i, c, rc, 1 if m else 0, m.group(1) if m else "", (m.group(2).strip() + " " + m.group(3)) if m else "",
                        rec.group(1) if rec else "", esc.group(1) if esc else "", npos[-1] if npos else "", pf.group(1) if pf else "", t0])
            fo.flush()
            print("%d %s rc %s %s" % (i, c, rc, ("PERDA render %s" % m.group(1)) if m else "ok"), flush=True)
