"""gen_ps2types on tests/fixtures/mini (libclang, and clang or $CC)."""
import copy
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import gen_ps2types  # noqa: E402
import typedb  # noqa: E402

DECOMP = REPO / "tests" / "fixtures" / "mini" / "decomp"
FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror"] + (
    ["-mno-ms-bitfields"] if sys.platform == "win32" else [])


def find_cc():
    cc = os.environ.get("CC") or shutil.which("clang")
    return cc if cc and shutil.which(cc) else None


def record(kind, size, align, fields, where="x.h:1"):
    return {"kind": kind, "where": where, "ps2": {"size": size, "align": align},
            "host": {"size": size, "align": align}, "fields": fields}


def s32(name, offset):
    lay = {"offset": offset, "size": 4}
    return {"name": name, "type": "s32", "kind": "int", "signed": True, "size": 4,
            "ps2": lay, "host": lay}


class Names(unittest.TestCase):
    def errors(self, records):
        mirror, outputs = gen_ps2types.generate({"schema": typedb.SCHEMA, "records": records})
        self.assertEqual(outputs, {} if mirror.errors else outputs)
        return mirror.errors

    def test_mirror_names(self):
        self.assertEqual(gen_ps2types.mirror_name("struct nuvec_s"), "nw_ps2_nuvec_s")
        self.assertEqual(gen_ps2types.mirror_name("union variptr_u"), "nw_ps2_variptr_u")
        self.assertEqual(gen_ps2types.mirror_name("FixPair"), "nw_ps2_FixPair")

    def test_collision(self):
        self.assertEqual(self.errors({"struct foo": record("struct", 4, 4, [s32("a", 0)]),
                                      "union foo": record("union", 4, 4, [s32("b", 0)])}),
                         ["nw_ps2_foo would mirror struct foo and union foo"])

    def test_untagged_without_typedef(self):
        self.assertEqual(self.errors({"struct $x.h:3": record("struct", 4, 4, [s32("a", 0)])}),
                         ["struct $x.h:3: an untagged record without a typedef has no name"])

    def test_packed(self):
        self.assertEqual(self.errors({"struct p_s": record("struct", 4, 2, [s32("a", 0)])}),
                         ["x.h:1: struct p_s is aligned below its members (packed records are "
                          "not mirrored)"])

    def test_schema(self):
        with self.assertRaisesRegex(gen_ps2types.GenError, "schema 1, expected 2"):
            gen_ps2types.generate({"schema": 1, "records": {}})

    def test_list_outputs(self):
        run = subprocess.run([sys.executable, str(REPO / "tools" / "gen_ps2types.py"),
                              "--out", "gen", "--list-outputs"], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout.split(), ["nw_ps2types.h", "nw_ps2types_test.inc"])


@unittest.skipIf(typedb.cindex is None, "no libclang (tools/requirements.txt)")
class Fixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.db = typedb.build(DECOMP)
        cls.clean = copy.deepcopy(cls.db)
        del cls.clean["records"]["struct fixlong_s"]

    def test_long_is_an_error(self):
        mirror, outputs = gen_ps2types.generate(self.db)
        self.assertEqual(mirror.errors, ["nucore/fixtypes.h:31: struct fixlong_s.wide: long has "
                                         "no PS2 mirror type"])
        self.assertEqual(outputs, {})

    def test_render(self):
        mirror, outputs = gen_ps2types.generate(self.clean)
        self.assertEqual(mirror.errors, [])
        header = outputs["nw_ps2types.h"]
        node = header[header.index("struct nw_ps2_fixnode_s {"):]
        node = node[:node.index("\n};") + 3]
        self.assertEqual(node.splitlines(), [
            "struct nw_ps2_fixnode_s {",
            "    uint32_t next; /* struct fixnode_s * */",
            "    union {",
            "        int32_t i;",
            "        float f;",
            "        struct {",
            "            int16_t lo;",
            "            int16_t hi;",
            "        };",
            "    };",
            "    struct {",
            "        int32_t a;",
            "    } named;",
            "    uint64_t tag : 48;",
            "    uint64_t kind : 16;",
            "    uint32_t x : 3;",
            "    uint32_t : 2;",
            "    uint32_t y : 4;",
            "    int8_t name[2][3];",
            "    uint32_t cb; /* fixcb_t */",
            "};"])
        self.assertIn("    uint32_t addr; /* nuintptr */\n    uint32_t mode;\n    int32_t sign;\n",
                      header)
        self.assertIn('_Static_assert(offsetof(nw_ps2_fixnode_s, named.a) == 8 && '
                      'sizeof(((nw_ps2_fixnode_s *)0)->named.a) == 4, "struct fixnode_s.named.a");',
                      header)
        self.assertIn('_Static_assert(offsetof(nw_ps2_fixnode_s, hi) == 6', header)
        self.assertIn('{ "struct fixmisc_s.hi", sizeof(nw_ps2_fixmisc_s), nw_ps2_bf_1, 228u, 4u },',
                      outputs["nw_ps2types_test.inc"])
        self.assertEqual(len(mirror.bitfields), 6)

    def compile(self, db, out):
        mirror, outputs = gen_ps2types.generate(db)
        self.assertEqual(mirror.errors, [])
        for name, text in outputs.items():
            (out / name).write_text(text)
        exe = out / "test_ps2types.exe"
        return exe, subprocess.run([find_cc(), *FLAGS, f"-I{out}", "-o", str(exe),
                                    str(REPO / "tests" / "c" / "test_ps2types.c")],
                                   capture_output=True, text=True)

    def test_end_to_end(self):
        if find_cc() is None:
            self.skipTest("no clang (set CC)")
        with tempfile.TemporaryDirectory() as tmp:
            exe, build = self.compile(self.clean, Path(tmp))
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout.strip(), "6 bitfields, 0 wrong")

            db = copy.deepcopy(self.clean)
            db["records"]["struct fixmisc_s"]["fields"][8]["ps2"]["bit"] = 229
            exe, build = self.compile(db, Path(tmp))
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn("struct fixmisc_s.hi: bit 228 is 1, the PS2 range is [229, 233)",
                          run.stderr)

            db["records"]["FixPair"]["fields"][1]["ps2"]["offset"] = 8
            _exe, build = self.compile(db, Path(tmp))
            self.assertNotEqual(build.returncode, 0)
            self.assertIn('"FixPair.y"', build.stderr)


if __name__ == "__main__":
    unittest.main()
