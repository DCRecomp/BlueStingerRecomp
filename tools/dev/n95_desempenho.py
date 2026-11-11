"""N9.5 fase 5C: a matriz de desempenho pela telemetria do produto (BS_TELEMETRIA, native/platform/telemetry.h).

Cada execucao numa pasta propria dentro de --saida (copia do exe, instalacao.toml da Remastered, graficos.toml do caso e,
quando a rota pede, uma copia do cartao de teste), no modo espectador (o roteiro conduz: nenhuma entrada global), com
BS_TELEMETRIA para o arquivo do caso e BS_TELEMETRIA_DE no inicio do trecho medido. So o processo do caso e encerrado.

Configuracoes: 4:3 e 16:9 em 480 (original), 1080, 1440 e 2160 linhas. Trechos (VBlank do levantamento da fase 4,
tmp/sessao-20261109/fase4/l1/capturas.tsv):
  corredor   passo_load_slot1, RCG02 (o save), VBlanks 3600-4700;
  cais       passo_heliporto, RCG00 (o cais), VBlanks 29340-31320;
  exterior   passo_heliporto, RCG03 (o exterior, o monstro, o combate e o GAME OVER), VBlanks 43560-55980;
  combate    passo_combate, o fim (os socos no monstro), VBlanks 46000-52000.
Com --janela, a janela real (1280x720, V-Sync ligado, o padrao do produto): quadros apresentados, frametime e a CPU da thread
da janela; sem janela (--headless), o mesmo render sem apresentacao (fps e frametime ficam "na").

Saida: <saida>/matriz.tsv (uma linha por caso e trecho: medias das linhas por segundo do trecho e os picos) e a pasta de
cada caso (telemetria.csv, saida.txt).

  python tools/dev/n95_desempenho.py --exe build/msvc-release/app/bluestinger.exe --data <data> --cartao <cartao>
         --saida tmp/<pasta> [--janela] [--trechos corredor,cais,exterior] [--configs 4:3@480,16:9@2160] [--gpu <nome>]
N9.6 fase 1: uma configuracao com o sufixo +ao (ex.: 16:9@1080+ao) liga a oclusao ambiente na qualidade media (meia
resolucao); +ao-alta (o alvo inteiro, a primeira versao), +ao-media e +ao-baixa escolhem a qualidade. Fase 3: +luz liga a
luz das fontes do catalogo (combinavel: 16:9@1080+ao-media+luz).
"""
import argparse
import csv
import math
import os
import shutil
import subprocess
import sys
import time

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(AQUI))
ROT = os.path.join(RAIZ, "tools", "dist", "roteiros")
# rota -> (roteiro, usa o cartao, VBlanks ate o fim do ultimo trecho); trecho -> (rota, de, ate)
ROTAS = {"load": ("passo_load_slot1.txt", True, 4700), "heliporto": ("passo_heliporto.txt", False, 56000),
         "combate": ("passo_combate.txt", False, 52000)}
TRECHOS = {"corredor": ("load", 3600, 4700), "cais": ("heliporto", 29340, 31320), "exterior": ("heliporto", 43560, 55980),
           "combate": ("combate", 46000, 52000)}
CONFIGS = ["4:3@480", "16:9@480", "4:3@1080", "16:9@1080", "4:3@1440", "16:9@1440", "4:3@2160", "16:9@2160"]
RES = {480: "original", 1080: "1080p", 1440: "1440p", 2160: "2160p"}
GRAFICOS = """preset = "personalizado"
[imagem]
resolucao = "{res}"
[tela]
modo = "janela"
largura = 1280
altura = 720
proporcao = "{prop}"
renderizador = "vulkan"
"""


def ler(csvp):
    """As linhas por segundo e o resumo (# resumo) da telemetria."""
    linhas, resumo = [], []
    if not os.path.exists(csvp):
        return linhas, resumo
    cab = None
    for l in open(csvp, encoding="utf-8").read().splitlines():
        if l.startswith("# resumo"):
            resumo.append(l[2:])
        elif l.startswith("#") or not l.strip():
            continue
        elif cab is None:
            cab = l.split(",")
        else:
            linhas.append(dict(zip(cab, l.split(","))))
    return linhas, resumo


def f(x):
    try:
        v = float(x)
        return v if math.isfinite(v) else None
    except (TypeError, ValueError):
        return None


def resumir(linhas, de, ate, inicio):
    """O trecho [de, ate] das linhas (o VBlank acumulado a partir de `inicio`, o BS_TELEMETRIA_DE)."""
    vb, sel = inicio, []
    for l in linhas:
        n = int(l.get("vblanks", "0") or 0)
        if vb >= de and vb + n <= ate + 60:
            sel.append(l)
        vb += n
    out = {"segundos": len(sel)}

    def media(k, peso=None):
        xs = [(f(l[k]), f(l[peso]) if peso else 1.0) for l in sel if f(l.get(k)) is not None]
        xs = [(v, w) for v, w in xs if w]
        return sum(v * w for v, w in xs) / sum(w for _, w in xs) if xs else None

    def pico(k):
        xs = [f(l[k]) for l in sel if f(l.get(k)) is not None]
        return max(xs) if xs else None

    for k in ("fps", "ft_p50_ms", "acima_16_7_pct", "acima_25_pct", "cpu_jogo_pct", "cpu_janela_pct"):
        out[k] = media(k, "quadros" if k.startswith("ft") or k.startswith("acima") else None)
    for k in ("ft_p95_ms", "ft_p99_ms", "ft_max_ms"):
        out[k + "_pico"] = pico(k)
        out[k + "_medio"] = media(k)
    for k in ("gpu_envios_ms", "gpu_raster_ms", "gpu_resolve_ms", "gpu_saida_ms", "gpu_total_ms"):
        out[k] = media(k, "renders_gpu")
    out["gpu_total_ms_pico"] = pico("gpu_total_ms")
    mems = [k for k in (sel[0].keys() if sel else []) if k.startswith("mem_") or k.startswith("vram_")]
    for k in mems:
        out[k + "_pico"] = pico(k)
    out["vblanks_por_s"] = media("vblanks")
    return out


def rodar(a, cfg, rota, de):
    # N9.6 fase 1: o sufixo "+ao[-qualidade]" liga a oclusao ambiente (graficos.toml na janela, --ssao ligado sem janela);
    # fase 3: "+luz" liga a luz das fontes do catalogo ([imagem] iluminacao, --iluminacao ligado); os dois se combinam
    base, *sufs = cfg.split("+")
    ao = luz = False
    qual = "media"
    for suf in sufs:
        if suf == "ao" or suf.startswith("ao-"):
            ao, qual = True, suf[3:] if suf.startswith("ao-") else "media"
        elif suf == "luz":
            luz = True
        else:
            raise SystemExit("configuracao invalida: %s" % cfg)
    if qual not in ("baixa", "media", "alta"):
        raise SystemExit("configuracao invalida: %s" % cfg)
    prop, alt = base.split("@")
    alt = int(alt)
    roteiro, cartao, vblanks = ROTAS[rota]
    nome = "%s_%s_%d%s%s" % (rota, prop.replace(":", "x"), alt, "_ao_" + qual if ao else "", "_luz" if luz else "")
    d = os.path.join(a.saida, nome)
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    exe = os.path.join(d, os.path.basename(a.exe))
    shutil.copy2(a.exe, exe)
    open(os.path.join(d, "instalacao.toml"), "w").write('[edicao]\nperfil = "remastered"\n')
    graf = GRAFICOS.format(res=RES[alt], prop=prop)
    if luz:  # a chave fica na secao [imagem], a primeira do modelo
        graf = graf.replace("[imagem]\n", "[imagem]\niluminacao = true\n", 1)
    open(os.path.join(d, "graficos.toml"), "w").write(graf + ("[efeitos]\nssao = true\nssao_qualidade = \"%s\"\n" % qual
                                                              if ao else ""))
    saves = os.path.join(d, "saves")
    if cartao:
        shutil.copytree(a.cartao, saves)
    else:
        os.makedirs(saves)
    tele = os.path.join(d, "telemetria.csv")
    env = {k: v for k, v in os.environ.items() if not k.startswith("BS_")}
    env.update({"BS_RELOGIO_FIXO": "1700000000", "BS_INPUT_SCRIPT": os.path.join(ROT, roteiro), "BS_TELEMETRIA": tele,
                "BS_TELEMETRIA_DE": str(de), "BS_TELEMETRIA_ATE": str(vblanks)})
    if a.gpu:
        env["BS_VULKAN_GPU"] = a.gpu
    cmd = [exe, "--data", a.data, "--saves-pasta", saves]
    if a.janela:
        cmd += ["--apresentacao", "vulkan"]
    else:
        cmd += ["--render", "nativo", "--proporcao", prop, "--headless", str(vblanks)]
        if alt != 480:
            cmd += ["--resolucao-interna", str(alt)]
        if ao:
            cmd += ["--ssao", "ligado", "--ssao-qualidade", qual]
        if luz:
            cmd += ["--iluminacao", "ligado"]
    t0 = time.time()
    with open(os.path.join(d, "saida.txt"), "w", encoding="utf-8") as out:
        p = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env=env, cwd=d)
        limite = t0 + (vblanks / 60.0 + 120 if a.janela else 3600)
        completo = False
        while p.poll() is None and time.time() < limite:
            # o jogo grava o resumo no BS_TELEMETRIA_ATE (revisao Codex 20261110-014042: so depois dele a janela e encerrada)
            if a.janela and any("memoria privada" in x for x in ler(tele)[1]):
                completo = True
                break
            time.sleep(2)
        if p.poll() is None:  # so o processo deste caso
            if not completo:
                print("    [aviso] %s: o resumo nao saiu ate o limite de tempo: caso incompleto" % nome, flush=True)
            p.terminate()
            try:
                p.wait(20)
            except subprocess.TimeoutExpired:
                p.kill()
    completo = any("memoria privada" in x for x in ler(tele)[1])  # o resumo inteiro: o trecho chegou ao fim
    return d, tele, "completo" if completo else "incompleto (rc %s)" % p.returncode, time.time() - t0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--cartao", required=True)
    ap.add_argument("--saida", required=True)
    ap.add_argument("--janela", action="store_true")
    ap.add_argument("--trechos", default="corredor,cais,exterior")
    ap.add_argument("--configs", default=",".join(CONFIGS))
    ap.add_argument("--gpu", help="BS_VULKAN_GPU (parte do nome do dispositivo)")
    a = ap.parse_args()
    a.exe, a.data, a.saida = os.path.abspath(a.exe), os.path.abspath(a.data), os.path.abspath(a.saida)
    os.makedirs(a.saida, exist_ok=True)
    trechos = a.trechos.split(",")
    rotas = []
    for t in trechos:  # uma execucao por rota cobre os trechos dela (a medicao comeca no primeiro)
        r = TRECHOS[t][0]
        if r not in [x for x, _ in rotas]:
            rotas.append((r, min(TRECHOS[u][1] for u in trechos if TRECHOS[u][0] == r)))
    resultado = []
    for cfg in a.configs.split(","):
        for rota, de in rotas:
            d, tele, rc, secs = rodar(a, cfg, rota, de)
            linhas, resumo = ler(tele)
            for t in trechos:
                if TRECHOS[t][0] != rota:
                    continue
                r = resumir(linhas, TRECHOS[t][1], TRECHOS[t][2], de)
                r.update(config=cfg, trecho=t, modo="janela" if a.janela else "sem janela", rc=rc, duracao_s=round(secs),
                         dispositivo=next((x.split("dispositivo ")[1].split(";")[0] for x in resumo if "dispositivo" in x), "na"))
                resultado.append(r)
                print("[%s %s] %s rc=%s %ds: %d s medidos; GPU %s ms; fps %s; p99 pico %s ms; alvos %s MiB" % (
                    cfg, t, r["modo"], rc, secs, r["segundos"], "na" if r["gpu_total_ms"] is None else "%.2f" % r["gpu_total_ms"],
                    "na" if r["fps"] is None else "%.1f" % r["fps"], r.get("ft_p99_ms_pico"), r.get("mem_alvos_mib_pico")), flush=True)
    chaves = []
    for r in resultado:
        for k in r:
            if k not in chaves:
                chaves.append(k)
    with open(os.path.join(a.saida, "matriz.tsv"), "w", encoding="utf-8", newline="") as fo:
        w = csv.writer(fo, delimiter="\t", lineterminator="\n")
        w.writerow(chaves)
        for r in resultado:
            w.writerow(["na" if r.get(k) is None else ("%.3f" % r[k] if isinstance(r.get(k), float) else r[k]) for k in chaves])
    print("matriz: %s" % os.path.join(a.saida, "matriz.tsv"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
