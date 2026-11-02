#!/usr/bin/env python3
"""Testes do tools/dev/exe_identidade.py sobre o exe real da Preview (sem ele, os casos que precisam dele sao pulados).

Positivos (a identidade NAO muda): so os metadados de build normalizados. Negativos (muda): qualquer outro byte, a
literal ambigua (duas datas) e o separador da literal.
  python tools/dev/test_exe_identidade.py [<exe>]
"""
import os
import shutil
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_identidade as ei  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXE = os.path.join(REPO, "dist", "BlueStinger-Preview", "BlueStinger.exe")


class Identidade(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.isfile(EXE):
            raise unittest.SkipTest("exe ausente: %s" % EXE)
        cls.dir = tempfile.mkdtemp(prefix="bs_ident_")
        cls.orig = ei.ler(EXE)
        cls.base = cls.ident(cls.orig)
        cls.campos = {m: (a, z) for a, z, m in ei.campos(bytearray(cls.orig))}

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    @classmethod
    def ident(cls, b):
        p = os.path.join(cls.dir, "x.exe")
        with open(p, "wb") as f:
            f.write(b)
        return ei.identidade(p)

    def mut(self, *mudancas):
        b = bytearray(self.orig)
        for off, v in mudancas:
            b[off:off + len(v)] = v
        return self.ident(bytes(b))

    def test_campos_esperados(self):
        for m in ("COFF TimeDateStamp", "BS_BUILD_DATE", "BS_VERSION"):
            self.assertIn(m, self.campos)
        self.assertTrue(any(m.startswith("debug[") for m in self.campos))

    def test_metadados_nao_mudam(self):
        a, _ = self.campos["COFF TimeDateStamp"]
        self.assertEqual(self.mut((a, b"\x11\x22\x33\x44")), self.base)
        for m, (a, z) in self.campos.items():
            if m.startswith("debug["):
                self.assertEqual(self.mut((a, b"\x55\x66\x77\x88")), self.base, m)
        a, z = self.campos["BS_BUILD_DATE"]
        self.assertEqual(self.mut((a, b"2031-12-31 23:59")), self.base)
        a, z = self.campos["BS_VERSION"]
        novo = (b"abcdef0" + b"-dirty")[: z - a] if z - a >= 7 else b"abcdef0"
        self.assertEqual(self.mut((a, novo.ljust(z - a, b"0")[: z - a])), self.base)

    def test_codigo_muda(self):
        pe = struct.unpack_from("<I", self.orig, 0x3C)[0]
        secs = ei.secoes(self.orig, pe)
        text = [s for s in secs if s[0] == ".text"][0]
        off = text[3] + 0x1000
        self.assertNotEqual(self.mut((off, bytes([self.orig[off] ^ 0xFF]))), self.base)

    def test_outro_dado_somente_leitura_muda(self):
        a, z = self.campos["BS_BUILD_DATE"]
        off = z + 40  # perto da literal, fora dela
        self.assertNotEqual(self.mut((off, bytes([self.orig[off] ^ 0x01]))), self.base)

    def test_separador_da_literal_muda(self):
        a, z = self.campos["BS_VERSION"]
        self.assertNotEqual(self.mut((z, b"A")), self.base)  # o zero final virou texto: a literal deixa de existir

    def test_literal_ambigua_desliga_a_normalizacao(self):
        # uma segunda data em outro ponto da .rdata: nenhuma das duas e normalizada; mudar a de build muda a identidade
        a, z = self.campos["BS_BUILD_DATE"]
        b = bytearray(self.orig)
        pe = struct.unpack_from("<I", b, 0x3C)[0]
        rd = [s for s in ei.secoes(b, pe) if s[0] == ".rdata"][0]
        fim = rd[3] + rd[4]
        alvo = None
        for o in range(fim - 64, rd[3], -1):  # 18 zeros seguidos no fim da .rdata (enchimento)
            if b[o:o + 18] == b"\x00" * 18:
                alvo = o
                break
        self.assertIsNotNone(alvo)
        b[alvo + 1:alvo + 17] = b"2020-01-01 00:00"
        duas = self.ident(bytes(b))
        b[a:z] = b"2031-12-31 23:59"
        self.assertNotEqual(self.ident(bytes(b)), duas)

    def test_sem_o_titulo_do_jogo_nada_de_data_nem_versao(self):
        # um exe ou uma DLL que nao e o jogo: a unica data ou "versao" da .rdata pode ser dado funcional
        ti = self.orig.find(b"\x00Blue Stinger (recompilado)\x00")
        self.assertGreater(ti, 0)
        b = bytearray(self.orig)
        b[ti + 1] = ord("X")
        outro = self.ident(bytes(b))
        self.assertNotIn("BS_BUILD_DATE", [m for _, _, m in ei.campos(bytearray(b))])
        for nome in ("BS_BUILD_DATE", "BS_VERSION"):
            a, z = self.campos[nome]
            c = bytearray(b)
            c[a] = ord("0") if c[a] != ord("0") else ord("1")
            self.assertNotEqual(self.ident(bytes(c)), outro, nome)

    def test_nao_pe_cru(self):
        p = os.path.join(self.dir, "y.dll")
        with open(p, "wb") as f:
            f.write(b"nao sou PE")
        self.assertTrue(ei.identidade(p).startswith("raw-"))
        self.assertTrue(ei.arquivo(p).startswith("raw-"))

    def test_arvore(self):
        d = os.path.join(self.dir, "arv")
        os.makedirs(os.path.join(d, "sub"), exist_ok=True)
        for nome, txt in (("a.txt", "1"), (os.path.join("sub", "b.txt"), "2")):
            with open(os.path.join(d, nome), "w") as f:
                f.write(txt)
        x = ei.arvore(d)
        with open(os.path.join(d, "sub", "b.txt"), "w") as f:
            f.write("3")
        self.assertNotEqual(ei.arvore(d), x)
        self.assertEqual(ei.arvore(os.path.join(self.dir, "nao-existe")), "ausente")


if __name__ == "__main__":
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
        EXE = sys.argv.pop(1)
    unittest.main(verbosity=2)
