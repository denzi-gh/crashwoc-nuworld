"""gen_dataimage on tests/fixtures/mini (libclang; clang or $CC for the end-to-end test)."""
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tests"))
sys.path.insert(0, str(REPO / "tools"))
import fixture_build as fb  # noqa: E402
import gen_converters  # noqa: E402
import gen_dataimage  # noqa: E402
import nwunits  # noqa: E402
import typedb  # noqa: E402


def fixture_elf(cb_offset: int, bad: bool = False, trip: bool = False) -> bytes:
    """The fixture's retail image (fixtures/mini sections.json): one PT_LOAD at 0x00100000. bad: a pointer into
    .text; trip: two bytes the image cannot reproduce (a C value, a padding byte)."""
    base, filesz, memsz = 0x00100000, 0x1100, 0x1300
    seg = bytearray(filesz)

    def put(addr, data):
        seg[addr - base:addr - base + len(data)] = data

    put(0x00100B00, b"hello\0world\0")
    put(0x00100900, struct.pack("<II", 0x00100104 if bad else 0x00100B00, 0x00100B06))
    put(0x00100A00, struct.pack("<Iii", 0x00100D00, 0x1234, 5))         # fixroot: next, i, named.a
    put(0x00100A00 + cb_offset, struct.pack("<I", 0x00100100))          # fixroot.cb = FixAdd
    put(0x00100A48, struct.pack("<4f", 1.5, 2.5, 3.5, 4.5))             # fixpairs
    put(0x00100C00, struct.pack("<4i", 1, 2, 3, 5 if trip else 4))      # fixstatic (C data)
    put(0x00100C10, struct.pack("<I", 0x00100B06))                      # fixgreet = "world" (C data)
    put(0x00100D00, struct.pack("<Ii", 0x00100D28, 77))                 # fixfar: next = fixafter, i
    put(0x00100D28, struct.pack("<Ii", 0x00100A00, 88))                 # fixafter (where fixfar ends)
    if trip:
        put(0x00100A0C, b"")                                         # fixroot's padding
    ident = b"\x7fELF" + bytes([1, 1, 1]) + bytes(9)
    ehdr = ident + struct.pack("<HHIIIIIHHHHHH", 2, 8, 1, 0x00100008, 52, 0, 0, 52, 32, 1, 40, 0, 0)
    phdr = struct.pack("<8I", 1, 0x100, base, base, filesz, memsz, 7, 0x100)
    return (ehdr + phdr).ljust(0x100, b"\0") + bytes(seg)


class ListOutputs(unittest.TestCase):
    def test_outputs(self):
        run = subprocess.run([sys.executable, str(REPO / "tools" / "gen_dataimage.py"), "--decomp", str(fb.DECOMP),
                              "--units", str(fb.UNITS), "--port-src", str(fb.PORT_SRC), "--out", "gen",
                              "--list-outputs"], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout.split(), ["dataimage.c", "dataimage_types.c", "nucore/fixcore.data.inc",
                                              "game/fixgame.data.inc", "game/fixdata.data.inc"])

    def test_data_toml(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "data.toml"
            path.write_text('schema = 1\n[owned]\n"a" = 1\n[other]\n')
            _data, errors = gen_dataimage.load_data_toml(path)
        self.assertEqual(errors, ["data.toml: unknown tables ['other']",
                                  "data.toml: owned.a: the value is the reason, a string"])


@unittest.skipIf(typedb.cindex is None, "no libclang (tools/requirements.txt)")
class Fixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.db = fb.fixture_db()
        cls.ann, errors = gen_converters.load_annotations(fb.FIXTURE / "port" / "convert.toml")
        assert errors == [], errors
        cls.data, errors = gen_dataimage.load_data_toml(fb.FIXTURE / "port" / "data.toml")
        assert errors == [], errors

    def generate(self, decomp=fb.DECOMP, data=None):
        model = nwunits.load(decomp, fb.UNITS, fb.PORT_SRC)
        self.assertEqual(model.errors, [])
        return gen_dataimage.generate(model, decomp, self.db, self.ann, data or self.data)

    def test_roots(self):
        image, outputs = self.generate()
        self.assertEqual(image.errors, [])
        roots = {r.name: (r.kind, r.ps2_size, r.nwtype or None,
                          f"{r.container.name}+{r.host_off}" if r.container else None)
                 for r in image.roots.values()}
        self.assertEqual(roots, {
            "fixnames": ("convert", 8, "nw_rt_p_s8", None),
            "fixroot": ("convert", 40, "nw_type_fixnode_s", None),
            "D_00100A04": ("alias", 0, None, "fixroot+8"),
            "fixpairs": ("copy", 16, None, None),
            "D_00100B00": ("mirror", 0, None, None),
            "D_00100C08": ("alias", 0, None, "fixstatic+8"),
        })
        self.assertEqual(sorted((o.name, o.static, o.plain) for o in image.objs),
                         [("fixgreet", False, False), ("fixstatic", True, True), ("fixtent", False, True)])

        src, inc = outputs["dataimage.c"], outputs[str(Path("game/fixdata.data.inc"))]
        for line in ['_Alignas(16) unsigned char fixroot[40];',
                     '_Alignas(16) unsigned char fixpairs[16];',
                     '        ".set " NW_SYM(D_00100A04) ", " NW_SYM(fixroot) "+8\\n");',
                     '        ".set " NW_SYM(D_00100B00) ", " NW_SYM(nw_ps2_mirror) "+1280\\n");',
                     '    { "fixnames", 0x00100900u, 8u, fixnames, 16u, &nw_rt_p_s8, 2u, NW_ROOT_CONVERT },',
                     "#define NW_MIRROR_BASE 0x00100600u"]:
            self.assertIn(line + "\n", src)
        for line in ['    { "fixstatic", 0x00100c00u, 16u, (void *)&fixstatic, (unsigned int)sizeof fixstatic, 0, '
                     '4u, NW_DATA_PLAIN },',
                     '        ".set " NW_SYM(D_00100C08) ", " NW_SYM(fixstatic) "+8\\n");',
                     '_Static_assert(sizeof(fixroot) == 40, "fixroot");',
                     '_Static_assert(sizeof(D_00100B00[0]) == 1, "D_00100B00");']:
            self.assertIn(line + "\n", inc)
        self.assertNotIn("fixhidden", src + inc)        # only the overridden FixDataHidden used it
        self.assertNotIn("fixowned", src)               # port/data.toml [owned]
        self.assertIn('const NwDataUnit nw_data_unit_nucore_fixcore = { "nucore/fixcore", 0, 0u };',
                      outputs[str(Path("nucore/fixcore.data.inc"))])

    def broken(self, edits):
        """Errors of the fixture with its sources edited: [(relative path, old, new)]."""
        with tempfile.TemporaryDirectory() as tmp:
            decomp = Path(tmp) / "decomp"
            shutil.copytree(fb.DECOMP, decomp)
            for rel, old, new in edits:
                path = decomp / rel
                text = path.read_text()
                self.assertIn(old, text)
                path.write_text(text.replace(old, new))
            image, outputs = self.generate(decomp)
            self.assertEqual(outputs, {})
            return image.errors

    def test_misaligned_complete_array(self):
        # 16 bytes: clang assumes 16-byte alignment, which 0x00100B03 does not have in the mirror
        with tempfile.TemporaryDirectory() as tmp:
            decomp = Path(tmp) / "decomp"
            shutil.copytree(fb.DECOMP, decomp)
            path = decomp / "src" / "game" / "fixdata.c"
            path.write_text(path.read_text()
                            .replace("extern s32 fixowned;", "extern s32 fixowned;\nextern char D_00100B03[16];")
                            .replace("    return D_00100B00[0]", "    return D_00100B03[0] + D_00100B00[0]"))
            image, _outputs = self.generate(decomp)
        self.assertEqual(image.errors, [])
        root = image.roots["D_00100B03"]
        self.assertEqual((root.kind, root.ps2_size, root.extent, root.need), ("copy", 16, "declared", 16))

    def test_plain_and_pointer_declarations(self):
        errors = self.broken([("src/game/fixgame.c", "void FixGameTick(struct fixobj_s *obj)\n{\n",
                               "extern s32 fixnames[];\n\nvoid FixGameTick(struct fixobj_s *obj)\n"
                               "{\n    obj->hp = fixnames[0];\n")])
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("fixnames: declared as plain data at src/game/fixgame.c:", errors[0])

    def test_unknown_address_and_stale_owned(self):
        data = {"owned": {**self.data["owned"], "fixghost": "gone"}, "conflicts": {}}
        model = nwunits.load(fb.DECOMP, fb.UNITS, fb.PORT_SRC)
        image, _outputs = gen_dataimage.generate(model, fb.DECOMP, self.db, self.ann, data)
        self.assertEqual(image.errors, ["data.toml: owned.fixghost is no root (nothing references it, or the "
                                        "decomp defines it)"])
        errors = self.broken([("src/game/fixdata.c", "extern s32 fixowned;",
                               "extern s32 fixowned;\nextern s32 fixnowhere;"),
                              ("src/game/fixdata.c", "    return D_00100B00[0]",
                               "    return fixnowhere + D_00100B00[0]")])
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("fixnowhere has no retail address", errors[0])

    def test_end_to_end(self):
        cc = fb.find_cc()
        if cc is None:
            self.skipTest("no clang (set CC)")
        cb = next(f["ps2"]["offset"] for f in self.db["records"]["struct fixnode_s"]["fields"] if f["name"] == "cb")
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            fb.generate(out, self.db)
            exe = out / "data_harness.exe"
            build = fb.build(cc, out, fb.FIXTURE / "data_harness.c", exe)
            self.assertEqual(build.returncode, 0, build.stderr)
            (out / "good.elf").write_bytes(fixture_elf(cb))
            (out / "bad.elf").write_bytes(fixture_elf(cb, bad=True))
            run = subprocess.run([str(exe), str(out / "good.elf")], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertTrue(run.stdout.endswith("ok\n"), run.stdout)
            bad = subprocess.run([str(exe), str(out / "bad.elf"), "bad"], capture_output=True, text=True)
            self.assertEqual(bad.returncode, 0, bad.stdout + bad.stderr)
            self.assertEqual(bad.stdout.strip(), "fixnames[0]: 0x00100104 points into .text, not at data")
            (out / "trip.elf").write_bytes(fixture_elf(cb, trip=True))
            trip = subprocess.run([str(exe), str(out / "trip.elf"), "trip"], capture_output=True, text=True)
            self.assertEqual(trip.returncode, 0, trip.stdout + trip.stderr)
            self.assertEqual([line for line in trip.stdout.splitlines() if line.startswith("  ")], [
                "  fixroot +0xc (0x00100a0c): ELF 01 00 00 00, back 00 00 00 00; 1 bytes differ",
                "  fixstatic +0xc (0x00100c0c): ELF 05 00 00 00, back 04 00 00 00; 1 bytes differ"])


if __name__ == "__main__":
    unittest.main()
