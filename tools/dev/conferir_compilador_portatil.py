"""Conferencia independente do compilador portatil do Windows (builder/compilador_portatil.h), em Python.

    python tools/dev/conferir_compilador_portatil.py <pasta do compilador> [<pasta dos downloads>]

A pasta do compilador e <cache>/compilador/zig-0.13.0 e a dos downloads, por padrao, <pasta>.downloads. Confere:
  1. o hash da arvore (o mesmo algoritmo do Builder, outra implementacao) contra o kArvoreSha256 fixado no cabecalho;
  2. cada arquivo vindo dos zips oficiais (o Zig sem o que nao serve ao Windows, ver podado(); do CMake bin/cmake.exe,
     share/cmake-3.30 e o Copyright.txt; o ninja.exe) identico, byte a byte, a entrada correspondente;
  3. so os arquivos gerados pelo Builder alem desses (invocadores, toolchain do CMake, VERSAO.txt e licencas).
Saida 0 = tudo confere. Revisao Codex 20261120-214554 (o hash oficial provado fora da implementacao do Builder).
"""
import hashlib
import os
import re
import sys
import zipfile

GERADOS = {"VERSAO.txt", "bin/bs-ar.cmd", "bin/bs-ranlib.cmd", "bs-regras.cmake", "bs-toolchain.cmake",
           "licencas/CMake-Copyright.txt", "licencas/Ninja-COPYING.txt", "licencas/Zig-LICENSE.txt",
           "licencas/libcxx-LICENSE.txt", "licencas/mingw-w64-COPYING.txt"}


PODADOS = ("lib/libc/musl/", "lib/libc/wasi/", "lib/libc/glibc/", "lib/libc/darwin/", "lib/tsan/", "lib/docs/")


def podado(rel):
    """O mesmo filtro de builder/compilador_portatil.cpp (o que do Zig nao serve ao alvo x86_64-windows-gnu)."""
    if rel.startswith(PODADOS):
        return True
    return rel.startswith("lib/libc/include/") and not rel.startswith("lib/libc/include/any-windows-any/")


def fixado():
    raiz = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    with open(os.path.join(raiz, "builder", "compilador_portatil.h"), encoding="utf-8") as f:
        m = re.search(r'kArvoreSha256\[\] = "([0-9a-f]{64})"', f.read())
    if not m:
        raise SystemExit("kArvoreSha256 nao encontrado no cabecalho")
    return m.group(1)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    pasta = sys.argv[1]
    downloads = sys.argv[2] if len(sys.argv) > 2 else pasta + ".downloads"
    arq = {}
    for r, ds, fs in os.walk(pasta):
        # como o C++ (hash_arvore): so arquivos comuns e pastas reais; ligacao, junction ou outro tipo invalida a pasta
        for nome in ds + fs:
            e = os.path.join(r, nome)
            if os.path.islink(e) or (hasattr(os.path, "isjunction") and os.path.isjunction(e)):
                print("ERRO: ligacao ou junction:", e)
                return 1
            if nome in fs and not os.path.isfile(e):
                print("ERRO: entrada que nao e arquivo comum:", e)
                return 1
        for f in fs:
            p = os.path.join(r, f)
            with open(p, "rb") as h:
                arq[os.path.relpath(p, pasta).replace(os.sep, "/")] = hashlib.sha256(h.read()).hexdigest()
    lista = "".join(f"{arq[k]}  {k}\n" for k in sorted(arq, key=lambda s: s.encode("utf-8")))
    hash_arvore = hashlib.sha256(lista.encode("utf-8")).hexdigest()

    esperado = {}
    z = zipfile.ZipFile(os.path.join(downloads, "zig-windows-x86_64-0.13.0.zip"))
    t = "zig-windows-x86_64-0.13.0/"
    for i in z.infolist():
        if not i.is_dir() and i.filename.startswith(t) and not podado(i.filename[len(t):]):
            esperado["zig/" + i.filename[len(t):]] = hashlib.sha256(z.read(i)).hexdigest()
    z = zipfile.ZipFile(os.path.join(downloads, "cmake-3.30.5-windows-x86_64.zip"))
    t = "cmake-3.30.5-windows-x86_64/"
    for i in z.infolist():
        r = i.filename[len(t):] if i.filename.startswith(t) else ""
        if not i.is_dir() and (r == "bin/cmake.exe" or r.startswith("share/cmake-3.30/") or r == "doc/cmake/Copyright.txt"):
            esperado["cmake/" + r] = hashlib.sha256(z.read(i)).hexdigest()
    z = zipfile.ZipFile(os.path.join(downloads, "ninja-1.12.1-win.zip"))
    esperado["bin/ninja.exe"] = hashlib.sha256(z.read("ninja.exe")).hexdigest()

    erros = []
    if hash_arvore != fixado():
        erros.append(f"hash da arvore {hash_arvore}, fixado {fixado()}")
    dif = [k for k in esperado if arq.get(k) != esperado[k]]
    if dif:
        erros.append(f"{len(dif)} arquivo(s) diferente(s) dos zips, por exemplo {dif[:3]}")
    extras = sorted(set(arq) - set(esperado) - GERADOS)
    if extras:
        erros.append(f"arquivos que nao vem dos zips nem do Builder: {extras[:5]}")
    print(f"arvore {hash_arvore} ({len(arq)} arquivos); dos zips {len(esperado)}; gerados {len(set(arq) & GERADOS)}")
    for e in erros:
        print("ERRO:", e)
    return 1 if erros else 0


if __name__ == "__main__":
    sys.exit(main())
