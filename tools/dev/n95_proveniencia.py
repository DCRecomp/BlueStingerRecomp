"""N9.5 fase 2 (so validacao): a proveniencia dos desenhos contra o oraculo diferencial da fase 0.

  python tools/dev/n95_proveniencia.py capturar --exe <bluestinger.exe de validacao> --data <pasta data>
                                       --cartao <pasta de saves sintetica> --saida <pasta> [--cenas a,b]
  python tools/dev/n95_proveniencia.py cruzar <pasta> --ir-tela <bs-ir-tela.exe> [--limite 8C158000]

capturar: para cada rota, duas sessoes do gameplay assistido sem janela (uma por vez), a 4:3 e a da projecao larga
(BS_TELA_TESTE=854,427, so validacao), com o mesmo roteiro por passo; em cada passo marcado, o comando `ir` grava a IR
do ultimo render e a proveniencia dos desenhos (<nome>.ir.origem.tsv: a cadeia de chamadas do guest no momento em que
o parametro chegou ao TA, da funcao que escreveu para fora).

cruzar: `bs-ir-tela comparar` classifica cada desenho (cena = andou +107 em x; tela = nao andou; so_4x3/so_larga) e a
proveniencia de cada um entra ao lado. Saida em <pasta>/cruzado/: um TSV por cena (o comparar com as colunas da
origem), rotinas.tsv (cada cadeia de chamadas com a contagem por classe) e resumo.txt. A "rotina do jogo" de um desenho
e a primeira entrada da cadeia abaixo de --limite (o limite e so da apresentacao; a cadeia inteira fica no TSV).
"""
import argparse
import collections
import csv
import os
import shutil
import subprocess
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(AQUI))
ROT = os.path.join(RAIZ, "tools", "dist", "roteiros")
DX = 107

# as cenas da fase 0 (docs/N9-5-AUDITORIA.md secao 11.3): rota, cartao de saves (LOAD) e os passos
CENAS = {
    "load": dict(roteiro="passo_load_slot1.txt", cartao=True, passos=[1855, 2300]),
    "combate": dict(roteiro="passo_combate.txt", cartao=False, passos=[2000, 6000, 10000, 11660]),
    # N9.5 fase 3: o HUD em mais situacoes (marcos anotados nos roteiros): exterior RCG03, menu do inventario, item
    # encontrado (monstro se levantando), menu de usar item, combate, dano e GAME OVER; e as cenas de historia do C-GATE
    "heliporto": dict(roteiro="passo_heliporto.txt", cartao=False,
                      passos=[7900, 8840, 11240, 11345, 11500, 11800, 12100, 12190]),
    "cgate": dict(roteiro="passo_cgate.txt", cartao=False, passos=[4000, 7900]),
}


def assistido(*args, env=None):
    cmd = [sys.executable, os.path.join(AQUI, "assistido.py")] + list(args)
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        sys.exit("assistido %s falhou (%d):\n%s%s" % (args[0], r.returncode, r.stdout[-2000:], r.stderr[-2000:]))
    return r.stdout


def capturar(a):
    os.makedirs(a.saida, exist_ok=True)
    cenas = a.cenas.split(",") if a.cenas else list(CENAS)
    for nome in cenas:
        c = CENAS[nome]
        for variante, tela in (("4x3", None), ("larga", "854,427")):
            d = os.path.join(a.saida, "%s_%s" % (nome, variante))
            if os.path.isdir(d):
                shutil.rmtree(d)
            os.makedirs(d)
            env = dict(os.environ)
            for k in ("BS_TELA_TESTE", "BS_INPUT_SCRIPT", "BS_GRAVAR", "BS_TELA_LOGICA43"):
                env.pop(k, None)
            if tela:
                env["BS_TELA_TESTE"] = tela
            if a.escritor_dma:  # N9.5 fase 3A: a cadeia de quem escreveu o buffer do DMA (sombra da RAM)
                env["BS_PROVENIENCIA_DMA"] = "1"
            else:
                env.pop("BS_PROVENIENCIA_DMA", None)
            extra = ""
            if c["cartao"]:
                saves = os.path.join(d, "saves")
                shutil.copytree(a.cartao, saves)
                extra = "--saves nativo --saves-pasta %s" % saves
            print("[%s %s] iniciando" % (nome, variante), flush=True)
            assistido("iniciar", d, "--exe", a.exe, "--data", a.data, "--extra", extra, env=env)
            assistido("cmd", d, "roteiro " + os.path.join(ROT, c["roteiro"]), env=env)
            for p in c["passos"]:
                out = assistido("cmd", d, "ate_passo %d" % p, "ir p%d" % p, env=env)
                linha = [l for l in out.splitlines() if l.startswith("ok ir")]
                print("[%s %s] passo %d: %s" % (nome, variante, p, linha[-1] if linha else out.strip()[-200:]), flush=True)
            assistido("cmd", d, "fim", env=env)


def ler_tsv(p):
    with open(p, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def rotina(cadeia, limite):
    if cadeia in ("", "-"):
        return "-"
    for f in cadeia.split(">"):
        if int(f, 16) < limite:
            return f
    return "(so biblioteca)"


def cruzar(a):
    limite = int(a.limite, 16)
    out = os.path.join(a.pasta, "cruzado")
    os.makedirs(out, exist_ok=True)
    por_cadeia = collections.defaultdict(collections.Counter)  # (escritor, rotina, cadeia) -> classe -> n
    marca = collections.Counter()  # (marca do jogo, classe do oraculo) -> n: a marca (kamui/draw_class) contra o oraculo
    divergentes = 0
    exemplos = {}
    resumo = []
    for nome, c in CENAS.items():
        da, db = os.path.join(a.pasta, nome + "_4x3"), os.path.join(a.pasta, nome + "_larga")
        if not os.path.isdir(da) or not os.path.isdir(db):
            continue
        for p in c["passos"]:
            ia, ib = os.path.join(da, "p%d.ir" % p), os.path.join(db, "p%d.ir" % p)
            oa, ob = ia + ".origem.tsv", ib + ".origem.tsv"
            if not all(os.path.isfile(x) for x in (ia, ib, oa, ob)):
                resumo.append("%s passo %d: faltam arquivos (IR ou proveniencia)" % (nome, p))
                continue
            comp = os.path.join(out, "%s_p%d.comparar.tsv" % (nome, p))
            r = subprocess.run([a.ir_tela, "comparar", ia, ib, str(DX), comp], capture_output=True, text=True)
            if r.returncode != 0:
                sys.exit("bs-ir-tela comparar falhou: " + r.stderr)
            orig_a, orig_b = ler_tsv(oa), ler_tsv(ob)
            linhas = ler_tsv(comp)
            classes = collections.Counter()
            difcad = 0
            with open(os.path.join(out, "%s_p%d.tsv" % (nome, p)), "w", encoding="utf-8", newline="") as f:
                w = csv.writer(f, delimiter="\t", lineterminator="\n")
                sem = [k for k in orig_a[0].keys() if k not in ("desenho", "lista", "tipo", "param", "global", "dma", "cadeia",
                                                                "cadeia_global")] if orig_a else []
                w.writerow(list(linhas[0].keys()) + ["tipo", "dma", "escritor", "rotina", "cadeia", "cadeia_global"] + sem)
                for l in linhas:
                    i, j = int(l["desenho_4x3"]), int(l["desenho_larga"])
                    o = orig_a[i] if i >= 0 else orig_b[j]
                    # a proveniencia dos dois lados de um par tem de ser a mesma (o mesmo codigo enviou o desenho);
                    # revisao Codex 20261108-201953: um par que diverge e erro da captura e sai das estatisticas
                    if i >= 0 and j >= 0 and any(orig_a[i].get(k) != orig_b[j].get(k)
                                                 for k in ("cadeia", "cadeia_global", "dma", "tipo", "marca")):
                        difcad += 1
                        continue
                    cad = o["cadeia"]
                    esc = cad.split(">")[0] if cad not in ("", "-") else "-"
                    rot = rotina(cad, limite)
                    w.writerow(list(l.values()) + [o["tipo"], o["dma"], esc, rot, cad, o["cadeia_global"]] + [o.get(k, "") for k in sem])
                    cls = l["classe"]
                    classes[cls] += 1
                    marca[(o.get("marca", "-"), cls)] += 1
                    k = (esc, rot, cad, o["dma"], o["tipo"])
                    por_cadeia[k][cls] += 1
                    exemplos.setdefault(k, "%s p%d desenho %s" % (nome, p, l["desenho_4x3"]))
            resumo.append("%s passo %d: %s; pares com proveniencia diferente nos dois lados (fora das contagens): %d" % (
                nome, p, ", ".join("%s %d" % kv for kv in sorted(classes.items())), difcad))
            divergentes += difcad
    with open(os.path.join(out, "rotinas.tsv"), "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["escritor", "rotina", "dma", "tipo", "cena", "tela", "so_4x3", "so_larga", "pura", "cadeia", "exemplo"])
        for k, cnt in sorted(por_cadeia.items(), key=lambda kv: -sum(kv[1].values())):
            esc, rot, cad, dma, tipo = k
            tot = sum(cnt.values())
            pura = "sim" if max(cnt.get("cena", 0), cnt.get("tela", 0)) == tot - cnt.get("so_4x3", 0) - cnt.get("so_larga", 0) else "nao"
            w.writerow([esc, rot, dma, tipo, cnt.get("cena", 0), cnt.get("tela", 0), cnt.get("so_4x3", 0),
                        cnt.get("so_larga", 0), pura, cad, exemplos[k]])
    # quao bem separa cada nivel: o escritor (fn[0]), a rotina do jogo e a cadeia inteira
    for nivel, idx in (("escritor", 0), ("rotina", 1), ("cadeia", 2)):
        g = collections.defaultdict(collections.Counter)
        for k, cnt in por_cadeia.items():
            for cls in ("cena", "tela"):
                g[k[idx]][cls] += cnt.get(cls, 0)
        mistas = [(x, c) for x, c in g.items() if c["cena"] and c["tela"]]
        nm = sum(min(c["cena"], c["tela"]) for _, c in mistas)
        resumo.append("nivel %s: %d chaves, %d mistas (cena e tela), %d desenhos na minoria das mistas" % (
            nivel, len(g), len(mistas), nm))
    # a marca contra o oraculo: erro = cena marcada tela ou tela marcada cena (so_4x3/so_larga nao tem par: ficam a parte)
    if any(k[0] != "-" for k in marca):
        resumo.append("marca x oraculo: " + ", ".join("%s/%s %d" % (m, c, n) for (m, c), n in sorted(marca.items())))
        erros = marca[("cena", "tela")] + marca[("tela", "cena")]
        resumo.append("marca errada (cena<->tela): %d; desconhecida: %d; ambigua: %d" % (
            erros, sum(n for (m, _), n in marca.items() if m == "desconhecida"),
            sum(n for (m, _), n in marca.items() if m == "ambiguo")))
    texto = "\n".join(resumo) + "\n"
    open(os.path.join(out, "resumo.txt"), "w", encoding="utf-8").write(texto)
    print(texto, end="")
    if divergentes:
        sys.exit("%d pares com proveniencia diferente nos dois lados: a captura nao vale" % divergentes)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capturar")
    c.add_argument("--exe", required=True)
    c.add_argument("--data", required=True)
    c.add_argument("--cartao", required=True)
    c.add_argument("--saida", required=True)
    c.add_argument("--cenas", default="")
    c.add_argument("--escritor-dma", action="store_true", help="BS_PROVENIENCIA_DMA=1: a cadeia de quem escreveu o buffer do DMA")
    x = sub.add_parser("cruzar")
    x.add_argument("pasta")
    x.add_argument("--ir-tela", required=True)
    x.add_argument("--limite", default="8C158000")
    a = ap.parse_args()
    capturar(a) if a.cmd == "capturar" else cruzar(a)


if __name__ == "__main__":
    main()
