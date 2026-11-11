"""N9.6 (mantenedor, 2026-11-11): os presets seguros e as melhorias na escolha manual, conferidos pelo estado que chega ao
renderizador e pelo graficos.toml. Q1-Q6: o pedido aceito pelo render_capture (as linhas "[render] ..." que o game_host
grava ao entregar cada efeito); Q7: o que o Vulkan aplicou, pelos contadores do dispositivo no resumo [render-vk].

Mesmo arranjo do n95_persistencia (pasta propria por caso dentro de --saida, copia do exe, instalacao.toml da edicao,
graficos.toml do caso, cartao vazio; o controle virtual BS_CONTROLE_VIRTUAL so na janela do Blue Stinger, sem entrada global
nem captura da area de trabalho). A janela abre por ~35-60 s em cada execucao.

  Q1 Remastered com `preset = "original"`, `"equilibrado"` e `"alta"`: o Vulkan em uso e nenhum dos tres efeitos entregue ao
     renderizador (sombras aprimoradas, luz das fontes, SSAO); o arquivo nao e reescrito.
  Q2 Remastered com o Alto: no menu, Sombras -> aprimoradas, Iluminacao aprimorada e SSAO ligados: os tres chegam ao
     renderizador; o arquivo vira "personalizado" com as tres escolhas e o resto do Alto (1440p, anisotropica, MSAA 4x).
  Q3 a reabertura (o mesmo arquivo, sem tocar em nada): os tres chegam ao renderizador ja no inicio; o arquivo fica.
  Q4 no menu, os tres desfeitos: o renderizador recebe os tres desligados; o arquivo igual ao do Q2 menos as tres escolhas.
  Q5 a reabertura: nenhum dos tres; o arquivo fica.
  O menu grava os valores ja ajustados aos recursos (clamp_to, desde o N7c): no produto o MSAA, a filtragem e as texturas HD
  ainda sao "em breve" e voltam ao original, entao o Alto gravado pelo menu fica "personalizado" (regra existente).
  Q6 Original com o Alto e as tres escolhas no arquivo: nenhuma chega ao renderizador (a Original fica fiel); o arquivo nao e
     reescrito.
  Q7 a rota passo_load_slot1 no modo espectador ate o corredor (luzes do catalogo e volumes), fechada pelo "sair" do controle
     virtual: o Alto sem nenhum efeito no Vulkan (AO, luz e borda suave em 0 renders); o Alto com as tres escolhas no
     arquivo com as tres aplicadas (> 0); a Original com as tres no arquivo sem nenhuma e sem o catalogo.

  python tools/dev/n96_presets.py --exe build/msvc-release/app/bluestinger.exe --data <data> --cartao <saves> --saida tmp/<pasta>
Saida 1 se algum criterio falhar.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SAIR_MS = 95000  # a rota passo_load_slot1 chega ao corredor (VBlank ~4700) em ~80 s de janela
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n95_persistencia import ATE_GRAFICOS, REM_INICIO, depois, preparar, rodar  # noqa: E402

TELA = '[tela]\nmodo = "janela"\nlargura = 1280\naltura = 720\nrenderizador = "vulkan"\n'
LINHAS = {"sombras": "[render] sombras aprimoradas: ligadas", "luz": "[render] luz das fontes: ligada",
          "ssao": "[render] oclusao ambiente: ligada"}
DESLIGADAS = {"sombras": "[render] sombras aprimoradas: desligadas", "luz": "[render] luz das fontes: desligada",
              "ssao": "[render] oclusao ambiente: desligada"}
# Graficos: Renderizador 0 ... MSAA 8, Sombras 9, Iluminacao aprimorada 10, Texturas HD 11, Efeitos de imagem 12, Voltar 13.
# Efeitos de imagem: Correcao de cor 0 ... Intensidade 13, SSAO 14 (o cursor nao pula linhas inativas).
LIGAR = ATE_GRAFICOS + ["dpdown"] * 9 + ["dpright", "dpdown", "a", "dpdown", "dpdown", "a"] + ["dpdown"] * 14 + ["a", "b", "b", "b"]
DESFAZER = ATE_GRAFICOS + ["dpdown"] * 9 + ["dpleft", "dpdown", "a", "dpdown", "dpdown", "a"] + ["dpdown"] * 14 + ["a", "b", "b", "b"]


def chave(toml, k):
    m = re.search(r'^%s = "?([^"\n]*)"?$' % re.escape(k), toml, re.M)
    return m.group(1) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--saida", required=True)
    ap.add_argument("--cartao", required=True, help="pasta de saves sintetica com o slot 1 (a mesma do n96_luz)")
    a = ap.parse_args()
    a.exe, a.data, a.saida = os.path.abspath(a.exe), os.path.abspath(a.data), os.path.abspath(a.saida)
    a.cartao = os.path.abspath(a.cartao)
    os.makedirs(a.saida, exist_ok=True)
    falhas = []

    def ok(nome, cond, det=""):
        print("[%s] %s %s" % ("ok" if cond else "FALHA", nome, det), flush=True)
        if not cond:
            falhas.append(nome)

    def efeitos(t):
        return sorted(k for k, l in LINHAS.items() if l in t)

    def vulkan(t):
        return "[render-vk]" in t and "[render] Vulkan:" in t

    # Q1: os tres presets na Remastered
    for p in ("original", "equilibrado", "alta"):
        g = 'preset = "%s"\n' % p + TELA
        d = preparar(a, "Q1-" + p, "remastered", g)
        rc, t, toml = rodar(a, d, "1", [], REM_INICIO, fim_ms=REM_INICIO)
        ok("Q1 %s: Vulkan e nenhum efeito pedido ao render_capture" % p, rc == 0 and vulkan(t) and not efeitos(t),
           "rc %d, efeitos %s" % (rc, efeitos(t)))
        ok("Q1 %s: o arquivo fica" % p, toml == g)

    # Q2-Q5: o Alto, a escolha manual, a reabertura e a volta
    g = 'preset = "alta"\n' + TELA
    d = preparar(a, "Q2", "remastered", g)
    rc, t, toml = rodar(a, d, "2", LIGAR, REM_INICIO)
    m = depois(t, "[remaster] menu inicial")
    ok("Q2 antes do menu: nenhum efeito", not efeitos(t[:len(t) - len(m)]) if m else False)
    ok("Q2 o menu pede os tres ao render_capture", rc == 0 and efeitos(m) == ["luz", "sombras", "ssao"], "rc %d, %s" % (rc, efeitos(m)))
    # o resto do Alto como o menu o grava pelas regras existentes (clamp_to desde o N7c): o que o produto ainda nao tem (MSAA,
    # filtragem, texturas HD: "em breve") volta ao original; o resto fica
    resto = {"resolucao": "1440p", "anisotropia": "16", "nitidez": "0.25", "cor": "true", "contraste": "1.1", "saturacao": "1.15",
             "msaa": "1", "filtragem": "original", "texturas_hd": "false"}
    vals = {k: chave(toml, k) for k in list(resto) + ["preset", "sombras", "iluminacao", "ssao"]}
    ok("Q2 o arquivo: personalizado com as tres escolhas e o resto do Alto",
       vals["preset"] == "personalizado" and vals["sombras"] == "aprimoradas" and vals["iluminacao"] == "true"
       and vals["ssao"] == "true" and all(vals[k] == v for k, v in resto.items()), str(vals))
    antes = toml
    feito = toml
    rc, t, toml = rodar(a, d, "3", [], REM_INICIO, fim_ms=REM_INICIO)
    pos = t.find("[remaster] menu inicial")  # revisao Codex 20261111-073225: a marca tem de existir
    ok("Q3 reabertura: os tres pedidos ja no inicio", rc == 0 and pos >= 0 and efeitos(t[:pos]) == ["luz", "sombras", "ssao"],
       "rc %d, marca %s, %s" % (rc, pos >= 0, efeitos(t[:pos]) if pos >= 0 else "-"))
    ok("Q3 o arquivo fica", toml == antes)
    rc, t, toml = rodar(a, d, "4", DESFAZER, REM_INICIO)
    m = depois(t, "[remaster] menu inicial")
    des = sorted(k for k, l in DESLIGADAS.items() if l in m)
    ok("Q4 o menu desfaz os tres no render_capture", rc == 0 and des == ["luz", "sombras", "ssao"], "rc %d, %s" % (rc, des))
    # o arquivo inteiro igual ao do Q2 menos as tres escolhas (o preset continua "personalizado": o Alto gravado pelo menu ja
    # tem o MSAA, a filtragem e as texturas HD no original, como acima)
    esperado = feito.replace('sombras = "aprimoradas"', 'sombras = "original"').replace("iluminacao = true", "iluminacao = false")
    esperado = esperado.replace("\nssao = true", "\nssao = false")
    ok("Q4 o arquivo: as tres escolhas desfeitas e nada mais", toml == esperado and chave(toml, "sombras") == "original",
       "preset %s" % chave(toml, "preset"))
    antes = toml
    rc, t, toml = rodar(a, d, "5", [], REM_INICIO, fim_ms=REM_INICIO)
    ok("Q5 reabertura: nenhum efeito", rc == 0 and vulkan(t) and not efeitos(t), "rc %d, %s" % (rc, efeitos(t)))
    ok("Q5 o arquivo fica", toml == antes)

    # Q6: a Original com tudo pedido no arquivo
    g = 'preset = "alta"\n[imagem]\nsombras = "aprimoradas"\niluminacao = true\n[efeitos]\nssao = true\n' + TELA
    d = preparar(a, "Q6", "original", g)
    rc, t, toml = rodar(a, d, "6", [], REM_INICIO, fim_ms=REM_INICIO)
    ok("Q6 Original: Vulkan e nenhum efeito pedido ao render_capture", rc == 0 and vulkan(t) and not efeitos(t), "rc %d, %s" % (rc, efeitos(t)))
    ok("Q6 Original: o arquivo fica", toml == g)

    # Q7 (revisao Codex 20261111-073225): o que o Vulkan aplicou, pelos contadores do dispositivo. A rota passo_load_slot1 no
    # modo espectador (BS_INPUT_SCRIPT) ate o corredor, onde ha luzes do catalogo e volumes; o controle virtual so manda "sair"
    # no fim, para o jogo fechar normalmente e gravar o resumo [render-vk].
    def rota(nome, edicao, graf):
        d = preparar(a, nome, edicao, graf)
        shutil.rmtree(os.path.join(d, "saves"))
        shutil.copytree(a.cartao, os.path.join(d, "saves"))
        pad = os.path.join(d, "pad.txt")
        open(pad, "w").write("0 conectar\n%d sair\n" % SAIR_MS)
        env = {k: v for k, v in os.environ.items() if not k.startswith("BS_")}
        env.update({"BS_CONTROLE_VIRTUAL": pad, "BS_RELOGIO_FIXO": "1700000000",
                    "BS_INPUT_SCRIPT": os.path.join(RAIZ, "tools", "dist", "roteiros", "passo_load_slot1.txt")})
        with open(os.path.join(d, "jogo.txt"), "w", encoding="utf-8") as out:
            p = subprocess.Popen([os.path.join(d, "bluestinger.exe"), "--data", a.data, "--saves-pasta", os.path.join(d, "saves"),
                                  "--apresentacao", "vulkan"], stdout=out, stderr=subprocess.STDOUT, env=env, cwd=d)
            try:
                rc = p.wait(SAIR_MS / 1000 + 120)
            except subprocess.TimeoutExpired:  # so o processo deste caso
                p.kill()
                rc = -1
        t = open(os.path.join(d, "jogo.txt"), encoding="utf-8", errors="replace").read()
        m = re.search(r"\[render-vk\] [^\n]*quadros (\d+)[^\n]*AO em (\d+) renders[^\n]*catalogo de materiais: (\d+) envios"
                      r"[^\n]*luz das fontes em (\d+) renders \(ate (\d+) luzes\); borda suave dos volumes em (\d+) renders", t)
        c = {k: int(v) for k, v in zip(("quadros", "ao", "catalogo", "luz", "luzes", "borda"), m.groups())} if m else None
        return rc, c

    alto = 'preset = "alta"\n' + TELA
    tudo = 'preset = "alta"\n[imagem]\nsombras = "aprimoradas"\niluminacao = true\n[efeitos]\nssao = true\n' + TELA
    rc, c = rota("Q7-alto", "remastered", alto)
    ok("Q7 Remastered Alto na rota: o Vulkan sem nenhum efeito", rc == 0 and c is not None and c["quadros"] > 1000
       and c["ao"] == 0 and c["luz"] == 0 and c["borda"] == 0 and c["catalogo"] > 0, "rc %d, %s" % (rc, c))
    rc, c = rota("Q7-manual", "remastered", tudo)
    ok("Q7 Remastered Alto + as tres escolhas: o Vulkan aplica as tres", rc == 0 and c is not None and c["ao"] > 0
       and c["luz"] > 0 and c["luzes"] >= 1 and c["borda"] > 0, "rc %d, %s" % (rc, c))
    rc, c = rota("Q7-original", "original", tudo)
    ok("Q7 Original com as tres no arquivo: o Vulkan sem nenhum efeito nem catalogo", rc == 0 and c is not None
       and c["quadros"] > 1000 and c["ao"] == 0 and c["luz"] == 0 and c["borda"] == 0 and c["catalogo"] == 0, "rc %d, %s" % (rc, c))

    print("%d falha(s) (%s)" % (len(falhas), a.saida))
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
