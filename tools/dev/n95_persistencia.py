"""N9.5 fase 5A: a proporcao no menu e no graficos.toml (decisoes D5-1, D5-2 e D5-4 do mantenedor, 2026-11-10).

Cada caso roda o executavel numa pasta propria dentro de --saida (copia do exe, instalacao.toml da edicao, graficos.toml do
caso e um cartao vazio; nada da pasta do usuario e tocado), com o controle virtual (BS_CONTROLE_VIRTUAL: so a janela do
Blue Stinger recebe os botoes, nada de entrada global nem captura da area de trabalho) e confere o log e o arquivo depois.

  P1 Remastered, sem graficos.toml: Configuracoes > Graficos > Proporcao, Direita. O 16:9 vale na hora (sem o aviso de
     confirmacao), o arquivo passa a "16:9"; numa segunda execucao, sem tocar em nada, o jogo comeca em 16:9.
  P2 Remastered com o arquivo antigo `proporcao = "automatico"` (D5-1): comeca em 4:3, com o aviso; a mesma troca grava
     "16:9" e o "automatico" some do arquivo.
  P3 Remastered com o software e "16:9" no arquivo: a linha Proporcao fica indisponivel (Direita nao muda nada) e o arquivo
     guarda o 16:9; de volta ao Vulkan pelo menu, o quadro largo liga. Original com "16:9" no arquivo (D5-4): continua
     4:3; uma mudanca do renderizador pelo menu nao reescreve a proporcao do arquivo.

  P4 --proporcao (so nesta execucao, revisao Codex 20261110-004711): uma gravacao por outra opcao mantem a proporcao do
     arquivo nos dois sentidos; mudada no menu, a proporcao vai para o arquivo.
  P5 o recuo real para o SDL (BS_APRESENTACAO_FALHAR, exe de validacao): o quadro largo desliga e o 16:9 do arquivo fica.

  python tools/dev/n95_persistencia.py --exe build/msvc-release/app/bluestinger.exe --data <data> --saida tmp/<pasta>
         [--casos P1,P2,P3,P4,P5] [--exe-validacao build/msvc-validacao/app/bluestinger.exe]
Saida 1 se algum criterio falhar. A janela do jogo abre por ~45 s em cada execucao.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

# a Remastered: o menu inicial nativo aparece no "PRESS START" (~25 s, como no test_edicao E8); sem perfil, Continuar e
# Carregar ficam desativados: um Down leva a Configuracoes. Pagina principal: Graficos (0). Graficos: Renderizador (0),
# Resolucao interna (1), Escala (2), Proporcao (3).
REM_INICIO = 32000
ATE_GRAFICOS = ["dpdown", "a", "a"]
PROPORCAO = ["dpdown"] * 3
SAIR_MENU = ["b", "b"]

GRAFICOS = """preset = "personalizado"
[tela]
modo = "janela"
largura = 1280
altura = 720
proporcao = "{prop}"
renderizador = "{rend}"
"""


def roteiro(botoes, inicio, fim_ms=None):
    linhas, t = ["0 conectar"], inicio
    for b in botoes:
        linhas += ["%d %s 1" % (t, b), "%d %s 0" % (t + 100, b)]
        t += 500
    linhas.append("%d sair" % (fim_ms if fim_ms else t + 3000))
    return "\n".join(linhas) + "\n"


def rodar(a, d, nome, botoes, inicio, fim_ms=None, args=(), env_extra=None, exe=None):
    pad = os.path.join(d, "pad-%s.txt" % nome)
    with open(pad, "w", encoding="utf-8") as f:
        f.write(roteiro(botoes, inicio, fim_ms))
    env = {k: v for k, v in os.environ.items() if not k.startswith("BS_")}
    env.update({"BS_CONTROLE_VIRTUAL": pad, "BS_RELOGIO_FIXO": "1700000000"})
    env.update(env_extra or {})
    log = os.path.join(d, "jogo-%s.txt" % nome)
    with open(log, "w", encoding="utf-8") as out:
        p = subprocess.Popen([os.path.join(d, "bluestinger.exe"), "--data", a.data, "--saves-pasta", os.path.join(d, "saves"),
                              "--apresentacao", "vulkan"] + list(args), stdout=out, stderr=subprocess.STDOUT, env=env, cwd=d)
        try:
            rc = p.wait(150)
        except subprocess.TimeoutExpired:  # so o processo deste caso
            p.kill()
            rc = -1
    texto = open(log, encoding="utf-8", errors="replace").read()
    g = os.path.join(d, "graficos.toml")
    toml = open(g, encoding="utf-8").read() if os.path.exists(g) else ""
    return rc, texto, toml


def preparar(a, nome, edicao, graficos=None, exe=None):
    d = os.path.join(a.saida, nome)
    if os.path.exists(d):
        shutil.rmtree(d)
    os.makedirs(os.path.join(d, "saves"))
    shutil.copy2(exe or a.exe, os.path.join(d, "bluestinger.exe"))
    if edicao == "remastered":
        open(os.path.join(d, "instalacao.toml"), "w").write('[edicao]\nperfil = "remastered"\n')
    if graficos:
        open(os.path.join(d, "graficos.toml"), "w").write(graficos)
    return d


def depois(texto, marca):
    i = texto.find(marca)
    return texto[i:] if i >= 0 else ""


def p1(a, ok):
    d = preparar(a, "P1", "remastered")
    rc, t, toml = rodar(a, d, "1", ATE_GRAFICOS + PROPORCAO + ["dpright"] + SAIR_MENU, REM_INICIO)
    m = depois(t, "[remaster] menu inicial")
    ok("P1 a troca rodou (rc=0, menu inicial)", rc == 0 and bool(m), "rc=%d" % rc)
    ok("P1 16:9 na hora pelo menu", "[menu] proporcao: 16:9 (quadro largo)" in m and "[render] quadro: largo 854x480" in m)
    ok("P1 sem a confirmacao de 15 s (nenhuma volta ao 4:3)", "[menu] proporcao: 4:3" not in m)
    ok("P1 o arquivo grava 16:9", 'proporcao = "16:9"' in toml, toml.replace("\n", " | ")[:200])
    rc, t, toml2 = rodar(a, d, "2", [], 0, 20000)
    ok("P1 a execucao seguinte comeca em 16:9", rc == 0 and "[menu] proporcao: 16:9 (quadro largo)" in t, "rc=%d" % rc)
    ok("P1 sem mudanca, o arquivo fica igual", toml2 == toml)


def p2(a, ok):
    d = preparar(a, "P2", "remastered", GRAFICOS.format(prop="automatico", rend="vulkan"))
    rc, t, toml = rodar(a, d, "1", ATE_GRAFICOS + PROPORCAO + ["dpright"] + SAIR_MENU, REM_INICIO)
    antes = t[:max(0, t.find("[remaster] menu inicial"))]
    ok("P2 o arquivo antigo vale 4:3, com o aviso", "proporcao automatica removida temporariamente" in t and
       "[render] quadro: largo" not in antes, "rc=%d" % rc)
    ok("P2 a troca grava 16:9 e o automatico some", rc == 0 and 'proporcao = "16:9"' in toml and "automatico" not in toml,
       toml.replace("\n", " | ")[:200])


def p3(a, ok):
    d = preparar(a, "P3", "remastered", GRAFICOS.format(prop="16:9", rend="software"))
    # Direita na Proporcao (indisponivel: nada), sobe ao Renderizador e troca para o Vulkan
    rc, t, toml = rodar(a, d, "1", ATE_GRAFICOS + PROPORCAO + ["dpright"] + ["dpup"] * 3 + ["dpright"] + SAIR_MENU, REM_INICIO)
    m = depois(t, "[remaster] menu inicial")
    # o fim da parte com o software: a linha do menu que aplica o Vulkan (a thread do jogo registra o quadro largo antes da
    # troca do renderizador, no mesmo pedido; o software nunca desenha o largo: render_core so alarga no modo nativo)
    troca = t.find("[menu] proporcao: 16:9 (quadro largo)")
    ok("P3 com o software, a imagem fica em 4:3", rc == 0 and troca > 0 and "[render] quadro: largo" not in t[:troca], "rc=%d" % rc)
    ok("P3 a Direita na linha indisponivel nao muda nada (uma troca so, a do Vulkan)", m.count("[menu] proporcao:") == 1 and
       m.count("[render] renderizador:") == 1)
    ok("P3 de volta ao Vulkan, o 16:9 guardado vale", "[render] renderizador: Vulkan" in m and "[menu] proporcao: 16:9 (quadro largo)" in m)
    ok("P3 o arquivo guardou o 16:9 e o Vulkan", 'proporcao = "16:9"' in toml and 'renderizador = "vulkan"' in toml,
       toml.replace("\n", " | ")[:200])
    # a Original (D5-4): 4:3 fixo; o menu troca o renderizador (primeira linha), a proporcao do arquivo nao muda
    d = preparar(a, "P3-original", "original", GRAFICOS.format(prop="16:9", rend="vulkan"))
    rc, t, toml = rodar(a, d, "1", ["back", "a", "a", "b", "b"], 6000)
    ok("P3 Original: 4:3 com o arquivo pedindo 16:9", rc == 0 and "[render] quadro: largo" not in t, "rc=%d" % rc)
    ok("P3 Original: o menu gravou o renderizador e manteve a proporcao do arquivo",
       "preferencias gravadas" in t and 'renderizador = "software"' in toml and 'proporcao = "16:9"' in toml,
       toml.replace("\n", " | ")[:200])


def p4(a, ok):
    """--proporcao vale so nesta execucao: uma gravacao provocada por outra opcao (o renderizador, ida e volta) mantem a
    proporcao do arquivo, nos dois sentidos; uma mudanca da propria proporcao no menu passa a valer no arquivo."""
    vai_volta = ATE_GRAFICOS + ["a", "a"] + SAIR_MENU  # Renderizador: software e de volta ao Vulkan
    for nome, arq, cmd in (("P4a", "4:3", "16:9"), ("P4b", "16:9", "4:3")):
        d = preparar(a, nome, "remastered", GRAFICOS.format(prop=arq, rend="vulkan"))
        rc, t, toml = rodar(a, d, "1", vai_volta, REM_INICIO, args=["--proporcao", cmd])
        largo = "[menu] proporcao: 16:9 (quadro largo)" in t
        ok("%s --proporcao %s com %s no arquivo: a imagem segue a linha de comando" % (nome, cmd, arq), rc == 0 and largo == (cmd == "16:9"),
           "rc=%d" % rc)
        ok("%s a outra opcao foi gravada e a proporcao do arquivo ficou %s" % (nome, arq),
           "preferencias gravadas" in t and 'renderizador = "vulkan"' in toml and ('proporcao = "%s"' % arq) in toml,
           toml.replace("\n", " | ")[:200])
    d = preparar(a, "P4c", "remastered", GRAFICOS.format(prop="4:3", rend="vulkan"))
    rc, t, toml = rodar(a, d, "1", ATE_GRAFICOS + PROPORCAO + ["dpright", "dpright"] + SAIR_MENU, REM_INICIO, args=["--proporcao", "16:9"])
    ok("P4c mudada no menu, a proporcao deixa de ser da linha de comando e vai para o arquivo", rc == 0 and 'proporcao = "16:9"' in toml,
       toml.replace("\n", " | ")[:200])


def p5(a, ok):
    """O recuo real da apresentacao Vulkan para o SDL (BS_APRESENTACAO_FALHAR, so no exe de validacao): o quadro largo deixa de
    existir, a imagem volta ao 4:3 e uma gravacao depois disso (o renderizador) mantem o 16:9 do arquivo."""
    if not a.exe_validacao:
        ok("P5 precisa do --exe-validacao (BS_APRESENTACAO_FALHAR so existe na validacao)", False)
        return
    d = preparar(a, "P5", "remastered", GRAFICOS.format(prop="16:9", rend="vulkan"), exe=a.exe_validacao)
    rc, t, toml = rodar(a, d, "1", ATE_GRAFICOS + ["a"] + SAIR_MENU, REM_INICIO, env_extra={"BS_APRESENTACAO_FALHAR": "300"})
    recuo = t.find("[menu] proporcao: 4:3 (a apresentacao SDL nao desenha o quadro largo)")
    ok("P5 o recuo para o SDL desliga o quadro largo", rc == 0 and "[menu] proporcao: 16:9 (quadro largo)" in t and recuo > 0, "rc=%d" % rc)
    ok("P5 depois do recuo, o menu grava o renderizador e mantem o 16:9 do arquivo",
       recuo > 0 and "preferencias gravadas" in t[recuo:] and 'renderizador = "software"' in toml and 'proporcao = "16:9"' in toml,
       toml.replace("\n", " | ")[:200])
    ok("P5 o quadro largo nao volta depois do recuo", recuo > 0 and "[menu] proporcao: 16:9" not in t[recuo:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--saida", required=True, help="pasta dos casos (dentro de tmp/)")
    ap.add_argument("--casos", default="P1,P2,P3,P4,P5")
    ap.add_argument("--exe-validacao", help="exe de validacao (para o P5)")
    a = ap.parse_args()
    a.exe, a.data, a.saida = os.path.abspath(a.exe), os.path.abspath(a.data), os.path.abspath(a.saida)
    if a.exe_validacao:
        a.exe_validacao = os.path.abspath(a.exe_validacao)
    os.makedirs(a.saida, exist_ok=True)
    falhas = []

    def ok(nome, cond, detalhe=""):
        print("[%s] %s%s" % ("ok" if cond else "FALHA", nome, (" (" + detalhe + ")") if detalhe and not cond else ""))
        if not cond:
            falhas.append(nome)

    for n in a.casos.split(","):
        {"P1": p1, "P2": p2, "P3": p3, "P4": p4, "P5": p5}[n](a, ok)
    print("\n%d falha(s) (%s)" % (len(falhas), a.saida))
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
