#!/usr/bin/env python3
"""Monitor de Campanha (N8.8, requisito D-N8-4): jogar a campanha inteira em varias sessoes e provar que ela e
reproduzivel do inicio aos creditos.

O jogador so joga. Cada sessao e gravada pelo proprio jogo, sem mudar nada da logica (os mesmos mecanismos do
tools/dev/gravacoes.py): BS_GRAVAR (entrada por passo de jogo, uma linha por mudanca, gravada na hora: uma queda nao
perde o que ja foi jogado), BS_STATE_PASSO (estado logico por passo), BS_SAVES_LOG (o passo de cada LOAD e SAVE) e um
cartao proprio da campanha (--saves-pasta). A sessao seguinte comeca dos saves que a anterior deixou.

A prova NAO e a gravacao: e a reproducao. `verificar` reproduz cada sessao headless com o executavel fixado na criacao
da campanha e confere o estado de todos os passos, os LOAD/SAVE e os saves; a cadeia so vale se o cartao de inicio de
cada sessao for igual aos saves de fim REPRODUZIDOS da anterior (a primeira comeca de um cartao vazio, NEW GAME), e a
campanha so e dada como concluida se a reproducao chegar aos creditos.

Dados particulares (gravacoes, saves, copia do programa) ficam fora do repositorio, em
%LOCALAPPDATA%\\BlueStingerRecomp\\campanhas\\<id>. A instalacao usada (Preview ou a do Builder) nao e alterada: o
programa (exe, DLLs, mods) e copiado para a campanha e os dados sao lidos no lugar, fixados pela identidade.

  python tools/dev/campanha.py                     interface (Jogar e Gravar Campanha)
  python tools/dev/campanha.py nova [--instalacao <pasta>] [--camera original|livre]
  python tools/dev/campanha.py jogar [<id>]        continua a campanha (a mais recente aberta, sem <id>)
  python tools/dev/campanha.py status [<id>]
  python tools/dev/campanha.py verificar [<id>] [--sessao N]
  python tools/dev/campanha.py reproduzir-em <id> --instalacao <pasta> [--rotulo r] [--camera m] [--render r]
  python tools/dev/campanha.py lista

Uma gravacao, duas edicoes (gate pos-N9, docs/N9-PLANO.md secao 14): a entrada gravada e a efetiva, a reproducao e
headless (independe do renderizador) e `reproduzir-em` reproduz a cadeia em OUTRO programa, comparando o estado logico (sem
o objeto camera do SDK), os LOAD/SAVE, os saves e os VBlanks contra o programa fixado. So informa: nao muda o veredito.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_identidade as ei  # noqa: E402
import gravacoes as gv  # noqa: E402

REPO = gv.REPO
RAIZ = os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "BlueStingerRecomp", "campanhas")
FORMATO = 1
ARGS = gv.com_tempo(gv.ARGS_PADRAO)  # os tres provedores nativos e o tempo da janela, explicito
CREDITOS = ("ENDING.SFD", "ENDING2.SFD")  # os filmes dos creditos (N5)
# a saida do jogo: "[video] NEWGAME.SFD -> NEWGAME.mp4 (passo 1030)"; "[video] fim natural: ..."; "[video] parado pelo jogo ..."
FILME_INICIO = re.compile(r"^\[video\] (\S+\.SFD) -> \S+.*?(?: \(passo (\d+)\))?\s*$", re.I)
FILME_FIM = re.compile(r"^\[video\] (fim natural|parado pelo jogo)\b", re.I)
SEM_PROGRAMA = ("data", "licencas")  # o que nao e copiado da instalacao (os dados ficam no lugar)
CAMERAS = ("original", "livre")


class Erro(Exception):
    pass


# ---------------------------------------------------------------- arquivos e manifesto

def agora():
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def escrever_json(path, obj):
    """Escrita atomica: um corte no meio deixa o arquivo anterior inteiro."""
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2, ensure_ascii=False)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def ler_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def pasta(cid):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", cid or ""):
        raise Erro("identificador de campanha invalido: %r" % cid)
    return os.path.join(RAIZ, cid)


def manifesto(cid):
    p = os.path.join(pasta(cid), "campanha.json")
    if not os.path.isfile(p):
        raise Erro("campanha %s nao encontrada em %s" % (cid, RAIZ))
    m = ler_json(p)
    if m.get("formato") != FORMATO:
        raise Erro("campanha %s com formato %r (este monitor le o %d)" % (cid, m.get("formato"), FORMATO))
    return m


def salvar(m):
    escrever_json(os.path.join(pasta(m["id"]), "campanha.json"), m)


def campanhas():
    if not os.path.isdir(RAIZ):
        return []
    out = []
    for n in sorted(os.listdir(RAIZ)):
        if os.path.isfile(os.path.join(RAIZ, n, "campanha.json")):
            try:
                out.append(manifesto(n))
            except (Erro, ValueError, OSError):
                pass
    return out


def sessao_dir(m, n):
    return os.path.join(pasta(m["id"]), "sessao_%03d" % n)


def selo(d):
    """SHA-256 de tudo o que a prova da sessao usa (entrada, estado, LOAD/SAVE e o cartao, inicio e fim): conferido antes
    de continuar e de verificar (corrupcao ou edicao depois da gravacao)."""
    out = {}
    for nome in ("entrada.txt", "estado.tsv", "saves.tsv"):
        p = os.path.join(d, nome)
        out[nome] = sha(p) if os.path.isfile(p) else None
    for sub in ("saves_inicio", "saves"):
        for raiz, _, arqs in os.walk(os.path.join(d, sub)):
            for a in sorted(arqs):
                p = os.path.join(raiz, a)
                out[os.path.relpath(p, d).replace("\\", "/")] = sha(p)
    return out


def ler_saves_log(path):
    out = []
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as f:
            next(f, None)
            for l in f:
                c = l.rstrip("\n").split("\t")
                if len(c) == 3 and c[0].isdigit():
                    out.append([int(c[0]), c[1], c[2]])
    return out


# ---------------------------------------------------------------- programa fixado e dados

def identidade_programa(d):
    return ei.arvore(d)


def identidade_dados(d):
    return ei.arvore(d)


def conferir_instalacao(m):
    """O programa copiado e os dados tem de ser os da criacao: outra versao reproduziria outra campanha."""
    prog = os.path.join(pasta(m["id"]), "programa")
    if identidade_programa(prog) != m["programa"]["identidade"]:
        raise Erro("o programa fixado da campanha foi alterado (%s); a campanha nao pode continuar com ele" % prog)
    if not os.path.isdir(m["dados"]["pasta"]):
        raise Erro("dados do jogo ausentes: %s" % m["dados"]["pasta"])
    if identidade_dados(m["dados"]["pasta"]) != m["dados"]["identidade"]:
        raise Erro("os dados do jogo mudaram desde a criacao da campanha (%s): reinstale os mesmos dados ou comece "
                   "uma campanha nova" % m["dados"]["pasta"])


def exe(m):
    return os.path.join(pasta(m["id"]), "programa", "BlueStinger.exe")


def sem_opcao(args, nome):
    """Os argumentos sem `nome valor`."""
    a, out, i = args.split(), [], 0
    while i < len(a):
        if a[i] == nome:
            i += 2
            continue
        out.append(a[i])
        i += 1
    return out


def nova(instalacao=None, teste=False, save=None, cid=None, args=None, log=print, camera="original"):
    inst = os.path.abspath(instalacao or os.path.join(REPO, "dist", "BlueStinger-Preview"))
    if camera not in CAMERAS:
        raise Erro("camera invalida: %r (use %s)" % (camera, " ou ".join(CAMERAS)))
    if not os.path.isfile(os.path.join(inst, "BlueStinger.exe")) or not os.path.isfile(os.path.join(inst, "data", "disc.manifest")):
        raise Erro("instalacao invalida (sem BlueStinger.exe ou data\\disc.manifest): %s" % inst)
    if save and not teste:
        raise Erro("uma campanha que vale para o D-N8-4 comeca do NEW GAME (cartao vazio); --save so em campanha de teste")
    if args and not teste:
        raise Erro("--args so em campanha de teste (a campanha real usa os provedores nativos e o tempo da janela)")
    cid = cid or ("teste-" if teste else "campanha-") + datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    d = pasta(cid)
    if os.path.exists(d):
        raise Erro("a campanha %s ja existe" % cid)
    os.makedirs(d)
    prog = os.path.join(d, "programa")
    shutil.copytree(inst, prog, ignore=lambda r, ns: [n for n in ns if r == inst and n.lower() in SEM_PROGRAMA])
    dados = os.path.join(inst, "data")
    origem = {"tipo": "newgame"}
    if save:
        origem = {"tipo": "save", "arquivo": os.path.abspath(save), "sha256": sha(save)}
    m = {"formato": FORMATO, "id": cid, "criada": agora(), "teste": bool(teste), "origem": origem,
         "instalacao": inst,
         "programa": {"identidade": identidade_programa(prog), "exe_sha256": sha(os.path.join(prog, "BlueStinger.exe")),
                      "exe_identidade": ei.identidade(os.path.join(prog, "BlueStinger.exe")),
                      "versao": open(os.path.join(prog, "VERSAO.txt"), encoding="utf-8", errors="replace").readline().strip()
                      if os.path.isfile(os.path.join(prog, "VERSAO.txt")) else ""},
         "dados": {"pasta": dados, "identidade": identidade_dados(dados), "manifesto_sha256": sha(os.path.join(dados, "disc.manifest"))},
         # a camera sempre explicita: sem --camera, o [camera] do controles.toml do jogador entraria na gravacao
         "args": " ".join(sem_opcao(gv.com_tempo(args) if args else ARGS, "--camera") + ["--camera", camera]),
         "camera": camera,
         # na Livre a vista mexe no objeto camera do SDK, que nao e estado do jogo: a sessao grava e compara sem ele; na
         # Original a comparacao continua com o estado completo
         "estado_sem_camera": camera == "livre",
         "relogio": gv.RELOGIO, "sessoes": []}
    salvar(m)
    log("campanha %s criada (programa %s, %s)" % (cid, m["programa"]["exe_identidade"], m["programa"]["versao"]))
    return m


# ---------------------------------------------------------------- sessoes

def vivo(pid):
    if not pid:
        return False
    try:
        r = subprocess.run(["tasklist", "/FI", "PID eq %d" % pid, "/NH"], capture_output=True, text=True, timeout=20)
        return str(pid) in r.stdout and "bluestinger" in r.stdout.lower()
    except (OSError, subprocess.SubprocessError):
        return True  # na duvida, nao trata como queda


def fechar_sessao(m, s, rc=None):
    """Registra o fim de uma sessao a partir dos arquivos (vale depois de uma queda do jogo ou do proprio monitor)."""
    d = sessao_dir(m, s["n"])
    entrada = os.path.join(d, "entrada.txt")
    fim, cps = gv.roteiro_info(entrada) if os.path.isfile(entrada) else (0, [])
    rod = gv.rodape(entrada) if os.path.isfile(entrada) else None
    ops = ler_saves_log(os.path.join(d, "saves.tsv"))
    saves_ok = [p for p, op, r in ops if op == "save" and r == "ok"]
    s.update({"fim": agora(), "rc": rc if rc is not None else s.get("rc"), "rodape": rod is not None,
              "fim_passo": fim, "fim_vblank": rod[1] if rod else None, "checkpoints": cps,
              "saves_fim": gv.hashes(os.path.join(d, "saves")), "operacoes": ops,
              "ultimo_save_passo": saves_ok[-1] if saves_ok else None, "selo": selo(d)})
    # rc None: o monitor caiu e nao viu o fim do processo; o rodape so existe no fechamento normal do jogo
    normal = rod is not None and s["rc"] in (0, None)
    s["estado"] = "encerrada" if normal else "interrompida"
    s["motivo"] = "" if normal else ("jogo fechado sem o rodape (queda, processo encerrado ou travamento)" if rod is None
                                      else "o jogo terminou com rc=%s" % s["rc"])
    # progresso: o que foi gravado, o que ficou salvo (a sessao seguinte continua dali) e o que se perdeu
    s["progresso"] = {"gravado_ate_passo": fim, "ultimo_save_no_passo": s["ultimo_save_passo"],
                      "passos_depois_do_ultimo_save": (fim - s["ultimo_save_passo"]) if s["ultimo_save_passo"] is not None else fim,
                      "avancou_a_cadeia": bool(saves_ok)}
    s.pop("pid", None)
    salvar(m)
    return s


def recuperar(m, log=print):
    """Sessao marcada como gravando, com o jogo morto: queda do jogo ou do monitor. Fecha a partir dos arquivos."""
    for s in m["sessoes"]:
        if s.get("estado") == "gravando":
            if vivo(s.get("pid")):
                raise Erro("a sessao %d da campanha %s esta sendo gravada agora (PID %s)" % (s["n"], m["id"], s.get("pid")))
            log("sessao %d estava gravando e o jogo nao esta aberto: recuperada dos arquivos" % s["n"])
            fechar_sessao(m, s)


def conferir_selos(m):
    ruins = []
    for s in m["sessoes"]:
        if s.get("estado") in ("encerrada", "interrompida") and s.get("selo"):
            atual = selo(sessao_dir(m, s["n"]))
            dif = sorted(k for k in set(atual) | set(s["selo"]) if atual.get(k) != s["selo"].get(k))
            if dif:
                ruins.append((s["n"], dif))
    return ruins


def jogar(cid=None, teste_base=None, teste_ate=None, teste_controle=None, log=print, ao_iniciar=None):
    """Grava a proxima sessao. Os parametros teste_* so valem em campanha de teste (roteiro antes do jogador, controle
    virtual): os testes automaticos simulam o jogador sem mudar o caminho da gravacao."""
    if cid is None:
        abertas = [c for c in campanhas() if not c.get("teste") and not c.get("concluida")]
        m = abertas[-1] if abertas else nova(log=log)
    else:
        m = manifesto(cid)
    if (teste_base or teste_controle) and not m.get("teste"):
        raise Erro("roteiro ou controle virtual so em campanha de teste")
    recuperar(m, log)
    ruins = conferir_selos(m)
    if ruins:
        raise Erro("arquivos de sessoes anteriores mudaram depois da gravacao (corrupcao ou edicao): %s; a campanha nao "
                   "continua sobre eles" % "; ".join("sessao %d: %s" % (n, ", ".join(d[:4])) for n, d in ruins))
    conferir_instalacao(m)
    n = len(m["sessoes"]) + 1
    d = sessao_dir(m, n)
    if os.path.exists(d):
        raise Erro("pasta da sessao %d ja existe sem registro no manifesto: %s (mova-a antes de continuar)" % (n, d))
    os.makedirs(d)
    ini = os.path.join(d, "saves_inicio")
    if n == 1:
        if m["origem"]["tipo"] == "save":
            gv.importar_save({"exe": exe(m), "data": m["dados"]["pasta"]}, m["origem"]["arquivo"], ini)
        else:
            os.makedirs(ini)
    else:
        shutil.copytree(os.path.join(sessao_dir(m, n - 1), "saves"), ini)
    shutil.copytree(ini, os.path.join(d, "saves"))
    env = {"BS_GRAVAR": os.path.join(d, "entrada.txt"), "BS_STATE_PASSO": os.path.join(d, "estado.tsv"),
           "BS_STATE_PASSO_CADA": "1", "BS_SAVES_LOG": os.path.join(d, "saves.tsv")}
    if m.get("estado_sem_camera"):
        env["BS_STATE_PASSO_SEM_CAMERA"] = "1"
    if teste_base:
        shutil.copy(teste_base, os.path.join(d, "base.txt"))
        env.update({"BS_GRAVAR_BASE": os.path.join(d, "base.txt"), "BS_GRAVAR_ATE": str(int(teste_ate))})
    if teste_controle:
        env["BS_CONTROLE_VIRTUAL"] = os.path.abspath(teste_controle)
    s = {"n": n, "estado": "gravando", "inicio": agora(), "saves_inicio": gv.hashes(ini),
         "programa_identidade": m["programa"]["identidade"], "teste": {"base": bool(teste_base), "controle": bool(teste_controle)}}
    m["sessoes"].append(s)
    salvar(m)
    c = {"exe": exe(m), "data": m["dados"]["pasta"]}
    p = gv.jogo(c, ["--saves-pasta", os.path.join(d, "saves")] + m["args"].split(), env, os.path.join(d, "jogo"), espera=False)
    s["pid"] = p.pid
    salvar(m)
    log("GRAVANDO a sessao %d da campanha %s (feche a janela do jogo para terminar; F5 = checkpoint)" % (n, m["id"]))
    if ao_iniciar:
        ao_iniciar(m, s, p)
    rc = p.wait()
    fechar_sessao(m, s, rc)
    pr = s["progresso"]
    log("sessao %d %s: gravada ate o passo %d; ultimo save no passo %s; %d passo(s) depois do ultimo save (nao continuam)%s" % (
        n, s["estado"], pr["gravado_ate_passo"], pr["ultimo_save_no_passo"], pr["passos_depois_do_ultimo_save"],
        "" if s["estado"] == "encerrada" else " -- " + s["motivo"]))
    return m, s


# ---------------------------------------------------------------- reproducao e verificacao

def alcance(m, s):
    """O que a sessao gravou: estado por passo, passos a comparar, ultimo passo, VBlank final, rodape e problemas."""
    d = sessao_dir(m, s["n"])
    gravado = gv.ler_estado(os.path.join(d, "estado.tsv")) if os.path.isfile(os.path.join(d, "estado.tsv")) else {}
    entrada = os.path.join(d, "entrada.txt")
    rod = gv.rodape(entrada) if os.path.isfile(entrada) else None
    problemas = []
    fim = rod[0] if rod else (max(gravado) if gravado else 0)
    passos = sorted(k for k in gravado if k <= fim)
    if not passos or passos[-1] != fim or passos != list(range(passos[0], fim + 1)):
        problemas.append("estado gravado sem continuidade ate o passo %d" % fim)
    if not rod:
        problemas.append("sessao interrompida: conferida ate o ultimo passo gravado")
    vb = rod[1] if rod else (int(gravado[fim][1]) + 1 if fim in gravado else 0)
    return {"gravado": gravado, "entrada": entrada, "rodape": rod, "fim": fim, "passos": passos, "vblank": vb, "problemas": problemas}


def rodar(executavel, dados, args, entrada, saves_inicio, r, vb, sem_camera):
    """Uma reproducao headless em `r`: o programa recebe so a entrada gravada e o cartao de inicio."""
    shutil.rmtree(r, ignore_errors=True)
    os.makedirs(r)
    shutil.copytree(saves_inicio, os.path.join(r, "saves"))
    env = {"BS_INPUT_SCRIPT": entrada, "BS_STATE_PASSO": os.path.join(r, "estado.tsv"), "BS_STATE_PASSO_CADA": "1",
           "BS_SAVES_LOG": os.path.join(r, "saves.tsv"), "BS_FILE_LOG": os.path.join(r, "arquivos.tsv")}
    if sem_camera:
        env["BS_STATE_PASSO_SEM_CAMERA"] = "1"
    t0 = time.time()
    rc = gv.jogo({"exe": executavel, "data": dados}, ["--saves-pasta", os.path.join(r, "saves")] + args + ["--headless", str(vb)],
                 env, os.path.join(r, "jogo"))
    return {"rc": rc, "segundos": round(time.time() - t0),
            "estado": gv.ler_estado(os.path.join(r, "estado.tsv")) if os.path.isfile(os.path.join(r, "estado.tsv")) else {},
            "operacoes": ler_saves_log(os.path.join(r, "saves.tsv")), "saves": gv.hashes(os.path.join(r, "saves")),
            "cobertura": cobertura_arquivos(os.path.join(r, "arquivos.tsv"), os.path.join(r, "jogo.txt"))}


def comparar_estado(ref, alvo, passos):
    """A primeira divergencia do estado (hash, vida, posicao) e o numero de passos em VBlank diferente."""
    diverg = next((k for k in passos if k not in alvo or alvo[k][2:] != ref[k][2:]), None)
    return diverg, sum(1 for k in passos if k in alvo and alvo[k][1] != ref[k][1])


def reproduzir_sessao(m, s, log=print):
    """Reproduz a sessao headless com o programa fixado; confere estado por passo, LOAD/SAVE e saves."""
    d = sessao_dir(m, s["n"])
    r = os.path.join(d, "reproducao")
    a = alcance(m, s)
    gravado, rod, fim, passos, problemas = a["gravado"], a["rodape"], a["fim"], a["passos"], a["problemas"]
    x = rodar(exe(m), m["dados"]["pasta"], m["args"].split(), a["entrada"], os.path.join(d, "saves_inicio"), r, a["vblank"],
              m.get("estado_sem_camera", False))
    rc, rep, cobertura, saves_r = x["rc"], x["estado"], x["cobertura"], x["saves"]
    diverg, vbdif = comparar_estado(gravado, rep, passos)
    ops_g, ops_r = s.get("operacoes", []), x["operacoes"]
    ops_ok = [o for o in ops_r if o[0] <= fim] == [o for o in ops_g if o[0] <= fim]
    saves_ok = saves_r == s.get("saves_fim")
    ok = diverg is None and vbdif == 0 and ops_ok and saves_ok and rc == 0 and not any("continuidade" in p for p in problemas)
    res = {"quando": agora(), "veredito": ("REPRODUZIDA" if rod else "REPRODUZIDA_ATE_A_QUEDA") if ok else "DIVERGENTE",
           "rc": rc, "segundos": x["segundos"], "passos": len(passos), "primeira_divergencia": diverg,
           "vblank_diferente": vbdif, "load_save_iguais": ops_ok, "saves_iguais": saves_ok, "saves_fim_reproduzidos": saves_r,
           "problemas": problemas, "creditos": cobertura["creditos"], "arquivos_lidos": cobertura["arquivos"],
           "filmes": cobertura["filmes"],
           "programa_identidade": m["programa"]["identidade"]}
    if diverg is not None:
        res["gravado"], res["reproduzido"] = gravado.get(diverg), rep.get(diverg)
    escrever_json(os.path.join(r, "resultado.json"), res)
    log("sessao %d: %s (%d passos; estado %s; LOAD/SAVE %s; saves %s; rc=%d; %d s)" % (
        s["n"], res["veredito"], len(passos), "igual" if diverg is None else "diverge no passo %d" % diverg,
        "iguais" if ops_ok else "DIFERENTES", "iguais" if saves_ok else "DIFERENTES", rc, res["segundos"]))
    return res


def filmes_da_saida(path):
    """Os filmes da saida do jogo, em ordem: nome, passo de inicio e como terminou ("fim natural", "parado pelo jogo" ou
    None: a reproducao acabou com o filme em andamento)."""
    out = []
    if os.path.isfile(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            for l in f:
                m = FILME_INICIO.match(l)
                if m:
                    out.append({"filme": m.group(1).upper(), "passo": int(m.group(2)) if m.group(2) else None, "fim": None})
                    continue
                m = FILME_FIM.match(l)
                if m and out and out[-1]["fim"] is None:
                    out[-1]["fim"] = m.group(1).lower()
    return out


def cobertura_arquivos(arquivos, saida=None):
    """Arquivos do disco lidos na reproducao (BS_FILE_LOG) e os filmes (saida do jogo). Creditos: um filme dos creditos
    que comecou E terminou dentro da reproducao; uma gravacao cortada no meio do filme nao conta."""
    nomes = set()
    if os.path.isfile(arquivos):
        with open(arquivos, encoding="utf-8", errors="replace") as f:
            next(f, None)
            for l in f:
                c = l.rstrip("\n").split("\t")
                if len(c) >= 5 and c[4] not in ("setor", "-", ""):
                    nomes.add(c[4])
    filmes = filmes_da_saida(saida) if saida else []
    return {"arquivos": sorted(nomes), "filmes": filmes,
            "creditos": any(x["filme"] in CREDITOS and x["fim"] is not None for x in filmes)}


def impedimentos_reais(m):
    """O que impede uma campanha marcada como real de valer, conferido de novo em cada verificacao: a marca `teste` e um campo
    editavel do manifesto, e sozinha nao prova nada (revisao Codex 2026-11-01)."""
    out = []
    if m.get("origem", {}).get("tipo") != "newgame":
        out.append("a campanha nao comeca do NEW GAME (origem %r)" % m.get("origem", {}).get("tipo"))
    if sem_opcao(m.get("args", ""), "--camera") != ARGS.split() or m.get("camera") not in CAMERAS or \
            "--camera %s" % m.get("camera") not in m.get("args", ""):
        out.append("argumentos fora do padrao de uma campanha real: %r" % m.get("args"))
    if bool(m.get("estado_sem_camera")) != (m.get("camera") == "livre"):
        out.append("comparacao de estado incoerente com a camera %r" % m.get("camera"))
    if m.get("relogio") != gv.RELOGIO:
        out.append("relogio fora do padrao: %r" % m.get("relogio"))
    for s in m.get("sessoes", []):
        t = s.get("teste") or {}
        if t.get("base") or t.get("controle") or os.path.exists(os.path.join(sessao_dir(m, s["n"]), "base.txt")):
            out.append("sessao %d usou roteiro ou controle de teste" % s["n"])
    return out


def verificar(cid=None, so=None, log=print):
    m = manifesto(cid) if cid else (campanhas() or [None])[-1]
    if m is None:
        raise Erro("nenhuma campanha")
    recuperar(m, log)
    falhas = [] if m.get("teste") else impedimentos_reais(m)
    ruins = conferir_selos(m)
    for n, dif in ruins:
        falhas.append("sessao %d: arquivos mudaram depois da gravacao: %s" % (n, ", ".join(dif[:6])))
    try:
        conferir_instalacao(m)
    except Erro as e:
        falhas.append(str(e))
    sess = [s for s in m["sessoes"] if s.get("estado") in ("encerrada", "interrompida")]
    if not sess:
        falhas.append("nenhuma sessao gravada")
    resultados = {}
    if not falhas:
        for s in sess:
            if so is None or s["n"] == so:
                resultados[s["n"]] = reproduzir_sessao(m, s, log)
    # a cadeia: cada sessao comeca dos saves de fim REPRODUZIDOS da anterior; a primeira, do cartao vazio (NEW GAME)
    cadeia = []
    for i, s in enumerate(sess):
        r = resultados.get(s["n"])
        if r is None:
            continue
        if r["veredito"] == "DIVERGENTE":
            falhas.append("sessao %d divergente%s" % (s["n"], "" if r["primeira_divergencia"] is None else " no passo %d" % r["primeira_divergencia"]))
        if i == 0:
            if m["origem"]["tipo"] == "newgame" and s["saves_inicio"]:
                falhas.append("a primeira sessao nao comeca de um cartao vazio")
        else:
            ant = resultados.get(sess[i - 1]["n"])
            if ant and s["saves_inicio"] != ant["saves_fim_reproduzidos"]:
                falhas.append("sessao %d nao comeca dos saves reproduzidos da sessao %d" % (s["n"], sess[i - 1]["n"]))
            cadeia.append(s["n"])
        if s.get("programa_identidade") != m["programa"]["identidade"]:
            falhas.append("sessao %d gravada com outro programa" % s["n"])
    completa = so is None and len(resultados) == len(sess)
    creditos = bool(sess) and completa and resultados[sess[-1]["n"]]["creditos"]
    lidos = sorted(set(a for r in resultados.values() for a in r["arquivos_lidos"]))
    if falhas:  # vem antes: arquivo adulterado nem chega a ser reproduzido, e nao e so "nao verificada"
        veredito = "REPROVADA"
    elif not completa:
        veredito = "PARCIAL (so a sessao %s)" % so if so else "NAO VERIFICADA"
    elif m.get("teste"):
        veredito = "TESTE REPRODUZIDO (campanha de teste: nao vale para o D-N8-4)"
    elif creditos:
        veredito = "CAMPANHA REPRODUZIDA ATE OS CREDITOS"
    else:
        veredito = "REPRODUZIDA, MAS INCOMPLETA (sem os creditos)"
    rel = {"quando": agora(), "campanha": m["id"], "veredito": veredito, "falhas": falhas, "creditos": creditos,
           "programa": m["programa"], "dados": {"identidade": m["dados"]["identidade"], "manifesto_sha256": m["dados"]["manifesto_sha256"]},
           "sessoes": [{"n": s["n"], "estado": s["estado"], "progresso": s.get("progresso"), "reproducao": resultados.get(s["n"], {}).get("veredito"),
                        "primeira_divergencia": resultados.get(s["n"], {}).get("primeira_divergencia")} for s in sess],
           "cobertura": {"arquivos_do_disco_lidos": len(lidos), "arquivos_do_disco_total": total_disco(m),
                         "filmes": [x for s in sess for x in resultados.get(s["n"], {}).get("filmes", [])]}}
    escrever_json(os.path.join(pasta(m["id"]), "relatorio.json"), rel)
    with open(os.path.join(pasta(m["id"]), "relatorio.md"), "w", encoding="utf-8") as f:
        f.write(relatorio_md(m, rel))
    if completa:
        m["verificacao"] = {"quando": rel["quando"], "veredito": veredito}
        m["concluida"] = veredito.startswith("CAMPANHA REPRODUZIDA")
        salvar(m)
    log("== %s: %s" % (m["id"], veredito))
    for x in falhas:
        log("  " + x)
    return 0 if completa and not falhas and (creditos or m.get("teste")) else 1


def referencia_logica(m, s, a, log):
    """O estado logico da sessao pelo programa fixado (sem o objeto camera do SDK): quem compara duas edicoes nao pode exigir
    igualdade da camera. Numa campanha Livre e o proprio gravado; numa Original vem de uma reproducao, guardada por programa."""
    d = sessao_dir(m, s["n"])
    if m.get("estado_sem_camera"):
        return {"estado": a["gravado"], "operacoes": s.get("operacoes", []), "saves": s.get("saves_fim")}
    r = os.path.join(d, "referencia_logica")
    marca = os.path.join(r, "referencia.json")
    if os.path.isfile(marca) and os.path.isfile(os.path.join(r, "estado.tsv")):
        x = ler_json(marca)
        if x.get("programa") == m["programa"]["identidade"]:
            return {"estado": gv.ler_estado(os.path.join(r, "estado.tsv")), "operacoes": x["operacoes"], "saves": x["saves"]}
    log("sessao %d: referencia logica pelo programa fixado" % s["n"])
    x = rodar(exe(m), m["dados"]["pasta"], m["args"].split(), a["entrada"], os.path.join(d, "saves_inicio"), r, a["vblank"], True)
    if x["rc"] != 0:
        raise Erro("a referencia logica da sessao %d terminou com rc=%d" % (s["n"], x["rc"]))
    escrever_json(marca, {"programa": m["programa"]["identidade"], "operacoes": x["operacoes"], "saves": x["saves"]})
    return x


def reproduzir_em(cid, instalacao, rotulo=None, camera=None, render=None, log=print):
    """A cadeia gravada reproduzida em outro programa (outra edicao, versao ou opcoes de apresentacao). Compara estado logico,
    VBlanks, LOAD/SAVE e saves contra o programa fixado. So informa: nunca aprova nem reprova a campanha."""
    m = manifesto(cid)
    recuperar(m, log)
    ruins = conferir_selos(m)
    if ruins:
        raise Erro("arquivos da campanha mudaram depois da gravacao: %s" % "; ".join("sessao %d: %s" % (n, ", ".join(d[:4])) for n, d in ruins))
    conferir_instalacao(m)
    inst = os.path.abspath(instalacao)
    alvo_exe, alvo_dados = os.path.join(inst, "BlueStinger.exe"), os.path.join(inst, "data")
    if not os.path.isfile(alvo_exe) or not os.path.isfile(os.path.join(alvo_dados, "disc.manifest")):
        raise Erro("instalacao invalida (sem BlueStinger.exe ou data\\disc.manifest): %s" % inst)
    if sha(os.path.join(alvo_dados, "disc.manifest")) != m["dados"]["manifesto_sha256"]:
        raise Erro("a instalacao %s foi feita de outro disco: a gravacao nao vale nela" % inst)
    camera = camera or m.get("camera", "original")
    if camera not in CAMERAS:
        raise Erro("camera invalida: %r" % camera)
    args = sem_opcao(m["args"], "--camera") + ["--camera", camera]
    if render:
        args = sem_opcao(" ".join(args), "--render") + ["--render", render]
    rotulo = rotulo or datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", rotulo):
        raise Erro("rotulo invalido: %r" % rotulo)
    base = os.path.join(pasta(m["id"]), "outros", rotulo)
    sess = [s for s in m["sessoes"] if s.get("estado") in ("encerrada", "interrompida")]
    if not sess:
        raise Erro("nenhuma sessao gravada")
    falhas, sessoes, ini, creditos = [], [], None, False
    for s in sess:
        d, a = sessao_dir(m, s["n"]), alcance(m, s)
        for p in a["problemas"]:
            if "continuidade" in p:
                falhas.append("sessao %d: %s" % (s["n"], p))
        ref = referencia_logica(m, s, a, log)
        r = os.path.join(base, "sessao_%03d" % s["n"])
        x = rodar(alvo_exe, alvo_dados, args, a["entrada"], ini or os.path.join(d, "saves_inicio"), r, a["vblank"], True)
        ini = os.path.join(r, "saves")  # a cadeia no programa novo segue pelos saves que ele mesmo produziu
        diverg, vbdif = comparar_estado(ref["estado"], x["estado"], a["passos"])
        fim = a["fim"]
        ops_ok = [o for o in x["operacoes"] if o[0] <= fim] == [list(o) for o in ref["operacoes"] if o[0] <= fim]
        saves_ok = x["saves"] == ref["saves"]
        for ok, texto in ((x["rc"] == 0, "terminou com rc=%d" % x["rc"]), (diverg is None, "estado diverge no passo %s" % diverg),
                          (vbdif == 0, "%d passo(s) em VBlank diferente" % vbdif), (ops_ok, "LOAD/SAVE diferentes"), (saves_ok, "saves diferentes")):
            if not ok:
                falhas.append("sessao %d: %s" % (s["n"], texto))
        creditos = x["cobertura"]["creditos"]
        sessoes.append({"n": s["n"], "passos": len(a["passos"]), "primeira_divergencia": diverg, "vblank_diferente": vbdif,
                        "load_save_iguais": ops_ok, "saves_iguais": saves_ok, "segundos": x["segundos"], "creditos": x["cobertura"]["creditos"]})
        log("sessao %d em %s: estado %s; LOAD/SAVE %s; saves %s; %d s" % (
            s["n"], rotulo, "igual" if diverg is None else "diverge no passo %d" % diverg, "iguais" if ops_ok else "DIFERENTES",
            "iguais" if saves_ok else "DIFERENTES", x["segundos"]))
    veredito = "DIVERGENTE" if falhas else "REPRODUZIDA EM OUTRO PROGRAMA"
    rel = {"quando": agora(), "campanha": m["id"], "rotulo": rotulo, "veredito": veredito, "falhas": falhas, "creditos": creditos,
           "alvo": {"instalacao": inst, "exe_identidade": ei.identidade(alvo_exe), "exe_sha256": sha(alvo_exe), "args": " ".join(args)},
           "programa_fixado": m["programa"]["exe_identidade"], "sessoes": sessoes,
           "nota": "comparacao entre programas: nao aprova nem reprova a campanha"}
    os.makedirs(base, exist_ok=True)
    escrever_json(os.path.join(base, "relatorio.json"), rel)
    log("== %s em %s: %s" % (m["id"], rotulo, veredito))
    for x in falhas:
        log("  " + x)
    return 0 if not falhas else 1


def total_disco(m):
    p = os.path.join(m["dados"]["pasta"], "disc.manifest")
    try:
        with open(p, encoding="utf-8", errors="replace") as f:
            return sum(1 for l in f if l.strip() and not l.startswith("#"))
    except OSError:
        return None


def relatorio_md(m, rel):
    l = ["# Campanha %s" % m["id"], "", "**Veredito:** %s" % rel["veredito"], "",
         "Programa fixado: `%s` (%s); dados `%s`." % (m["programa"]["exe_identidade"], m["programa"]["versao"], m["dados"]["identidade"]), "",
         "| Sessao | Estado | Gravado ate o passo | Ultimo save no passo | Passos depois do ultimo save | Reproducao |", "|---|---|---|---|---|---|"]
    for s in rel["sessoes"]:
        p = s["progresso"] or {}
        l.append("| %d | %s | %s | %s | %s | %s%s |" % (s["n"], s["estado"], p.get("gravado_ate_passo"), p.get("ultimo_save_no_passo"),
                                                     p.get("passos_depois_do_ultimo_save"), s["reproducao"],
                                                     "" if s["primeira_divergencia"] is None else " (passo %d)" % s["primeira_divergencia"]))
    c = rel["cobertura"]
    l += ["", "Creditos alcancados na reproducao: **%s**." % ("sim" if rel["creditos"] else "nao"),
          "Arquivos do disco lidos na campanha: %s de %s." % (c["arquivos_do_disco_lidos"], c["arquivos_do_disco_total"]),
          "Filmes: %s." % (", ".join("%s (passo %s, %s)" % (x["filme"], x["passo"], x["fim"] or "sem fim na reproducao") for x in c["filmes"]) or "nenhum"), ""]
    if rel["falhas"]:
        l += ["Falhas:", ""] + ["- " + x for x in rel["falhas"]] + [""]
    return "\n".join(l)


def status(cid=None, log=print):
    m = manifesto(cid) if cid else (campanhas() or [None])[-1]
    if m is None:
        log("nenhuma campanha em %s" % RAIZ)
        return 0
    log("campanha %s%s, criada %s; programa %s; %d sessao(oes)%s" % (
        m["id"], " (TESTE)" if m.get("teste") else "", m["criada"], m["programa"]["versao"], len(m["sessoes"]),
        "; verificacao: %s (%s)" % (m["verificacao"]["veredito"], m["verificacao"]["quando"]) if m.get("verificacao") else "; ainda nao verificada"))
    for s in m["sessoes"]:
        p = s.get("progresso") or {}
        log("  sessao %d: %s; gravado ate o passo %s; ultimo save no passo %s; %s passo(s) depois do ultimo save%s" % (
            s["n"], s["estado"], p.get("gravado_ate_passo", "?"), p.get("ultimo_save_no_passo"), p.get("passos_depois_do_ultimo_save", "?"),
            " -- " + s["motivo"] if s.get("motivo") else ""))
    return 0


# ---------------------------------------------------------------- interface

def gui():
    import threading
    import tkinter as tk
    from tkinter import messagebox, scrolledtext

    w = tk.Tk()
    w.title("Jogar e Gravar Campanha - Blue Stinger")
    estado = tk.StringVar(value="")
    tk.Label(w, textvariable=estado, font=("Segoe UI", 12, "bold"), anchor="w", justify="left").pack(fill="x", padx=10, pady=(10, 4))
    caixa = scrolledtext.ScrolledText(w, width=96, height=18, font=("Consolas", 9))
    caixa.pack(fill="both", expand=True, padx=10)
    botoes = tk.Frame(w)
    botoes.pack(fill="x", padx=10, pady=8)
    ocupado = {"v": False}

    def log(t):
        w.after(0, lambda: (caixa.insert("end", t + "\n"), caixa.see("end")))

    def atualizar():
        if ocupado["v"]:
            return
        cs = [c for c in campanhas() if not c.get("teste")]
        if not cs:
            estado.set("Nenhuma campanha. 'Jogar e gravar' cria uma nova (NEW GAME).")
        else:
            m = cs[-1]
            ult = m["sessoes"][-1] if m["sessoes"] else None
            estado.set("Campanha %s: %d sessao(oes)%s%s" % (
                m["id"], len(m["sessoes"]), "" if not ult else "; ultima: %s ate o passo %s" % (ult["estado"], (ult.get("progresso") or {}).get("gravado_ate_passo")),
                "; " + m["verificacao"]["veredito"] if m.get("verificacao") else ""))

    def em_fundo(f):
        if ocupado["v"]:
            return
        ocupado["v"] = True
        for b in botoes.winfo_children():
            b.configure(state="disabled")

        def run():
            try:
                f()
            except Erro as e:
                log("ERRO: %s" % e)
                w.after(0, lambda: messagebox.showerror("Campanha", str(e)))
            except Exception as e:  # noqa: BLE001 - mostra qualquer falha em vez de fechar a janela
                log("ERRO inesperado: %r" % e)
            finally:
                ocupado["v"] = False
                w.after(0, lambda: ([b.configure(state="normal") for b in botoes.winfo_children()], atualizar()))
        threading.Thread(target=run, daemon=True).start()

    def jogar_():
        def iniciou(m, s, p):
            entrada = os.path.join(sessao_dir(m, s["n"]), "entrada.txt")

            def pulso():
                if p.poll() is not None:
                    return
                passo = "?"
                try:
                    with open(entrada, encoding="utf-8") as f:
                        for l in f:
                            if l[:1].isdigit():
                                passo = l.split(" ", 1)[0]
                except OSError:
                    pass
                estado.set("● GRAVANDO - campanha %s, sessao %d (ultima entrada no passo %s). Feche o jogo para terminar." % (m["id"], s["n"], passo))
                w.after(1000, pulso)
            w.after(0, pulso)
        em_fundo(lambda: jogar(log=log, ao_iniciar=iniciou))

    def nova_():
        if messagebox.askyesno("Campanha", "Comecar uma campanha nova (NEW GAME)? As anteriores continuam guardadas."):
            em_fundo(lambda: nova(log=log))

    def verificar_():
        em_fundo(lambda: verificar(log=log))

    def pasta_():
        cs = [c for c in campanhas() if not c.get("teste")]
        os.startfile(pasta(cs[-1]["id"]) if cs else RAIZ) if os.path.isdir(RAIZ) else None

    tk.Button(botoes, text="Jogar e gravar (continuar)", command=jogar_, width=26).pack(side="left")
    tk.Button(botoes, text="Nova campanha", command=nova_, width=16).pack(side="left", padx=6)
    tk.Button(botoes, text="Verificar (reproduzir tudo)", command=verificar_, width=24).pack(side="left")
    tk.Button(botoes, text="Abrir pasta", command=pasta_, width=12).pack(side="left", padx=6)
    log("Campanhas em %s (fora do repositorio). Jogue normalmente: salve no jogo, feche a janela para terminar a sessao.\n"
        "A prova e a reproducao: 'Verificar' reproduz todas as sessoes e so aprova a cadeia completa ate os creditos." % RAIZ)
    atualizar()
    w.mainloop()
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="Monitor de Campanha (D-N8-4)")
    sub = ap.add_subparsers(dest="cmd")
    a = sub.add_parser("nova")
    a.add_argument("--instalacao")
    a.add_argument("--teste", action="store_true")
    a.add_argument("--save")
    a.add_argument("--id")
    a.add_argument("--args")
    a.add_argument("--camera", default="original", choices=CAMERAS)
    a = sub.add_parser("jogar")
    a.add_argument("id", nargs="?")
    a.add_argument("--teste-base")
    a.add_argument("--teste-ate")
    a.add_argument("--teste-controle")
    a = sub.add_parser("status")
    a.add_argument("id", nargs="?")
    a = sub.add_parser("verificar")
    a.add_argument("id", nargs="?")
    a.add_argument("--sessao", type=int)
    a = sub.add_parser("reproduzir-em")
    a.add_argument("id")
    a.add_argument("--instalacao", required=True)
    a.add_argument("--rotulo")
    a.add_argument("--camera", choices=CAMERAS)
    a.add_argument("--render", choices=("software", "nativo"))
    sub.add_parser("lista")
    sub.add_parser("gui")
    o = ap.parse_args(argv)
    try:
        if o.cmd in (None, "gui"):
            return gui()
        if o.cmd == "nova":
            nova(o.instalacao, o.teste, o.save, o.id, o.args, camera=o.camera)
            return 0
        if o.cmd == "jogar":
            _, s = jogar(o.id, o.teste_base, o.teste_ate, o.teste_controle)
            return 0 if s["estado"] == "encerrada" else 3
        if o.cmd == "status":
            return status(o.id)
        if o.cmd == "verificar":
            return verificar(o.id, o.sessao)
        if o.cmd == "reproduzir-em":
            return reproduzir_em(o.id, o.instalacao, o.rotulo, o.camera, o.render)
        if o.cmd == "lista":
            for m in campanhas():
                print("%s%s: %d sessao(oes), %s" % (m["id"], " (teste)" if m.get("teste") else "", len(m["sessoes"]),
                                                    m.get("verificacao", {}).get("veredito", "nao verificada")))
            return 0
    except Erro as e:
        print("erro: %s" % e, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
