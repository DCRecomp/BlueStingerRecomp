#!/usr/bin/env python3
"""N9-L (L5): bateria do Builder para Linux, do pacote .tar.gz ao jogo gerado do CDI.

    python3 -I tools/dist/linux/test_builder_linux.py --pacote <BlueStinger-Builder-*.tar.gz> --cdi <imagem.cdi>
        [--saida <pasta>] [--casos B1,B2,...] [--compilador-sistema]
        [--windows-data <data do Preview do Windows>] [--windows-generated <generated/ do Windows>]
        [--game-data <pasta com 1ST_READ.BIN e IP.BIN>] [--pacote-repetido <outro .tar.gz do mesmo commit>]

Casos (os 14 da autorizacao da L5):
  B1  pacote: build limpo (o .tar.gz e o .sha256 batem, SHA256SUMS confere, bs-builder responde, dependencias so da base
      do sistema, glibc minima registrada; com --pacote-repetido, o mesmo commit da o mesmo .tar.gz)
  B2  instalacao em pasta vazia (nada fora do destino, nada na HOME, permissoes, --verificar)
  B3  jogo gerado do CDI (codigo 0, --version, --verify-data, boot curto feito pelo Builder; dados e C++ gerado iguais
      aos do Windows por SHA-256, com --windows-data e --windows-generated: A2)
  B4  edicoes Original e Remastered (instalacao.toml e o perfil que o jogo le; a camera livre na Remastered)
  B5  --version e integridade (arquivo de data/ trocado: --verify-data acusa; arquivo do Builder alterado: --verificar acusa)
  B6  caminhos com espaco, acento, aspas, $, ;, & e parenteses (sem injecao de comando)
  B7  atualizacao: o jogo gerado de novo mantem saves, mods e configuracoes; o Builder atualizado mantem o que voce
      acrescentou e guarda o que voce alterou
  B8  reinstalacao e recuperacao (troca interrompida desfeita, copias velhas limpas, Builder interrompido no meio da
      compilacao: a versao anterior fica e a proxima execucao conclui)
  B9  CDI ausente, pasta no lugar do CDI, CDI truncado, kit incompleto, compilador ausente, cmake ausente no sistema
  B10 permissoes (saida somente leitura, cache sem gravacao recai na pasta de trabalho, modos dos arquivos, nada gravavel
      por todos, tar com dono 0)
  B11 manifesto dos filmes igual ao do Windows (com --windows-data)
  B12 nenhuma midia do jogo nem arquivo particular no pacote (nomes, bytes do jogo com --game-data, known_versions.h)
  B13 hashes e artefatos (SHA256SUMS, lista do pacote do jogo, VERSAO.txt, dados iguais entre duas geracoes)
  B14 falhas e interrupcoes na instalacao (morte na copia e entre as trocas, Ctrl+C, disco cheio por ulimit)

Saida: uma linha [ok]/[FALHA] por verificacao e o resumo; codigo 0 so sem falhas. Tudo fica em --saida (padrao
$TMPDIR/bs-builder-linux-teste); a HOME e o XDG do teste ficam dentro dela, a do usuario nao e tocada.
"""
import argparse
import hashlib
import os
import shutil
import signal
import stat
import subprocess
import sys
import tarfile
import time
import tomllib
from pathlib import Path

FALHAS = []
OKS = 0


def check(nome, cond, detalhe=""):
    global OKS
    if cond:
        OKS += 1
        print(f"[ok] {nome}")
    else:
        FALHAS.append(nome)
        print(f"[FALHA] {nome}" + (f": {detalhe}" if detalhe else ""))
    sys.stdout.flush()
    return cond


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def run(argv, env=None, cwd=None, timeout=3600, preexec=None):
    t0 = time.time()
    r = subprocess.run([str(a) for a in argv], env=env, cwd=cwd, capture_output=True, timeout=timeout, preexec_fn=preexec)
    out = r.stdout.decode("utf-8", "replace") + r.stderr.decode("utf-8", "replace")
    return r.returncode, out, time.time() - t0


def tree_files(d):
    return sorted(str(p.relative_to(d)) for p in Path(d).rglob("*") if p.is_file() or p.is_symlink())


class Ctx:
    def __init__(self, a):
        self.a = a
        self.base = Path(a.saida).resolve()
        if self.base.exists():
            marker = self.base / ".bs-teste-builder-linux"
            if not marker.exists():
                sys.exit(f"{self.base} existe e nao e uma pasta deste teste")
            for p in self.base.rglob("*"):  # pastas somente leitura de uma execucao anterior
                try:
                    p.chmod(p.stat().st_mode | stat.S_IWUSR | stat.S_IXUSR if p.is_dir() else p.stat().st_mode | stat.S_IWUSR)
                except OSError:
                    pass
            shutil.rmtree(self.base)
        self.base.mkdir(parents=True)
        (self.base / ".bs-teste-builder-linux").write_text("pasta do test_builder_linux.py\n")
        self.home = self.base / "home"
        self.home.mkdir()
        self.env = dict(os.environ)
        self.env.update(HOME=str(self.home), XDG_DATA_HOME=str(self.home / ".local/share"),
                        XDG_CONFIG_HOME=str(self.home / ".config"), XDG_CACHE_HOME=str(self.home / ".cache"),
                        LC_ALL="C.UTF-8")
        self.env.pop("BS_BUILDER_CACHE", None)
        self.extra = ["--compilador-sistema"] if a.compilador_sistema else []
        # pacote extraido (uma vez)
        self.pkgdir = self.base / "pacote"
        self.pkgdir.mkdir()
        with tarfile.open(a.pacote) as t:
            t.extractall(self.pkgdir, filter="tar")
        tops = list(self.pkgdir.iterdir())
        self.pkg = tops[0] if len(tops) == 1 else None
        # o CDI num caminho com caracteres especiais (link)
        self.cdi_dir = self.base / "imagens 'ç' $(touch INJETADO) ; & (x)"
        self.cdi_dir.mkdir()
        self.cdi = self.cdi_dir / "Blue Stinger (cópia).cdi"
        os.symlink(Path(a.cdi).resolve(), self.cdi)
        self.installed = None
        self.game_out = None

    def install(self, dest, *extra, env=None):
        return run([self.pkg / "instalar.sh", "--destino", dest, *extra], env=env or self.env)

    def builder(self, inst, *args, env=None, timeout=7200, cwd=None):
        return run([inst / "bs-builder", *args, *self.extra], env=env or self.env, timeout=timeout, cwd=cwd)


def no_injection(ctx):
    found = [p for p in Path("/").glob("tmp/INJETADO")] + list(ctx.base.rglob("INJETADO")) + list(Path.cwd().glob("INJETADO"))
    return found


# ---------------------------------------------------------------------------------------------------------------------
def b1(ctx):
    a = ctx.a
    pk = Path(a.pacote)
    side = Path(str(pk) + ".sha256")
    check("B1 .sha256 ao lado do pacote", side.exists())
    if side.exists():
        check("B1 SHA-256 do .tar.gz confere", side.read_text().split()[0] == sha256(pk))
    check("B1 uma pasta no topo do .tar.gz", ctx.pkg is not None and ctx.pkg.name.startswith("BlueStinger-Builder-"))
    rc, out, _ = run(["sha256sum", "--quiet", "--strict", "-c", "SHA256SUMS.txt"], cwd=ctx.pkg)
    check("B1 SHA256SUMS.txt do pacote confere", rc == 0, out[-400:])
    rc, out, _ = run([ctx.pkg / "bs-builder"])
    check("B1 bs-builder sem argumentos: uso, codigo 2", rc == 2 and "uso:" in out, f"rc={rc}")
    rc, out, _ = run(["readelf", "-d", ctx.pkg / "bs-builder"])
    libs = sorted({l.split("[")[1].split("]")[0] for l in out.splitlines() if "Shared library" in l})
    base = {"libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1", "ld-linux-x86-64.so.2"}
    has_tc = (ctx.pkg / "toolchain").is_dir()
    allowed = base if has_tc else base | {"libstdc++.so.6", "libgcc_s.so.1"}
    check("B1 dependencias do bs-builder so da base do sistema", set(libs) <= allowed, " ".join(libs))
    versao = (ctx.pkg / "VERSAO.txt").read_text(errors="replace")
    check("B1 VERSAO.txt diz o commit, o compilador e a glibc minima",
          "Fontes: commit" in versao and "glibc minima" in versao and "Compilador" in versao)
    print("     " + versao.replace("\n", "\n     ").rstrip())
    if a.pacote_repetido:
        check("B1 mesmo commit, mesmo .tar.gz (reprodutivel)", sha256(a.pacote) == sha256(a.pacote_repetido))


def b2(ctx):
    parent = ctx.base / "b2"
    parent.mkdir()
    dest = parent / "BlueStinger-Builder"
    before_home = tree_files(ctx.home)
    rc, out, _ = ctx.install(dest)
    check("B2 instalar.sh em pasta vazia: codigo 0", rc == 0, out[-600:])
    check("B2 so o destino foi criado ao lado", sorted(p.name for p in parent.iterdir()) == ["BlueStinger-Builder"],
          str(sorted(p.name for p in parent.iterdir())))
    check("B2 nada gravado na HOME", tree_files(ctx.home) == before_home)
    rc, out, _ = ctx.install(dest, "--verificar")
    check("B2 --verificar: integra, sem arquivos estranhos", rc == 0 and "integra" in out and "acrescentados" not in out, out[-400:])
    modes = {p: p.stat().st_mode for p in dest.rglob("*") if not p.is_symlink()}
    check("B2 bs-builder e instalar.sh executaveis",
          os.access(dest / "bs-builder", os.X_OK) and os.access(dest / "instalar.sh", os.X_OK))
    check("B2 nada gravavel por todos nem setuid", all(not (m & (stat.S_IWOTH | stat.S_ISUID | stat.S_ISGID)) for m in modes.values()))
    ctx.installed = dest
    # padrao: ~/.local/share/BlueStinger-Builder da HOME do teste, e o atalho so com --atalho
    rc, out, _ = run([ctx.pkg / "instalar.sh", "--atalho"], env=ctx.env)
    check("B2 destino padrao no XDG_DATA_HOME e atalho em ~/.local/bin",
          rc == 0 and (ctx.home / ".local/share/BlueStinger-Builder/bs-builder").exists()
          and (ctx.home / ".local/bin/bs-builder").is_symlink(), out[-300:])


def need_install(ctx):
    if ctx.installed is None:
        dest = ctx.base / "b2" / "BlueStinger-Builder"
        dest.parent.mkdir(exist_ok=True)
        rc, out, _ = ctx.install(dest)
        if rc != 0:
            sys.exit("nao foi possivel instalar o pacote: " + out[-500:])
        ctx.installed = dest
    return ctx.installed


def game_dir(out):
    return Path(out) / "BlueStinger"


def b3(ctx):
    inst = need_install(ctx)
    out = ctx.base / "Jogos" / "Blue Stinger ç (teste)"
    # a pasta de onde o usuario chama fica intacta (o Zig 0.13 grava "-o -" do CMake num arquivo "-" na pasta atual)
    chamada = ctx.base / "b3 pasta atual"
    chamada.mkdir()
    rc, log, secs = ctx.builder(inst, "--disc", ctx.cdi, "--out", out, cwd=chamada)
    check(f"B3 bs-builder do CDI ao jogo: codigo 0 ({secs:.0f} s)", rc == 0, log[-1500:])
    check("B3 nada gravado na pasta atual", not any(chamada.iterdir()), str(sorted(p.name for p in chamada.iterdir())))
    g = game_dir(out)
    exe = g / "BlueStinger"
    check("B3 executavel Linux instalado", exe.is_file() and os.access(exe, os.X_OK))
    rc2, o2, _ = run(["file", exe])
    check("B3 ELF x86-64", "ELF 64-bit" in o2 and "x86-64" in o2, o2)
    rc2, o2, _ = run([exe, "--version"], cwd=g)
    check("B3 --version", rc2 == 0 and o2.startswith("Blue Stinger"), o2[:200])
    rc2, o2, _ = run([exe, "--verify-data"], cwd=g, timeout=1200)
    check("B3 --verify-data", rc2 == 0, o2[-300:])
    blog = (out / ".bs-build" / "builder.log").read_text(errors="replace")
    check("B3 boot de teste sem o CDI (120 VBlanks) no log", "VBlanks: 120;" in blog or "VBlank-In 120" in blog)
    ctx.game_out = out
    ctx.game_log = log
    a = ctx.a
    if a.windows_data:
        wd = Path(a.windows_data)
        diffs, n = [], 0
        for rel in ["disc.manifest", "disc_meta.bin", "disc_meta.idx"] + ["disc/" + f for f in tree_files(wd / "disc")]:
            n += 1
            if not (g / "data" / rel).is_file() or sha256(g / "data" / rel) != sha256(wd / rel):
                diffs.append(rel)
        check(f"B3 A2: {n} arquivos de dados iguais aos do Windows (SHA-256)", not diffs, " ".join(diffs[:10]))
    if a.windows_generated:
        wg = Path(a.windows_generated)
        caches = list((ctx.home / ".cache/BlueStingerRecomp/build").glob("*/generated"))
        if check("B3 C++ gerado no cache", len(caches) >= 1):
            lg = caches[0]
            files = [f for f in tree_files(wg) if f.endswith((".cpp", ".h", ".cmake"))]
            diffs = [f for f in files if not (lg / f).is_file() or sha256(lg / f) != sha256(wg / f)]
            check(f"B3 C++ gerado no Linux igual ao do Windows ({len(files)} arquivos)", not diffs, " ".join(diffs[:10]))


def need_game(ctx):
    if ctx.game_out is None:
        b3(ctx)
    return ctx.game_out


def headless(exe, n=60, extra=()):
    return run([exe, "--headless", str(n), *extra], cwd=exe.parent, timeout=600)


def b4(ctx):
    inst = need_install(ctx)
    out = ctx.base / "edicoes"
    rc, log, secs = ctx.builder(inst, "--disc", ctx.cdi, "--out", out, "--edicao", "remastered")
    check(f"B4 Remastered gerada ({secs:.0f} s)", rc == 0, log[-800:])
    g = game_dir(out)
    toml = (g / "instalacao.toml").read_text() if (g / "instalacao.toml").exists() else ""
    check("B4 instalacao.toml com perfil remastered", 'perfil = "remastered"' in toml, toml)
    rc, o, _ = headless(g / "BlueStinger")
    check("B4 o jogo le a Remastered e liga a camera livre",
          rc == 0 and "[edicao] perfil remastered" in o and "[camera] modo livre" in o, o[-500:])
    rc, log, secs = ctx.builder(inst, "--disc", ctx.cdi, "--out", out, "--edicao", "original")
    check(f"B4 Original gerada na mesma pasta ({secs:.0f} s)", rc == 0, log[-800:])
    rc, o, _ = headless(g / "BlueStinger")
    check("B4 o jogo le a Original, sem camera livre imposta",
          rc == 0 and "[edicao] perfil original" in o and "modo livre" not in o, o[-500:])
    rc, log, _ = ctx.builder(inst, "--disc", ctx.cdi, "--out", out)
    rc2, o, _ = headless(g / "BlueStinger")
    check("B4 sem --edicao a escolha anterior fica", rc == 0 and "[edicao] perfil original" in o, o[-300:])
    rc, o, _ = run([inst / "bs-builder", "--disc", ctx.cdi, "--out", out, "--edicao", "livre"], env=ctx.env)
    check("B4 edicao invalida: uso, codigo 2", rc == 2, o[-200:])


def b5(ctx):
    out = need_game(ctx)
    g = game_dir(out)
    exe = g / "BlueStinger"
    rc, o, _ = run([exe, "--version"], cwd=g)
    ver = (g / "VERSAO.txt").read_text()
    check("B5 --version e VERSAO.txt", rc == 0 and o.splitlines()[0] in ver, o[:200])
    # data/ e de links para .bs-build/data: o arquivo e trocado (novo inode), nunca editado no lugar
    # um arquivo qualquer do disco (diz qual) e o executavel de boot (diz que os dados sao de outra versao)
    for victim, sinal in ((sorted(p for p in (g / "data" / "disc" / "BSDATA").rglob("*") if p.is_file() and p.stat().st_size > 16)[0], None),
                          (g / "data" / "disc" / "1ST_READ.BIN", "hash do executavel de boot")):
        orig = victim.read_bytes()
        victim.unlink()
        victim.write_bytes(orig[:-1] + bytes([orig[-1] ^ 0xFF]))
        rc, o, _ = run([exe, "--verify-data"], cwd=g, timeout=1200)
        check(f"B5 --verify-data acusa {victim.name} alterado", rc != 0 and (sinal or victim.name) in o, o[-400:])
        victim.unlink()
        victim.write_bytes(orig)
    rc, o, _ = run([exe, "--verify-data"], cwd=g, timeout=1200)
    check("B5 restaurado, --verify-data volta a passar", rc == 0, o[-200:])
    inst = need_install(ctx)
    leia = inst / "kit" / "tools" / "dist" / "linux" / "LEIA-ME-JOGO.txt"
    keep = leia.read_bytes()
    leia.write_bytes(keep + b"\nalterado\n")
    rc, o, _ = ctx.install(inst, "--verificar")
    check("B5 instalar.sh --verificar acusa o arquivo do Builder alterado", rc != 0 and "LEIA-ME-JOGO.txt" in o, o[-400:])
    leia.write_bytes(keep)
    rc, o, _ = ctx.install(inst, "--verificar")
    check("B5 restaurado, --verificar volta a passar", rc == 0, o[-200:])


def b6(ctx):
    # o Builder (kit e compilador vao para o CMake): tudo menos ; \ " e $; a saida e o CDI: qualquer coisa
    weird = ctx.base / "b6 'aspas' & (par) ç ã #x"
    weird.mkdir()
    dest = weird / "Builder `touch INJETADO` 'id'"
    rc, o, _ = ctx.install(dest)
    check("B6 instalacao em caminho com aspas simples, crase, &, parenteses, acento e #", rc == 0, o[-400:])
    motivo = "que o CMake nao aceita"
    for nome, marca in (("b6 com ; ponto e virgula", ";"), ("b6 com \\ barra invertida", "\\"), ('b6 com "aspas"', '"'),
                        ("b6 com $HOME", "$")):
        ruim = ctx.base / nome
        ruim.mkdir()
        rc, o, _ = ctx.install(ruim / "Builder")
        check(f"B6 instalacao em caminho com {marca}: recusada com o motivo", rc != 0 and motivo in o and not (ruim / "Builder").exists(), o[-300:])
        shutil.copytree(ctx.pkg, ruim / "extraido", symlinks=True)
        rc, o, _ = run([ruim / "extraido" / "bs-builder", "--disc", ctx.cdi, "--out", ctx.base / "b6 nada", *ctx.extra], env=ctx.env)
        check(f"B6 bs-builder extraido em caminho com {marca}: recusado com o motivo", rc == 1 and motivo in o, o[-300:])
    out = weird / "saida ; rm -rf x \\ y $(touch INJETADO) \"q\" $HOME"
    rc, log, secs = ctx.builder(dest, "--disc", ctx.cdi, "--out", out, "--sem-filmes")
    check(f"B6 jogo gerado com CDI, Builder e saida em caminhos especiais ({secs:.0f} s)", rc == 0, log[-800:])
    g = game_dir(out)
    rc, o, _ = run([g / "BlueStinger", "--version"], cwd=g)
    check("B6 o jogo nesse caminho responde", rc == 0, o[:200])
    check("B6 --sem-filmes: sem data/video", not (g / "data" / "video").exists())
    check("B6 nenhum comando injetado", not no_injection(ctx), str(no_injection(ctx)))


def b7(ctx):
    inst = need_install(ctx)
    out = need_game(ctx)
    g = game_dir(out)
    # arquivos do usuario no jogo
    if not (g / "instalacao.toml").exists():
        (g / "instalacao.toml").write_text('[saves]\nlocal = "portatil"\n')
    user = {"saves/card0/BLUESTIN.000": b"partida", "mods/video/NEWGAME.pt-br.srt": b"1\n00:00:01,000 --> 00:00:02,000\nola\n",
            "mods/meu-mod/mod.toml": b'id = "meu"\n', "controles.toml": b"# meu mapa\n", "graficos.toml": b"[render]\n",
            "anotacoes ç.txt": b"minhas"}
    for rel, data in user.items():
        (g / rel).parent.mkdir(parents=True, exist_ok=True)
        (g / rel).write_bytes(data)
    cfg = (g / "instalacao.toml").read_bytes()
    rc, log, secs = ctx.builder(inst, "--disc", ctx.cdi, "--out", out)
    check(f"B7 jogo gerado de novo ({secs:.0f} s)", rc == 0, log[-600:])
    kept = [rel for rel, data in user.items() if (g / rel).is_file() and (g / rel).read_bytes() == data]
    check("B7 saves, mods e configuracoes mantidos", len(kept) == len(user), str(set(user) - set(kept)))
    check("B7 instalacao.toml mantido", (g / "instalacao.toml").read_bytes() == cfg)
    check("B7 versao anterior em BlueStinger.anterior", (Path(out) / "BlueStinger.anterior" / "BlueStinger").is_file())
    # Builder: nova versao do pacote (copia com um arquivo diferente e outro SHA256SUMS)
    v2 = ctx.base / "pacote-v2"
    shutil.copytree(ctx.pkg, v2, symlinks=True)
    (v2 / "kit" / "VERSION").write_text("v2-teste\n")
    rc, o, _ = run(["sh", "-c", "find . -type f ! -name SHA256SUMS.txt -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS.txt"], cwd=v2)
    added = inst / "kit" / "mods-padrao" / "meu-pacote" / "mod.toml"
    added.parent.mkdir(parents=True, exist_ok=True)
    added.write_text('id = "meu-pacote"\n')
    changed = inst / "LEIA-ME.txt"
    changed.write_text(changed.read_text() + "\nminha anotacao\n")
    rc, o, _ = run([v2 / "instalar.sh", "--destino", inst], env=ctx.env)
    check("B7 atualizacao do Builder: codigo 0", rc == 0, o[-600:])
    check("B7 a nova versao entrou", (inst / "kit" / "VERSION").read_text() == "v2-teste\n")
    check("B7 arquivo acrescentado por voce passou para a nova versao", added.is_file())
    saved = list(inst.glob("LEIA-ME.txt.usuario-*"))
    check("B7 arquivo do pacote alterado por voce guardado (.usuario-<data>)",
          len(saved) == 1 and "minha anotacao" in saved[0].read_text() and "minha anotacao" not in changed.read_text(), o[-400:])
    check("B7 versao anterior do Builder em .anterior", (inst.parent / (inst.name + ".anterior") / "bs-builder").is_file())
    rc, o, _ = ctx.install(inst, "--verificar")
    check("B7 --verificar da nova versao (acrescimos listados a parte)", rc == 0 and "acrescentados" in o, o[-300:])
    # volta a versao do pacote (os outros casos usam a do teste)
    rc, o, _ = ctx.install(inst)


def b8(ctx):
    parent = ctx.base / "b8"
    parent.mkdir()
    dest = parent / "Builder"
    rc, o, _ = ctx.install(dest)
    rc, o, _ = ctx.install(dest)
    check("B8 reinstalacao da mesma versao", rc == 0 and (parent / "Builder.anterior").is_dir(), o[-300:])
    # troca interrompida: so a .troca (marcada) existe
    shutil.rmtree(parent / "Builder.anterior")
    os.rename(dest, parent / "Builder.troca")
    (parent / "Builder.novo-ZZZZZZ").mkdir()
    (parent / "Builder.novo-ZZZZZZ" / ".bs-builder-copia").write_text("")
    rc, o, _ = ctx.install(dest)
    check("B8 troca interrompida desfeita e instalacao concluida", rc == 0 and "desfazendo" in o and dest.is_dir(), o[-400:])
    check("B8 copia nova incompleta de antes limpa", not (parent / "Builder.novo-ZZZZZZ").exists())
    rc, o, _ = ctx.install(dest, "--verificar")
    check("B8 --verificar depois da recuperacao", rc == 0, o[-200:])
    # pasta alheia no destino: recusada, sem mexer
    alheia = parent / "alheia"
    alheia.mkdir()
    (alheia / "meu.txt").write_text("meu")
    rc, o, _ = ctx.install(alheia)
    check("B8 pasta alheia nao vazia: recusada sem mexer", rc != 0 and (alheia / "meu.txt").read_text() == "meu"
          and sorted(p.name for p in alheia.iterdir()) == ["meu.txt"], o[-300:])
    # Builder interrompido (SIGINT) no meio da compilacao: a versao anterior do jogo fica
    inst = need_install(ctx)
    out = need_game(ctx)
    g = game_dir(out)
    before = sha256(g / "BlueStinger")
    p = subprocess.Popen([str(inst / "bs-builder"), "--disc", str(ctx.cdi), "--out", str(out), "--clean", *ctx.extra],
                         env=ctx.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    seen = []
    for line in iter(p.stdout.readline, b""):
        seen.append(line.decode("utf-8", "replace"))
        if "compilando" in seen[-1] and "%" in seen[-1]:
            time.sleep(3)
            p.send_signal(signal.SIGINT)
            break
    rest = p.communicate(timeout=600)[0].decode("utf-8", "replace")
    check("B8 Builder interrompido: codigo 3, cancelado", p.returncode == 3 and "cancelado" in rest, rest[-400:])
    leftovers = [x.name for x in Path(out).iterdir() if ".new-" in x.name]
    check("B8 nada de pacote temporario sobrando", not leftovers, str(leftovers))
    check("B8 a versao anterior do jogo ficou intacta", sha256(g / "BlueStinger") == before)
    rc, o, _ = run([g / "BlueStinger", "--version"], cwd=g)
    check("B8 e continua respondendo", rc == 0, o[:200])
    rc, log, secs = ctx.builder(inst, "--disc", ctx.cdi, "--out", out)
    check(f"B8 a execucao seguinte conclui ({secs:.0f} s)", rc == 0, log[-600:])


def b9(ctx):
    inst = need_install(ctx)
    out = ctx.base / "b9"
    rc, o, _ = ctx.builder(inst, "--disc", ctx.base / "nao existe.cdi", "--out", out)
    check("B9 CDI ausente: codigo 1, diz qual", rc == 1 and "imagem nao encontrada" in o, o[-300:])
    rc, o, _ = ctx.builder(inst, "--disc", ctx.base, "--out", out)
    check("B9 pasta no lugar do CDI: recusada", rc == 1 and "imagem nao encontrada" in o, o[-300:])
    trunc = ctx.base / "truncado.cdi"
    with open(ctx.a.cdi, "rb") as f:
        trunc.write_bytes(f.read(8 << 20))
    rc, o, _ = ctx.builder(inst, "--disc", trunc, "--out", out)
    check("B9 CDI truncado: recusado com a mensagem da imagem", rc == 1 and "esta imagem nao pode ser usada" in o, o[-400:])
    lixo = ctx.base / "lixo.cdi"
    lixo.write_bytes(os.urandom(1 << 20))
    rc, o, _ = ctx.builder(inst, "--disc", lixo, "--out", out)
    check("B9 arquivo qualquer como CDI: recusado", rc == 1 and "esta imagem nao pode ser usada" in o, o[-400:])
    check("B9 nada instalado depois das recusas", not (out / "BlueStinger").exists())
    # kit incompleto
    f = inst / "kit" / "third_party" / "freetype" / "CMakeLists.txt"
    f.rename(f.with_suffix(".bak"))
    rc, o, _ = ctx.builder(inst, "--disc", ctx.cdi, "--out", out)
    f.with_suffix(".bak").rename(f)
    check("B9 kit incompleto: diz o arquivo e manda reinstalar", rc == 1 and "kit de fontes incompleto" in o and "reinstale" in o, o[-300:])
    # compilador: sem o portatil (pacote sem toolchain/ e sem --compilador-sistema)
    rc, o, _ = run([inst / "bs-builder", "--disc", ctx.cdi, "--out", out, "--compilador", ctx.base / "sem compilador"], env=ctx.env)
    check("B9 compilador portatil ausente: diz o que falta", rc == 1 and "compilador portatil incompleto" in o, o[-400:])
    env = dict(ctx.env, PATH=str(ctx.base / "vazio"))
    rc, o, _ = run([inst / "bs-builder", "--disc", ctx.cdi, "--out", out, "--compilador-sistema"], env=env)
    check("B9 --compilador-sistema sem cmake, ninja e clang: lista os ausentes",
          rc == 1 and "cmake" in o and "ninja" in o and "clang" in o and "apt install" in o, o[-400:])


def b10(ctx):
    inst = need_install(ctx)
    if os.geteuid() == 0:
        check("B10 (rodando como root: permissoes nao se aplicam)", True)
    else:
        ro = ctx.base / "b10 somente leitura"
        ro.mkdir()
        ro.chmod(0o555)
        rc, o, _ = ctx.builder(inst, "--disc", ctx.cdi, "--out", ro / "jogo")
        ro.chmod(0o755)
        check("B10 saida sem permissao de gravacao: recusada com a mensagem",
              rc == 1 and "nao foi possivel criar a pasta de saida" in o and "Permission denied" in o, o[-300:])
        # cache: XDG_CACHE_HOME sem gravacao -> o Builder usa a pasta de trabalho
        rocache = ctx.base / "cache ro"
        rocache.mkdir()
        rocache.chmod(0o555)
        env = dict(ctx.env, XDG_CACHE_HOME=str(rocache))
        out = ctx.base / "b10 saida"
        rc, log, secs = run([inst / "bs-builder", "--disc", ctx.cdi, "--out", out, "--sem-filmes", *ctx.extra], env=env, timeout=7200)
        rocache.chmod(0o755)
        blog = (out / ".bs-build" / "builder.log").read_text(errors="replace") if (out / ".bs-build" / "builder.log").exists() else ""
        check(f"B10 cache sem gravacao: recai na pasta de trabalho ({secs:.0f} s)",
              rc == 0 and f"cache de compilacao: {out}/.bs-build/build/" in blog, log[-500:])
    out = need_game(ctx)
    g = game_dir(out)
    bad = [str(p) for p in g.rglob("*") if not p.is_symlink() and p.stat().st_mode & (stat.S_IWOTH | stat.S_ISUID | stat.S_ISGID)]
    check("B10 jogo: nada gravavel por todos nem setuid", not bad, " ".join(bad[:5]))
    check("B10 jogo: executavel 755, dados sem execucao",
          stat.S_IMODE((g / "BlueStinger").stat().st_mode) == 0o755
          and not any(p.stat().st_mode & 0o111 for p in (g / "data").rglob("*") if p.is_file()))
    with tarfile.open(ctx.a.pacote) as t:
        members = t.getmembers()
    check("B10 tar: dono 0, sem links, dispositivos ou caminhos absolutos",
          all(m.uid == 0 and m.gid == 0 and (m.isfile() or m.isdir()) and not m.name.startswith("/") and ".." not in m.name
              for m in members))
    check("B10 tar: modos sem escrita para outros", all(not (m.mode & 0o022) for m in members))


def b11(ctx):
    if not ctx.a.windows_data:
        check("B11 (sem --windows-data: manifesto do Windows ausente)", False)
        return
    out = need_game(ctx)
    lin = tomllib.loads((game_dir(out) / "data" / "video" / "manifest.toml").read_text())
    win = tomllib.loads((Path(ctx.a.windows_data) / "video" / "manifest.toml").read_text())
    L = {e["original"]: e for e in lin["filme"]}
    W = {e["original"]: e for e in win["filme"]}
    check("B11 os mesmos 17 IDs", sorted(L) == sorted(W) and len(L) == 17)
    keys = ["quadros", "fps", "largura", "altura", "fim_video_100ns", "fim_audio_100ns", "amostras_audio", "fonte_sha256"]
    diffs, n = [], 0
    for k, w in W.items():
        l = L.get(k, {})
        if w.get("ausente") or l.get("ausente"):
            if bool(w.get("ausente")) != bool(l.get("ausente")):
                diffs.append(k + ":ausente")
            continue
        n += 1
        diffs += [f"{k}:{c}" for c in keys if w.get(c) != l.get(c)]
        if "arquivo" in l:
            diffs.append(k + ":arquivo")
    check(f"B11 {n} filmes com os mesmos campos da fonte que o Windows (inclusive os fins arredondados)", n == 16 and not diffs,
          " ".join(diffs[:10]))
    three = [k for k in ("NEWGAME.SFD", "E193.SFD", "ENDING2.SFD") if L[k]["fim_video_100ns"] != L[k]["fim_audio_100ns"]]
    check("B11 os 3 filmes do arredondamento (NEWGAME, E193, ENDING2) com o fim de video arredondado", len(three) == 3)


def b12(ctx):
    names = [m.name for m in tarfile.open(ctx.a.pacote).getmembers()]
    low = [n.lower() for n in names]
    proib = (".cdi", ".gdi", ".chd", ".iso", ".sfd", ".mp4", ".adx", ".afs", ".pvr", ".mlt", ".msb", ".mpb")
    bad = [n for n in low if n.endswith(proib) or n.endswith(("/1st_read.bin", "/ip.bin")) or "/generated/" in n
           or "/game-data/" in n or "/analysis/" in n or "/reviews/" in n or "distribuicao-compilador" in n
           or "_bs-extract" in n or "/icone-local/" in n]
    check("B12 nenhum arquivo de midia, dado do jogo ou particular no pacote", not bad, " ".join(bad[:10]))
    kv = [n for n in names if n.endswith("tools/extractor/known_versions.h")]
    if check("B12 known_versions.h publico no kit", len(kv) == 1):
        txt = tarfile.open(ctx.a.pacote).extractfile(kv[0]).read()
        check("B12 known_versions.h sem as entradas particulares",
              b"hedhunter" not in txt and b"MK-5104150" not in txt and hashlib.sha256(txt).hexdigest().upper()
              != "C4AD9E1A87788ED448C596813D1CF79A1CBE394D3349BB8F2E9BB673CD1F4FC8")
    gd = ctx.a.game_data
    if gd and (Path(gd) / "1ST_READ.BIN").is_file():
        needles = []
        for name, start, step, want in (("1ST_READ.BIN", 0x1000, 0x31000, 8), ("IP.BIN", 0x400, 0xC00, 6)):
            p = Path(gd) / name
            if not p.is_file():
                continue
            b, got, off = p.read_bytes(), 0, start
            while off + 48 <= len(b) and got < want:
                s = b[off:off + 48]
                if len(set(s)) >= 20:
                    needles.append(s)
                    got += 1
                off += step
        hits = []
        for f in ctx.pkg.rglob("*"):
            if f.is_file():
                data = f.read_bytes()
                hits += [str(f) for n in needles if n in data]
        check(f"B12 nenhum dos {len(needles)} trechos do executavel do jogo no pacote", needles and not hits, " ".join(hits[:5]))
    else:
        check("B12 (sem --game-data: conferencia de bytes do jogo nao feita)", False)


def b13(ctx):
    out = need_game(ctx)
    g = game_dir(out)
    listed = [l for l in (g / ".bs-package").read_text().splitlines() if l]
    missing = [l for l in listed if not (g / l).exists()]
    check(f"B13 lista do pacote do jogo ({len(listed)} arquivos) existe inteira", not missing, " ".join(missing[:5]))
    check("B13 instalacao.toml e arquivos do usuario fora da lista", "instalacao.toml" not in listed and not any(l.startswith("saves/") for l in listed))
    ver = (g / "VERSAO.txt").read_text()
    check("B13 VERSAO.txt do jogo: versao, compilador, CMake e Ninja", all(k in ver for k in ("Blue Stinger", "Compilador", "CMake", "Ninja")), ver)
    before = {f: sha256(g / "data" / f) for f in tree_files(g / "data")}
    rc, log, secs = ctx.builder(need_install(ctx), "--disc", ctx.cdi, "--out", out)
    after = {f: sha256(g / "data" / f) for f in tree_files(g / "data")}
    check(f"B13 dados identicos entre duas geracoes ({len(after)} arquivos; {secs:.0f} s)", rc == 0 and before == after)
    check("B13 dados reaproveitados (conferidos, nao refeitos)", "dados ja preparados e conferidos" in (out / ".bs-build" / "builder.log").read_text(errors="replace"))
    rc, o, _ = ctx.install(need_install(ctx), "--verificar")
    check("B13 Builder instalado confere com o SHA256SUMS", rc == 0, o[-200:])


def b14(ctx):
    parent = ctx.base / "b14"
    parent.mkdir()
    dest = parent / "Builder"
    rc, o, _ = ctx.install(dest)
    stamp = (dest / ".bs-builder-instalacao").read_text()
    for etapa in ("copia", "troca"):
        env = dict(ctx.env, BS_INSTALAR_TESTE_PARADA=etapa)
        rc, o, _ = ctx.install(dest, env=env)
        check(f"B14 morte do instalador na etapa '{etapa}': processo morto (sinal 9)", rc == -9, f"rc={rc}")
        if etapa == "copia":
            rc2, o2, _ = ctx.install(dest, "--verificar")
            check("B14 depois da morte na copia, a instalacao anterior continua integra", rc2 == 0, o2[-300:])
        else:
            check("B14 depois da morte entre as trocas, so a .troca existe", not dest.exists() and (parent / "Builder.troca").is_dir())
        rc, o, _ = ctx.install(dest)
        check(f"B14 a execucao seguinte recupera e instala ('{etapa}')", rc == 0 and dest.is_dir(), o[-400:])
        left = [p.name for p in parent.iterdir() if ".novo-" in p.name or p.name.endswith(".troca")]
        check(f"B14 sem sobras depois da recuperacao ('{etapa}')", not left, str(left))
    # Ctrl+C durante a copia: nada trocado
    p = subprocess.Popen([str(ctx.pkg / "instalar.sh"), "--destino", str(dest)], env=ctx.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for line in iter(p.stdout.readline, b""):
        if b"copiando" in line:
            p.send_signal(signal.SIGINT)
            break
    rest = p.communicate(timeout=300)[0].decode("utf-8", "replace")
    left = [x.name for x in parent.iterdir() if ".novo-" in x.name]
    rc2, o2, _ = ctx.install(dest, "--verificar")
    check("B14 Ctrl+C na copia: codigo 130 ou concluido, instalacao integra, sem sobras",
          p.returncode in (130, 0) and rc2 == 0 and not left, f"rc={p.returncode} {rest[-200:]} {left}")
    # disco cheio (limite de tamanho de arquivo): a copia falha antes da troca
    lim = 64 * 1024
    rc, o, _ = run([ctx.pkg / "instalar.sh", "--destino", dest], env=ctx.env,
                   preexec=lambda: (signal.signal(signal.SIGXFSZ, signal.SIG_DFL),
                                    __import__("resource").setrlimit(__import__("resource").RLIMIT_FSIZE, (lim, lim))))
    left = [x.name for x in parent.iterdir() if ".novo-" in x.name]
    rc2, o2, _ = ctx.install(dest, "--verificar")
    check("B14 sem espaco (ulimit -f): falha, instalacao anterior integra, sem sobras", rc != 0 and rc2 == 0 and not left,
          f"rc={rc} {o[-300:]} {left}")


CASOS = {"B1": b1, "B2": b2, "B3": b3, "B4": b4, "B5": b5, "B6": b6, "B7": b7, "B8": b8, "B9": b9, "B10": b10,
         "B11": b11, "B12": b12, "B13": b13, "B14": b14}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pacote", required=True)
    ap.add_argument("--cdi", required=True)
    ap.add_argument("--saida", default=os.path.join(os.environ.get("TMPDIR", "/tmp"), "bs-builder-linux-teste"))
    ap.add_argument("--casos", default=",".join(CASOS))
    ap.add_argument("--compilador-sistema", action="store_true")
    ap.add_argument("--windows-data")
    ap.add_argument("--windows-generated")
    ap.add_argument("--game-data")
    ap.add_argument("--pacote-repetido")
    a = ap.parse_args()
    if os.name != "posix" or sys.platform != "linux":
        sys.exit("so no Linux")
    ctx = Ctx(a)
    print(f"pacote: {a.pacote} ({sha256(a.pacote)[:16]}...), saida: {ctx.base}")
    t0 = time.time()
    for c in a.casos.split(","):
        c = c.strip().upper()
        if c not in CASOS:
            sys.exit(f"caso desconhecido: {c}")
        print(f"== {c}")
        t = time.time()
        try:
            CASOS[c](ctx)
        except Exception as e:  # um caso que quebra nao esconde os outros
            check(f"{c} terminou sem excecao", False, repr(e))
        print(f"   ({time.time() - t:.0f} s)")
    print(f"\n{OKS} ok, {len(FALHAS)} falha(s) em {time.time() - t0:.0f} s")
    for f in FALHAS:
        print("  FALHA:", f)
    return 1 if FALHAS else 0


if __name__ == "__main__":
    sys.exit(main())
