#!/usr/bin/env python3
"""Testes do Monitor de Campanha (tools/dev/campanha.py) com o jogo de verdade (Preview), em campanhas de TESTE.

O jogador e simulado sem mudar o caminho da gravacao: BS_GRAVAR_BASE faz um roteiro jogar ate um passo e o controle
virtual do SDL (BS_CONTROLE_VIRTUAL) fecha a janela. As bases sao as duas sessoes reais de 2026-10-21 (LOAD do save
g1, SAVE no slot 1; LOAD e SAVE no slot 2) e o NEW GAME (passo_newgame_area1). Casos:
  C1 cadeia de duas sessoes com SAVE/LOAD reais, reproduzida (TESTE REPRODUZIDO) e reproduzida de novo igual
  C2 corrupcao: um byte da entrada, um save e o programa fixado -> verificar reprova e jogar recusa continuar
  C3 estado gravado adulterado (e o selo refeito): a reproducao acusa a divergencia no passo certo
  C4 queda: o jogo encerrado no meio -> sessao interrompida, conferida ate a queda; a seguinte continua do cartao
  C5 queda do monitor com o jogo aberto -> a sessao e recuperada dos arquivos (encerrada normalmente)
  C6 segunda instancia recusada enquanto uma sessao grava; campanha real recusa roteiro/controle de teste
  C7 creditos: o detector so aceita o filme ENDING/ENDING2
  C8 uma gravacao, duas edicoes: reproduz com Livre + Vulkan e em outro programa; negativo; nao mexe no veredito
  C9 gravada com a camera Livre movida: reproduz no programa fixado e na camera Original
  C10 sem o jogo: controle virtual marcado por arquivo selado, mesmo com os booleanos do manifesto editados
  python tools/dev/test_campanha.py [--casos C1,C7] [--reusar] [--outra <instalacao>]
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import campanha as cp  # noqa: E402

LOCAL = os.path.join(os.environ["LOCALAPPDATA"], "BlueStingerRecomp")
G1 = os.path.join(LOCAL, "n6f", "jogo", "g1", "BLUESTIN.000")
S1 = os.path.join(LOCAL, "gravacoes", "sessao-20261021-140145", "entrada.txt")
S2 = os.path.join(LOCAL, "gravacoes", "sessao-20261021-145006", "entrada.txt")
NEWGAME = os.path.join(cp.REPO, "tools", "dist", "roteiros", "passo_newgame_area1.txt")
ARGS_SESSOES = "--audio-musica nativo --audio-falas nativo"  # os das sessoes-base (tempo nativo: gravadas antes de 2026-10-24)
TMP = os.path.join(os.environ.get("TEMP", "."), "bs-test-campanha")

falhas = []


def check(nome, ok, det=""):
    print("[%s] %s %s" % ("ok" if ok else "FALHA", nome, det), flush=True)
    if not ok:
        falhas.append(nome)


def controle(nome, segundos):
    os.makedirs(TMP, exist_ok=True)
    p = os.path.join(TMP, nome + ".txt")
    with open(p, "w", encoding="ascii") as f:
        f.write("%d sair\n" % int(segundos * 1000))
    return p


def fim_e_vb(entrada):
    r = cp.gv.rodape(entrada)
    return r


def segundos_da_base(entrada, folga=25):
    r = fim_e_vb(entrada)
    return (r[1] / 59.94 if r else 300) + folga


def limpa(cid):
    shutil.rmtree(cp.pasta(cid), ignore_errors=True)


def flip(path, off=-40):
    with open(path, "r+b") as f:
        f.seek(off, os.SEEK_END if off < 0 else os.SEEK_SET)
        b = f.read(1)
        f.seek(-1, os.SEEK_CUR)
        f.write(bytes([b[0] ^ 0x01]))


def silencioso(*a, **k):
    pass


def c1():
    cid = "teste-c1"
    limpa(cid)
    m = cp.nova(teste=True, save=G1, cid=cid, args=ARGS_SESSOES, log=silencioso)
    base1, fim1 = S1, cp.gv.rodape(S1)[0]
    m, s = cp.jogar(cid, base1, fim1, controle("c1s1", segundos_da_base(base1)), log=silencioso)
    check("C1 sessao 1 encerrada normalmente, com SAVE", s["estado"] == "encerrada" and s["ultimo_save_passo"] is not None,
          "estado %s, ultimo save %s, ate o passo %s" % (s["estado"], s["ultimo_save_passo"], s["fim_passo"]))
    m, s2 = cp.jogar(cid, S2, cp.gv.rodape(S2)[0], controle("c1s2", segundos_da_base(S2)), log=silencioso)
    check("C1 sessao 2 comeca dos saves da 1 e salva", s2["estado"] == "encerrada" and s2["saves_inicio"] == s["saves_fim"]
          and s2["ultimo_save_passo"] is not None, "ultimo save %s" % s2["ultimo_save_passo"])
    rc = cp.verificar(cid, log=silencioso)
    rel = cp.ler_json(os.path.join(cp.pasta(cid), "relatorio.json"))
    check("C1 cadeia reproduzida", rc == 0 and rel["veredito"].startswith("TESTE REPRODUZIDO") and not rel["falhas"],
          "%s %s" % (rel["veredito"], rel["falhas"]))
    r1 = [cp.ler_json(os.path.join(cp.sessao_dir(m, n), "reproducao", "resultado.json"))["saves_fim_reproduzidos"] for n in (1, 2)]
    rc2 = cp.verificar(cid, log=silencioso)
    r2 = [cp.ler_json(os.path.join(cp.sessao_dir(m, n), "reproducao", "resultado.json"))["saves_fim_reproduzidos"] for n in (1, 2)]
    check("C1 segunda reproducao igual (deterministica)", rc2 == 0 and r1 == r2)
    check("C1 creditos nao alcancados nessa cadeia", not rel["creditos"])
    return cid


def c2(cid):
    m = cp.manifesto(cid)
    d2 = cp.sessao_dir(m, 2)
    for nome, alvo in (("entrada", os.path.join(d2, "entrada.txt")),
                       ("save", next(os.path.join(r, a) for r, _, fs in os.walk(os.path.join(cp.sessao_dir(m, 1), "saves")) for a in fs))):
        bak = alvo + ".bak"
        shutil.copy(alvo, bak)
        flip(alvo)
        rc = cp.verificar(cid, log=silencioso)
        rel = cp.ler_json(os.path.join(cp.pasta(cid), "relatorio.json"))
        check("C2 %s corrompido: verificar reprova" % nome, rc != 0 and rel["veredito"] == "REPROVADA", rel["falhas"][:1])
        try:
            cp.jogar(cid, log=silencioso)
            recusou = False
        except cp.Erro:
            recusou = True
        check("C2 %s corrompido: jogar recusa continuar" % nome, recusou)
        os.replace(bak, alvo)
    exe = cp.exe(m)
    bak = exe + ".bak"
    shutil.copy(exe, bak)
    flip(exe, 0x2000)
    rc = cp.verificar(cid, log=silencioso)
    rel = cp.ler_json(os.path.join(cp.pasta(cid), "relatorio.json"))
    check("C2 programa fixado alterado: verificar reprova", rc != 0 and any("programa" in f for f in rel["falhas"]), rel["falhas"][:1])
    os.replace(bak, exe)
    check("C2 restaurado: reproduz de novo", cp.verificar(cid, log=silencioso) == 0)


def c3(cid):
    m = cp.manifesto(cid)
    s = m["sessoes"][0]
    d = cp.sessao_dir(m, 1)
    est = os.path.join(d, "estado.tsv")
    bak = est + ".bak"
    shutil.copy(est, bak)
    linhas = open(est, encoding="utf-8").read().split("\n")
    alvo = 1 + len(linhas) // 2
    c = linhas[alvo].split("\t")
    passo = int(c[0])
    c[2] = ("0" if c[2][0] != "0" else "1") + c[2][1:]
    linhas[alvo] = "\t".join(c)
    with open(est, "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(linhas))
    s["selo"] = cp.selo(d)  # o selo refeito: simula uma gravacao que de fato divergiu da reproducao
    cp.salvar(m)
    rc = cp.verificar(cid, log=silencioso)
    r = cp.ler_json(os.path.join(d, "reproducao", "resultado.json"))
    check("C3 divergencia acusada no passo adulterado", rc != 0 and r["veredito"] == "DIVERGENTE" and r["primeira_divergencia"] == passo,
          "primeira divergencia %s, esperado %d" % (r["primeira_divergencia"], passo))
    os.replace(bak, est)
    m = cp.manifesto(cid)
    m["sessoes"][0]["selo"] = cp.selo(d)
    cp.salvar(m)


def c4():
    cid = "teste-c4"
    limpa(cid)
    cp.nova(teste=True, cid=cid, log=silencioso)
    morto = {}

    def matar(m, s, p):
        def f():
            time.sleep(70)
            subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], capture_output=True)
            morto["ok"] = True
        threading.Thread(target=f, daemon=True).start()
    m, s = cp.jogar(cid, NEWGAME, 100000, controle("c4s1", 600), log=silencioso, ao_iniciar=matar)
    check("C4 queda: sessao interrompida, sem rodape", morto.get("ok") and s["estado"] == "interrompida" and not s["rodape"] and s["fim_passo"] > 0,
          "estado %s, ate o passo %s" % (s["estado"], s["fim_passo"]))
    check("C4 progresso: nada salvo, tudo depois do ultimo save", s["progresso"]["ultimo_save_no_passo"] is None and
          s["progresso"]["passos_depois_do_ultimo_save"] == s["fim_passo"] and not s["progresso"]["avancou_a_cadeia"])
    m, s2 = cp.jogar(cid, NEWGAME, 1500, controle("c4s2", 75), log=silencioso)
    check("C4 a sessao seguinte continua do cartao deixado", s2["estado"] == "encerrada" and s2["saves_inicio"] == s["saves_fim"])
    rc = cp.verificar(cid, log=silencioso)
    r1 = cp.ler_json(os.path.join(cp.sessao_dir(m, 1), "reproducao", "resultado.json"))
    check("C4 a sessao interrompida e conferida ate a queda", rc == 0 and r1["veredito"] == "REPRODUZIDA_ATE_A_QUEDA", r1["veredito"])
    limpa(cid)


def c5():
    cid = "teste-c5"
    limpa(cid)
    cp.nova(teste=True, cid=cid, log=silencioso)
    ctl = controle("c5", 60)
    mon = subprocess.Popen([sys.executable, "-I", os.path.join(os.path.dirname(__file__), "campanha.py"), "jogar", cid,
                            "--teste-base", NEWGAME, "--teste-ate", "100000", "--teste-controle", ctl],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(20)
    m = cp.manifesto(cid)
    pid = m["sessoes"][0].get("pid")
    subprocess.run(["taskkill", "/PID", str(mon.pid), "/F"], capture_output=True)  # so o monitor; o jogo segue
    try:
        cp.jogar(cid, log=silencioso)
        recusou = False
    except cp.Erro:
        recusou = True
    check("C6 segunda instancia recusada com o jogo gravando", recusou and cp.vivo(pid))
    while cp.vivo(pid):
        time.sleep(2)
    out = []
    cp.status(cid, log=out.append)
    m = cp.manifesto(cid)
    cp.recuperar(m, log=out.append)
    s = cp.manifesto(cid)["sessoes"][0]
    check("C5 queda do monitor: sessao recuperada dos arquivos e encerrada normalmente", s["estado"] == "encerrada" and s["rodape"],
          "estado %s" % s["estado"])
    check("C5 recuperada reproduz", cp.verificar(cid, log=silencioso) == 0)
    s = cp.manifesto(cid)["sessoes"][0]
    check("C5 controle virtual copiado e selado", bool((s.get("selo") or {}).get(cp.CONTROLE_TESTE)))
    limpa(cid)
    try:
        real = cp.nova(cid="campanha-teste-recusa", log=silencioso)
        cp.jogar(real["id"], NEWGAME, 10, None, log=silencioso)
        recusou = False
    except cp.Erro:
        recusou = True
    finally:
        limpa("campanha-teste-recusa")
    check("C6 campanha real recusa roteiro de teste", recusou)
    try:
        cp.nova(save=G1, cid="campanha-teste-recusa2", log=silencioso)
        recusou = False
    except cp.Erro:
        recusou = True
    finally:
        limpa("campanha-teste-recusa2")
    check("C6 campanha real recusa comecar de um save", recusou)


def c10():
    """Revisao Codex 20261102-041748: os booleanos do manifesto sao editaveis; o que decide e o arquivo selado."""
    base = {"origem": {"tipo": "newgame"}, "camera": "original", "args": cp.ARGS + " --camera original", "relogio": cp.gv.RELOGIO,
            "estado_sem_camera": False}
    cid = "teste-c10"
    limpa(cid)
    d = os.path.join(cp.pasta(cid), "sessao_001")
    os.makedirs(d)
    try:
        s = {"n": 1, "teste": {"base": False, "controle": False}}
        m = dict(base, id=cid, sessoes=[s])
        check("C10 sessao limpa nao tem impedimento", cp.impedimentos_reais(m) == [], cp.impedimentos_reais(m))
        with open(os.path.join(d, cp.CONTROLE_TESTE), "w", encoding="utf-8") as f:
            f.write("0 conectar\n")
        check("C10 arquivo do controle: impedido", any("controle" in x for x in cp.impedimentos_reais(m)))
        s["selo"] = cp.selo(d)
        check("C10 o selo cobre o arquivo do controle", bool(s["selo"].get(cp.CONTROLE_TESTE)))
        os.remove(os.path.join(d, cp.CONTROLE_TESTE))
        check("C10 arquivo apagado, selo mantido: impedido", any("controle" in x for x in cp.impedimentos_reais(m)))
        check("C10 arquivo apagado: selo acusa",
              any(cp.CONTROLE_TESTE in dif for _, dif in cp.conferir_selos(dict(m, sessoes=[dict(s, estado="encerrada")]))))
    finally:
        limpa(cid)


def c7():
    os.makedirs(TMP, exist_ok=True)
    arq, sai = os.path.join(TMP, "arquivos.tsv"), os.path.join(TMP, "jogo.txt")
    with open(arq, "w", encoding="utf-8") as f:
        f.write("seq\tvblank\torigem\ttipo\tarquivo\tdeslocamento\tbytes\tsha256\n0\t1\tnativo\tdado\tBSDATA/CDG000.NJ\t0\t16\t-\n")
    casos = (
        ("outro filme", ["[video] E200B.SFD -> E200B.mp4 (passo 10)", "[video] fim natural: 100 ms logicos (passo 11)"], False),
        ("creditos com fim natural", ["[video] ENDING.SFD -> ENDING.mp4 (passo 10)", "[video] fim natural: 100 ms logicos (passo 90)"], True),
        ("creditos parados pelo jogo", ["[video] ENDING2.SFD -> ENDING2.mp4 (mod) (passo 10)", "[video] parado pelo jogo em 50 ms logicos (passo 40)"], True),
        ("creditos sem fim na reproducao", ["[video] ENDING.SFD -> ENDING.mp4 (passo 10)"], False),
        ("nome parecido", ["[video] NOENDING.SFD -> NOENDING.mp4 (passo 10)", "[video] fim natural: 1 ms logicos (passo 11)"], False),
        ("o fim de outro filme nao vale para os creditos",
         ["[video] E200B.SFD -> E200B.mp4 (passo 5)", "[video] fim natural: 1 ms logicos (passo 6)", "[video] ENDING.SFD -> ENDING.mp4 (passo 10)"], False),
        ("erro do filme nao e inicio", ["[video] filme sem recurso: ENDING.SFD (ausente no disco original) (passo 10)"], False),
    )
    for nome, linhas, esperado in casos:
        with open(sai, "w", encoding="utf-8") as f:
            f.write("\n".join(linhas) + "\n")
        c = cp.cobertura_arquivos(arq, sai)
        check("C7 %s -> %s" % (nome, esperado), c["creditos"] == esperado and c["arquivos"] == ["BSDATA/CDG000.NJ"], c["filmes"])


def gameplay(cid, nome):
    """Sessao em gameplay de verdade: o LOAD do save real (ate o passo ~2131, roteiro) e depois o controle virtual, com o
    analogico direito (camera) e o esquerdo (movimento) mexendo pelo resto da sessao."""
    os.makedirs(TMP, exist_ok=True)
    ctl = os.path.join(TMP, nome + ".txt")
    with open(ctl, "w", encoding="ascii") as f:
        f.write("0 conectar\n")
        for t in range(60, 170, 10):
            f.write("%d000 rightx 30000\n%d000 rightx 0\n" % (t, t + 3))
            f.write("%d000 leftx 30000\n%d000 leftx 0\n" % (t + 4, t + 6))
        f.write("180000 sair\n")
    return cp.jogar(cid, cp.gv.LOAD_SLOT1, cp.gv.LOAD_SLOT1_ATE, ctl, log=silencioso)


def c8(outra):
    """Uma gravacao, duas edicoes: a sessao curta reproduzida com outra apresentacao e em outro programa. E o portao de cada
    subetapa do N9 (docs/N9-PLANO.md secao 14.2)."""
    cid = "teste-c8"
    limpa(cid)
    m = cp.nova(teste=True, save=G1, cid=cid, log=silencioso)
    check("C8 a camera e explicita na campanha", "--camera original" in m["args"] and not m["estado_sem_camera"], m["args"])
    m, s = gameplay(cid, "c8")
    check("C8 sessao em gameplay gravada", s["estado"] == "encerrada" and s["fim_passo"] > cp.gv.LOAD_SLOT1_ATE + 200, "ate o passo %s" % s["fim_passo"])
    inst = m["instalacao"]
    # uma campanha de teste nao vira real editando a marca do manifesto
    p = os.path.join(cp.pasta(cid), "campanha.json")
    original = open(p, encoding="utf-8").read()
    x = json.loads(original)
    x["teste"] = False
    cp.escrever_json(p, x)
    rc = cp.verificar(cid, log=silencioso)
    rel = cp.ler_json(os.path.join(cp.pasta(cid), "relatorio.json"))
    check("C8 marca de teste editada: reprovada, sem reproduzir", rc != 0 and rel["veredito"] == "REPROVADA" and
          any("NEW GAME" in f for f in rel["falhas"]) and any("roteiro" in f for f in rel["falhas"]), rel["falhas"][:3])
    with open(p, "w", encoding="utf-8") as f:
        f.write(original)
    for rot, kw in (("mesma-livre-vulkan", {"camera": "livre", "render": "nativo"}), ("outro-programa", {})):
        alvo = outra if rot == "outro-programa" else inst
        rc = cp.reproduzir_em(cid, alvo, rot, log=silencioso, **kw)
        rel = cp.ler_json(os.path.join(cp.pasta(cid), "outros", rot, "relatorio.json"))
        check("C8 reproduz em %s" % rot, rc == 0 and rel["veredito"] == "REPRODUZIDA EM OUTRO PROGRAMA", (rel["alvo"]["args"], rel["falhas"][:2]))
    apos = cp.manifesto(cid)
    check("C8 reproduzir-em nao mexe no veredito da campanha", "verificacao" not in apos and not apos.get("concluida"))
    # negativo: a mesma comparacao tem de acusar uma diferenca de estado no passo certo
    est = os.path.join(cp.sessao_dir(m, 1), "referencia_logica", "estado.tsv")
    linhas = open(est, encoding="utf-8").read().split("\n")
    alvo = 1 + len(linhas) // 2
    c = linhas[alvo].split("\t")
    passo = int(c[0])
    c[2] = ("0" if c[2][0] != "0" else "1") + c[2][1:]
    linhas[alvo] = "\t".join(c)
    with open(est, "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(linhas))
    rc = cp.reproduzir_em(cid, inst, "negativo", log=silencioso)
    rel = cp.ler_json(os.path.join(cp.pasta(cid), "outros", "negativo", "relatorio.json"))
    check("C8 negativo: divergencia acusada no passo adulterado", rc != 0 and rel["sessoes"][0]["primeira_divergencia"] == passo,
          "%s, esperado %d" % (rel["sessoes"][0]["primeira_divergencia"], passo))
    try:
        cp.reproduzir_em(cid, os.path.join(TMP, "nao-existe"), "x", log=silencioso)
        recusou = False
    except cp.Erro:
        recusou = True
    check("C8 instalacao invalida recusada", recusou)
    limpa(cid)


def c9():
    """Gravada com a camera Livre de verdade (analogico direito movido pelo controle virtual): a mesma gravacao vale na Original."""
    cid = "teste-c9"
    limpa(cid)
    m = cp.nova(teste=True, save=G1, cid=cid, camera="livre", log=silencioso)
    check("C9 campanha Livre grava sem o objeto camera", "--camera livre" in m["args"] and m["estado_sem_camera"])
    m, s = gameplay(cid, "c9")
    jogo = open(os.path.join(cp.sessao_dir(m, 1), "jogo.txt"), encoding="utf-8", errors="replace").read()
    resumo = [l for l in jogo.splitlines() if l.startswith("[camera]")]
    check("C9 sessao gravada", s["estado"] == "encerrada", "ate o passo %s; %s" % (s["fim_passo"], resumo[-1:]))
    desloc = re.search(r"com deslocamento (\d+)", resumo[-1] if resumo else "")
    giradas = re.search(r"amostras giradas (\d+)", resumo[-1] if resumo else "")
    check("C9 a vista e o movimento foram girados de verdade", bool(desloc and giradas) and int(desloc.group(1)) > 0 and int(giradas.group(1)) > 0,
          resumo[-1:])
    rc = cp.verificar(cid, log=silencioso)
    check("C9 reproduz no programa fixado", rc == 0, cp.ler_json(os.path.join(cp.pasta(cid), "relatorio.json"))["falhas"][:2])
    rc = cp.reproduzir_em(cid, m["instalacao"], "original", camera="original", log=silencioso)
    rel = cp.ler_json(os.path.join(cp.pasta(cid), "outros", "original", "relatorio.json"))
    check("C9 a gravacao Livre reproduz na camera Original", rc == 0, rel["falhas"][:3])
    limpa(cid)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--casos", default="C1,C2,C3,C4,C5,C7,C8,C9,C10")
    ap.add_argument("--reusar", action="store_true", help="C2/C3 sobre a campanha teste-c1 ja gravada, sem refazer o C1")
    ap.add_argument("--outra", default=os.path.join(cp.REPO, "dist", "BlueStinger-Preview.anterior"), help="instalacao de outro programa, para o C8")
    a = ap.parse_args()
    casos = a.casos.split(",")
    cid = None
    if "C7" in casos:
        c7()
    if "C10" in casos:
        c10()
    ausentes = [f for f in (G1, S1, S2) if not os.path.isfile(f)]
    if ausentes and set(casos) - {"C7", "C10"}:  # um caso pedido sem poder rodar nao e aprovacao (revisao Codex 20261102-041748)
        print("FALHA: base ausente: %s" % ", ".join(ausentes))
        return 1
    if "C1" in casos:
        cid = c1()
    elif a.reusar and os.path.isdir(cp.pasta("teste-c1")):
        cid = "teste-c1"
    if cid and "C2" in casos:
        c2(cid)
    if cid and "C3" in casos:
        c3(cid)
    if "C4" in casos:
        c4()
    if "C5" in casos:
        c5()
    if "C8" in casos:
        c8(a.outra)
    if "C9" in casos:
        c9()
    if cid and not falhas:
        limpa(cid)
    print("%d falha(s)%s" % (len(falhas), "" if not falhas else ": " + ", ".join(falhas)))
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
