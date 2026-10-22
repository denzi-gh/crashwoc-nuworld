"""vu1dis: the VU instruction tables and the package mapping on a synthetic ELF (no game bytes)."""
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import vu1dis  # noqa: E402

E = 0x8                     # flag field (bits 31..27): I E M D T


def up(op, dest=0, ft=0, fs=0, fd=0, flags=0):
    return flags << 27 | dest << 21 | ft << 16 | fs << 11 | fd << 6 | op


def up_special(idx, dest=0, ft=0, fs=0, flags=0):
    return up(0x3C | idx & 3, dest, ft, fs, idx >> 2, flags)


def lo(op, dest=0, ft=0, fs=0, low=0):
    return op << 25 | dest << 21 | ft << 16 | fs << 11 | low


def lo_op(sub, dest=0, ft=0, fs=0, fd=0):
    return lo(0x40, dest, ft, fs, fd << 6 | sub)


def lo_special(idx, dest=0, ft=0, fs=0):
    return lo_op(0x3C | idx & 3, dest, ft, fs, idx >> 2)


class Upper(unittest.TestCase):
    def test_forms(self):
        cases = [
            (up(0x00, 0xF, 3, 2, 1), "ADDx.xyzw VF01, VF02, VF03x"),
            (up(0x1C, 0xE, 0, 2, 1), "MULq.xyz VF01, VF02, Q"),
            (up(0x1E, 0x1, 0, 4, 5), "MULi.w VF05, VF04, I"),
            (up(0x2E, 0xE, 3, 2, 1), "OPMSUB.xyz VF01, VF02, VF03"),
            (up_special(0x1B, 0xF, 3, 2), "MULAw.xyzw ACC, VF02, VF03w"),
            (up_special(0x2E, 0xE, 3, 2), "OPMULA.xyz ACC, VF02, VF03"),
            (up_special(0x22, 0x8, 0, 6), "ADDAi.x ACC, VF06, I"),
            (up_special(0x10, 0xF, 4, 5), "ITOF0.xyzw VF04, VF05"),
            (up_special(0x1F, 0xE, 3, 2), "CLIPw.xyz VF02, VF03w"),
            (0x000002FF, "NOP"),
            (up_special(0x2F, flags=E), "NOP[E]"),
            (up(0x2A, 0xF, 1, 1, 1, flags=0x1), "MUL.xyzw[T] VF01, VF01, VF01"),
        ]
        for word, text in cases:
            self.assertEqual(vu1dis.upper(word), text, hex(word))
        self.assertEqual(up_special(0x2F), 0x000002FF)

    def test_tables(self):
        for op, (mn, _form) in vu1dis.UPPER.items():
            self.assertTrue(vu1dis.upper(up(op)).startswith(mn), mn)
        for idx, (mn, _form) in vu1dis.UPPER_SPECIAL.items():
            self.assertTrue(vu1dis.upper(up_special(idx)).startswith(mn), mn)

    def test_rejects(self):
        for word in (up(0x30), up_special(0x2B), up(0x1C, ft=1), 0x000002FF | 1 << 16, 0x000002FF | 1 << 25):
            self.assertIsNone(vu1dis.upper(word), hex(word))


class Lower(unittest.TestCase):
    def test_forms(self):
        cases = [
            (lo(0x00, 0xF, 1, 2, 0x7FF), 0, "LQ.xyzw VF01, -1(VI02)"),
            (lo(0x01, 0x8, 3, 4, 5), 0, "SQ.x VF04, 5(VI03)"),
            (lo(0x04, 0x1, 5, 6, 0x10), 0, "ILW.w VI05, 0x10(VI06)"),
            (lo(0x08, 0xF, 1, 0, 0x7FF), 0, "IADDIU VI01, VI00, 0x7fff"),
            (lo(0x12) | 0x123456, 0, "FCAND VI01, 0x123456"),
            (lo(0x11) | 0xFFF, 0, "FCSET 0x000fff"),
            (lo(0x16, ft=2) | 1 << 21 | 0x7FF, 0, "FSAND VI02, 0xfff"),
            (lo(0x1A, ft=1, fs=2), 0, "FMAND VI01, VI02"),
            (lo(0x1C, ft=3), 0, "FCGET VI03"),
            (lo(0x20, low=0x7FE), 0x10, "B 0x0008"),
            (lo(0x21, ft=15, low=1), 0, "BAL VI15, 0x0010"),
            (lo(0x24, fs=15), 0, "JR VI15"),
            (lo(0x29, ft=1, fs=2, low=3), 0x100, "IBNE VI01, VI02, 0x0120"),
            (lo(0x2C, fs=3), 0, "IBLTZ VI03, 0x0008"),
            (lo_op(0x30, ft=3, fs=2, fd=1), 0, "IADD VI01, VI02, VI03"),
            (lo_op(0x32, ft=1, fs=2, fd=0x1F), 0, "IADDI VI01, VI02, -1"),
            (lo_special(0x0C << 2, 0xF, 1, 2), 0, "MOVE.xyzw VF01, VF02"),
            (lo_special(0x0C << 2), 0, "NOP"),
            (lo_special(0x0D << 2, 0xF, 1, 2), 0, "LQI.xyzw VF01, (VI02++)"),
            (lo_special(0x0D << 2 | 3, 0xF, 3, 4), 0, "SQD.xyzw VF04, (--VI03)"),
            (lo_special(0x0E << 2, 0b1100, 2, 1), 0, "DIV Q, VF01x, VF02w"),
            (lo_special(0x0E << 2 | 1, 0b0100, 5), 0, "SQRT Q, VF05y"),
            (lo_special(0x0E << 2 | 3), 0, "WAITQ"),
            (lo_special(0x0F << 2, 0b0010, 1, 4), 0, "MTIR VI01, VF04z"),
            (lo_special(0x0F << 2 | 1, 0xF, 4, 1), 0, "MFIR.xyzw VF04, VI01"),
            (lo_special(0x0F << 2 | 2, 0x8, 1, 2), 0, "ILWR.x VI01, (VI02)"),
            (lo_special(0x19 << 2, 0x1, 5), 0, "MFP.w VF05, P"),
            (lo_special(0x10 << 2 | 2, 0b0011, fs=2), 0, "RINIT R, VF02w"),
            (lo_special(0x1A << 2, ft=2), 0, "XTOP VI02"),
            (lo_special(0x1B << 2, fs=14), 0, "XGKICK VI14"),
            (lo_special(0x1D << 2 | 2, 0xF, fs=3), 0, "ESUM P, VF03xyzw"),
            (lo_special(0x1E << 2, 0b0001, fs=2), 0, "ESQRT P, VF02y"),
        ]
        for word, pc, text in cases:
            self.assertEqual(vu1dis.lower(word, pc), text, hex(word))
        self.assertEqual(lo_special(0x0C << 2), vu1dis.LOWER_NOP)
        self.assertEqual(vu1dis.lower(lo(0x20, low=1), 0, lambda vu: f"L{vu:x}"), "B L10")

    def test_tables(self):
        for op, (mn, _form) in vu1dis.LOWER.items():
            self.assertTrue(vu1dis.lower(lo(op), 0).startswith(mn), mn)
        for sub, (mn, _form) in vu1dis.LOWER_OP.items():
            self.assertTrue(vu1dis.lower(lo_op(sub), 0).startswith(mn), mn)
        for idx, (mn, _form) in vu1dis.LOWER_SPECIAL.items():
            ft = 1 if mn == "MOVE" else 0       # an all-zero MOVE is the lower NOP
            self.assertTrue(vu1dis.lower(lo_special(idx, ft=ft), 0).startswith(mn), mn)

    def test_rejects(self):
        for word in (lo(0x02), lo_op(0x33), lo(0x24, ft=1, fs=2), lo_special(0x0C << 2 | 2), lo(0x12) | 1 << 24):
            self.assertIsNone(vu1dis.lower(word, 0), hex(word))

    def test_loi(self):
        self.assertEqual(vu1dis.instruction(0x3F000000, 0x800002FF, 0), f"{'NOP':<31} LOI 0.5")
        for word, text in ((0x3DCCCCCD, "0.1"), (0x3F800000, "1.0"), (0x80000000, "-0.0"),
                           (0x7F800000, "0x7f800000")):
            self.assertEqual(vu1dis.f32(word), text)


# --- A synthetic ELF: package a (a.dsm) and package b (b.dsm) both include shared.vsm ---------------------------

ADD = up(0x00, 0xF, 3, 2, 1)                # ADDx.xyzw VF01, VF02, VF03x
IADDIU5 = lo(0x08, 0, 1, 0, 5)              # IADDIU VI01, VI00, 5
NOP_E = up_special(0x2F, flags=E)
IBNE_BACK2 = lo(0x29, 0, 1, 0, 0x7FE)       # IBNE VI01, VI00, pc+8-16
NOPS = (0x000002FF, vu1dis.LOWER_NOP)


def build_elf(sections):
    """An ELF32 with section headers only; sections: (name, type, addr, data, link name)."""
    names = [s[0] for s in sections] + [".shstrtab"]
    shstr = b"\0"
    offs = []
    for n in names:
        offs.append(len(shstr))
        shstr += n.encode() + b"\0"
    index = {n: i + 1 for i, n in enumerate(names)}
    body = bytearray(52)
    heads = [bytes(40)]
    for (name, typ, addr, data, link), off in zip(sections + [(".shstrtab", 3, 0, shstr, None)], offs):
        heads.append(struct.pack("<10I", off, typ, 0, addr, len(body), len(data), index.get(link, 0), 0, 1, 0))
        body += data
    shoff = len(body)
    ident = b"\x7fELF" + bytes([1, 1, 1]) + bytes(9)
    body[:52] = ident + struct.pack("<HHIIIIIHHHHHH", 2, 8, 1, 0, 0, shoff, 0, 52, 0, 0, 40, len(heads),
                                    len(heads) - 1)
    return bytes(body) + b"".join(heads)


def synthetic_elf(differ=False, bad=False, vu_b=0x800):
    w = struct.Struct("<I").pack

    def pair(upper, lower):
        return w(lower) + w(upper)
    a = (w(0x60000003) + w(0) + w(0) + w(0x4A040000)
         + pair(0x000002FF, lo(0x20, low=1))                    # B loop (vu 0x10)
         + pair(*NOPS)
         + pair(ADD, IADDIU5) + pair(NOP_E, IBNE_BACK2)
         + w(0x03000100) + w(0x02000180) + w(0) + w(0)          # BASE, OFFSET
         + w(0x70000000) + w(0) + w(0) + w(0))                  # DMAend
    b = (w(0x60000001) + w(0) + w(0) + w(0x4A020100)
         + pair(up(0x04, 0xF, 3, 2, 1) if differ else ADD, IADDIU5) + pair(NOP_E, IBNE_BACK2))
    if bad:
        a = a[:0x1C] + w(up(0x30)) + a[0x20:]
    vutext = a + b
    assert len(a) == 0x50 and len(b) == 0x20

    ovl_a, ovl_b = ".DVP.overlay..0x0.1.5.0", f".DVP.overlay..0x{vu_b:x}.2.3.0"
    ostr = b"\0" + ovl_a.encode() + b"\0" + ovl_b.encode() + b"\0"
    ostr_addr = 0x9000
    ovlytab = struct.pack("<6I", ostr_addr + 1, 0x1010, 0, ostr_addr + 2 + len(ovl_a), 0x1060, vu_b)

    strs = [b""]

    def s(text):
        strs.append(text.encode() + b"\0")
        return sum(len(x) for x in strs[:-1])
    a_dsm, b_dsm, shared = "C:\\p\\a.dsm", "C:\\p\\b.dsm", "C:\\p\\shared.vsm"
    entries = [(s(a_dsm), 0x64, 0, 0x1000), (0, 0x44, 1, 0x1000), (0, 0x44, 3, 0x1008), (0, 0x44, 5, 0x1010),
               (0, 0x44, 6, 0x1018), (s(shared), 0x84, 0, 0x1020), (0, 0x44, 3, 0x1020), (0, 0x44, 4, 0x1028),
               (s(a_dsm), 0x84, 0, 0x1030), (0, 0x44, 9, 0x1030), (0, 0x44, 10, 0x1040), (0, 0x44, 11, 0x1050),
               (s(b_dsm), 0x64, 0, 0x1050), (0, 0x44, 1, 0x1050), (0, 0x44, 2, 0x1058),
               (s(shared), 0x84, 0, 0x1060), (0, 0x44, 3, 0x1060), (0, 0x44, 4, 0x1068),
               (s(b_dsm), 0x84, 0, 0x1070), (0, 0x44, 6, 0x1070)]
    stabstr = b"".join(strs)
    stab = struct.pack("<IBBHI", 0, 0, 0, len(entries), len(stabstr))
    stab += b"".join(struct.pack("<IBBHI", strx, typ, 0, desc, value) for strx, typ, desc, value in entries)

    # section indexes in build_elf order: 1 .vutext, 2/3 overlays
    syms = [("", 0, 0, 0), ("vu_load_a_mpg", 0x1000, 0x11, 1), ("vu_load_b_mpg", 0x1050, 0x11, 1),
            (".dma.1", 0x1000, 0, 1), ("__start_" + ovl_a, 0, 0, 2), ("a_entry", 0x10, 0, 2), ("loop", 0x10, 0, 2),
            ("loop", vu_b, 0, 3), ("_$a_entry", 0x1020, 0, 1), ("_$loop", 0x1020, 0, 1), ("_$loop", 0x1060, 0, 1)]
    strtab, symtab = b"\0", b""
    for name, value, info, shndx in syms:
        off = len(strtab) if name else 0
        strtab += (name.encode() + b"\0") if name else b""
        symtab += struct.pack("<IIIBBH", off, value, 0, info, 0, shndx)
    return build_elf([(".vutext", 1, 0x1000, vutext, None),
                      (ovl_a, 0x7FFFF421, 0, bytes(32), None), (ovl_b, 0x7FFFF421, 0, bytes(16), None),
                      (".DVP.ovlytab", 0x7FFFF420, ostr_addr, ovlytab, ".DVP.ovlystrtab"),
                      (".DVP.ovlystrtab", 3, ostr_addr, ostr, None),
                      (".stab", 1, 0, stab, ".stabstr"), (".stabstr", 3, 0, stabstr, None),
                      (".symtab", 2, 0, symtab, ".strtab"), (".strtab", 3, 0, strtab, None)])


def decode(**kw):
    mc = vu1dis.analyse(vu1dis.Elf(synthetic_elf(**kw)))
    vu1dis.rebuild(mc)
    return mc


class Mapping(unittest.TestCase):
    def test_packages(self):
        mc = decode()
        self.assertEqual(mc.errors, [])
        a, b = mc.packages
        self.assertEqual((a.name, a.ee, a.end, a.dsm), ("vu_load_a_mpg", 0x1000, 0x1050, "C:\\p\\a.dsm"))
        self.assertEqual((b.name, b.ee, b.end, b.dsm), ("vu_load_b_mpg", 0x1050, 0x1070, "C:\\p\\b.dsm"))
        self.assertEqual([(c.ee, c.vu, c.count) for c in a.chunks + b.chunks], [(0x1010, 0, 4), (0x1060, 0x800, 2)])
        self.assertEqual(a.labels, {0x10: ["a_entry", "loop"]})
        self.assertEqual(b.labels, {0x800: ["loop"]})
        self.assertEqual(mc.counts(), (2, 1, 2))
        self.assertEqual(mc.copies, {"a.dsm": ["vu_load_a_mpg"], "shared.vsm": ["vu_load_a_mpg", "vu_load_b_mpg"],
                                     "b.dsm": ["vu_load_b_mpg"]})

    def test_sources(self):
        mc = decode()
        nop = f"{'NOP':<31} "
        self.assertEqual(mc.rebuilt["shared.vsm"], {      # a_entry is only in package a: a.dsm's label
            2: "loop:",
            3: f"{'ADDx.xyzw VF01, VF02, VF03x':<31} IADDIU VI01, VI00, 5",
            4: f"{'NOP[E]':<31} IBNE VI01, VI00, loop"})
        self.assertEqual(mc.rebuilt["a.dsm"], {
            1: "DMAret 3", 3: "NOP ; MPG 0x0000, 4", 5: nop + "B loop", 6: nop + "NOP",
            9: "BASE 0x100 ; OFFSET 0x180 ; NOP ; NOP", 10: "DMAend 0 ; NOP ; NOP"})
        self.assertEqual(mc.rebuilt["b.dsm"], {1: "DMAret 1", 2: "NOP ; MPG 0x0800, 2"})

    def test_errors(self):
        self.assertIn("shared.vsm: vu_load_b_mpg differs from vu_load_a_mpg at line 3",
                      " ".join(decode(differ=True).errors))
        self.assertIn("vu_load_a_mpg: unknown instruction 0x00000030 0x8000033c at 0x1018", decode(bad=True).errors)
        self.assertIn("vu_load_b_mpg: the MPG codes do not match .DVP.ovlytab", decode(vu_b=0x808).errors)


class Cli(unittest.TestCase):
    def run_tool(self, *args):
        return subprocess.run([sys.executable, str(REPO / "tools" / "vu1dis.py"), *args],
                              capture_output=True, text=True)

    def test_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            disc = Path(tmp) / "disc"
            disc.mkdir()
            (disc / "SLES_503.86").write_bytes(synthetic_elf())
            out = Path(tmp) / "out"
            run = self.run_tool("--data-dir", tmp, "--out", str(out), "--expect", "2,1,2")
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(sorted(p.relative_to(out).as_posix() for p in out.rglob("*") if p.is_file()),
                             ["lst/vu_load_a_mpg.lst", "lst/vu_load_b_mpg.lst", "src/a.dsm", "src/b.dsm",
                              "src/shared.vsm", "vu1map.json"])
            self.assertTrue((out / "src" / "shared.vsm").read_text().startswith("; C:\\p\\shared.vsm (rebuilt"))
            self.assertIn("00001010 0000  000002ff 40000001", (out / "lst" / "vu_load_a_mpg.lst").read_text())

    def test_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            elf = Path(tmp) / "boot.elf"
            elf.write_bytes(synthetic_elf(bad=True))
            run = self.run_tool("--elf", str(elf), "--check", "--out", str(Path(tmp) / "out"))
            self.assertEqual(run.returncode, 1)
            self.assertIn("unknown instruction", run.stderr)
            self.assertFalse((Path(tmp) / "out").exists())
            elf.write_bytes(synthetic_elf())
            run = self.run_tool("--elf", str(elf), "--check", "--expect", "16,24,44")
            self.assertEqual(run.returncode, 1)
            self.assertIn("expected (16, 24, 44)", run.stderr)


if __name__ == "__main__":
    unittest.main()
