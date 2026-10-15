"""Generator tests on the synthetic decomp in tests/fixtures/mini (needs clang
or $CC for the end-to-end test)."""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import gen_funcmap  # noqa: E402
import gen_stubs  # noqa: E402
import nwunits  # noqa: E402

FIXTURE = REPO / "tests" / "fixtures" / "mini"
DECOMP = FIXTURE / "decomp"
UNITS = FIXTURE / "port" / "units.toml"
PORT_SRC = FIXTURE / "port" / "src"
HOST_FLAGS = ["-std=gnu11", "-fgnu89-inline", "-fno-strict-aliasing", "-fwrapv",
              "-ffp-contract=off", "-fsigned-char", "-Wall", "-Wextra", "-Werror", "-DNW_HOST"]


def load(units=UNITS, **kw):
    return nwunits.load(DECOMP, units, PORT_SRC, **kw)


class UnitNames(unittest.TestCase):
    def test_mdebug_paths(self):
        cases = {
            r"..\nu2crash.ps2\nucore\nufile.c": "nucore/nufile",
            r"..\nu2crash.ps2\numath\numtxvu0.s": "numath/numtxvu0",
            r".\main.c": "game/main",
            "vu/vu.c": "game/vu",
            "/usr/local/sce/ee/lib/crt0.s": "sdk/sce/crt0",
            "../../../newlib/libm/math/s_sin.c": "sdk/newlib/libm/math/s_sin",
            "../../gcc/libgcc2.c": "sdk/gcc/libgcc2",
        }
        for path, name in cases.items():
            self.assertEqual(nwunits.unit_name(path), name, path)


class Prototypes(unittest.TestCase):
    def test_param_names(self):
        self.assertEqual(nwunits.param_name("struct nuvec_s *v"), "v")
        self.assertIsNone(nwunits.param_name("struct nuvec_s *"))
        self.assertIsNone(nwunits.param_name("struct nuvec_s"))
        self.assertIsNone(nwunits.param_name("unsigned int"))
        self.assertEqual(nwunits.param_name("unsigned int n"), "n")
        self.assertEqual(nwunits.param_name("u8 buf[16]"), "buf")
        self.assertEqual(nwunits.param_name("void (*cb)(int)"), "cb")
        self.assertIsNone(nwunits.param_name("void (*)(int)"))

    def test_definition_params(self):
        self.assertEqual(nwunits.definition_params(""), "void")
        self.assertEqual(nwunits.definition_params("void"), "void")
        self.assertEqual(nwunits.definition_params("int, float f, char *"),
                         "int nw_p0, float f, char *nw_p2")
        self.assertEqual(nwunits.definition_params("u8 [4], void (*)(int)"),
                         "u8 nw_p0[4], void (*nw_p1)(int)")
        self.assertEqual(nwunits.definition_params("const char *fmt, ..."), "const char *fmt, ...")

    def test_erase_type(self):
        typedefs = nwunits.scan_c("typedef struct x_s { int a; } X, *PX;\n"
                                  "typedef enum { A, B } Mode;\n"
                                  "typedef unsigned char uchar;\n"
                                  "typedef void (*Fn)(int);\n", "t.h").typedefs
        self.assertEqual(typedefs["uchar"], "unsigned char")
        self.assertEqual(nwunits.erase_type("const struct x_s *", typedefs), "void *")
        self.assertIsNone(nwunits.erase_type("struct x_s", typedefs))
        self.assertIsNone(nwunits.erase_type("X", typedefs))
        self.assertEqual(nwunits.erase_type("PX", typedefs), "void *")
        self.assertEqual(nwunits.erase_type("Mode", typedefs), "int")
        self.assertEqual(nwunits.erase_type("uchar", typedefs), "unsigned char")
        self.assertEqual(nwunits.erase_type("Fn", typedefs), "void *")
        self.assertEqual(nwunits.erase_type("enum mode_e", typedefs), "int")
        self.assertEqual(nwunits.erase_type("f32", typedefs), "f32")

    def test_scan_skips_comments_strings_and_host_branches(self):
        text = ('/* int Hidden(void); */\n'
                'const char *s = "int Quoted(void);";\n'
                '#ifdef NW_HOST\nint HostOnly(void);\n#else\nint PsOnly(void);\n#endif\n'
                'static struct a_s *Real(int x)\n{\n    return 0;\n}\n')
        decls = {d.name: d for d in nwunits.scan_c(text, "x.c").decls}
        self.assertEqual(sorted(decls), ["HostOnly", "Real"])
        self.assertTrue(decls["Real"].is_def and decls["Real"].static)
        self.assertEqual(decls["Real"].ret, "struct a_s *")
        self.assertEqual(decls["Real"].line, 8)


class Model(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.model = load()
        cls.fn = {f.name: f for f in cls.model.functions()}

    def test_no_errors(self):
        self.assertEqual(self.model.errors, [])
        self.assertEqual(self.model.warnings, [])

    def test_units(self):
        kinds = {u.name: u.kind for u in self.model.units}
        self.assertEqual(kinds, {"sdk/sce/crt0": "none", "nucore/fixcore": "wrapper",
                                 "numath/fixasm": "standalone", "game/fixgame": "wrapper",
                                 "game/fixdead": "standalone"})

    def test_providers(self):
        expect = {
            "_start": ("shim", ""),
            "FixAdd": ("decomp", "matching"),
            "FixNear": ("decomp", "near"),
            "FixMissing": ("stub", "stub"),
            "FixLoad": ("port", "port"),
            "FixHelper": ("decomp", "matching"),
            "FixGhost": ("stub", "stub"),
            "FixOther": ("stub", "stub"),
            "FixAsmA": ("stub", "stub"),
            "FixAsmB": ("port", "port"),
            "main": ("stub", "stub"),
            "FixGameTick": ("decomp", "equivalent"),
            "FixDeadA": ("stub", "stub"),
        }
        self.assertEqual({n: (f.provider, f.provenance) for n, f in self.fn.items()}, expect)

    def test_linkage_and_prototypes(self):
        fn = self.fn
        self.assertTrue(fn["FixHelper"].static)
        self.assertTrue(fn["FixGhost"].static)
        self.assertEqual(fn["FixGhost"].origin, "no declaration")
        self.assertFalse(fn["FixMissing"].static)
        self.assertEqual(fn["FixMissing"].origin, "include/nucore/fixcore.h:7")
        self.assertTrue(fn["FixOther"].erased)
        self.assertEqual(fn["FixOther"].proto.params, "void *, int, f32")
        self.assertEqual(fn["FixAsmA"].include, "numath/fixasm.h")
        self.assertEqual(fn["main"].host, "nw_game_main")
        self.assertFalse(fn["main"].static)
        self.assertTrue(fn["FixLoad"].overridden)

    def test_editor_gate(self):
        text = UNITS.read_text().replace('"game/fixdead" = { treatment = "dead" }',
                                         '"game/fixdead" = { treatment = "editor" }')
        with tempfile.TemporaryDirectory() as tmp:
            units = Path(tmp) / "units.toml"
            units.write_text(text)
            off = {u.name: u.kind for u in load(units).units}
            on = {u.name: u.kind for u in load(units, editor=True).units}
        self.assertEqual(off["game/fixdead"], "standalone")
        self.assertEqual(on["game/fixdead"], "wrapper")

    def test_units_toml_errors(self):
        bad = {
            '"game/fixdead" = { treatment = "dead" }': '"game/fixdead" = { treatment = "gone" }',
            'override = ["FixLoad"]': 'override = ["FixLoad", "NoSuchFn*"], keep = ["Fix*"]',
            'rename = { main = "nw_game_main" }': 'rename = { mian = "nw_game_main" }',
        }
        text = UNITS.read_text()
        for old, new in bad.items():
            text = text.replace(old, new)
        text += '"game/nosuchunit" = { treatment = "compile" }\n"sdk/x*" = { treatment = "shim", keep = ["a"] }\n'
        with tempfile.TemporaryDirectory() as tmp:
            units = Path(tmp) / "units.toml"
            units.write_text(text)
            errors = "\n".join(load(units).errors)
        for needle in ("treatment must be one of", "'NoSuchFn*' matches no function",
                       "rename of unknown function 'mian'", "units.game/nosuchunit: matches no decomp unit",
                       "a glob key takes only a treatment"):
            self.assertIn(needle, errors)

    def test_missing_unit_entry(self):
        text = UNITS.read_text().replace('"sdk/*" = { treatment = "shim" }\n', "")
        with tempfile.TemporaryDirectory() as tmp:
            units = Path(tmp) / "units.toml"
            units.write_text(text)
            self.assertIn("unit sdk/sce/crt0: no entry in units.toml", load(units).errors)

    def test_port_collision(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "src"
            src.mkdir()
            (src / "clash.c").write_text("int FixAdd(int a, int b) { return a - b; }\n")
            model = nwunits.load(DECOMP, UNITS, src)
        self.assertTrue(any("the port defines FixAdd" in e for e in model.errors))


def find_cc():
    cc = os.environ.get("CC") or shutil.which("clang")
    return cc if cc and shutil.which(cc) else None


class EndToEnd(unittest.TestCase):
    def test_generated_code_links_and_runs(self):
        cc = find_cc()
        if cc is None:
            self.skipTest("no clang (set CC)")
        model = load()
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            for unit in model.units:
                if unit.kind == "none":
                    continue
                nwunits.write_if_changed(gen_stubs.output_path(out, unit), gen_stubs.render(unit))
                nwunits.write_if_changed(out / (unit.name + ".funcmap.inc"),
                                         gen_funcmap.render_unit(unit))
            built = [u for u in model.units if u.kind != "none"]
            nwunits.write_if_changed(out / "funcmap.c", gen_funcmap.render_index(built))

            sources = [out / "funcmap.c", REPO / "src" / "port" / "stub.c",
                       REPO / "src" / "port" / "funcmap.c", FIXTURE / "harness.c",
                       *sorted(PORT_SRC.glob("*.c"))]
            for unit in built:
                if unit.kind == "standalone":
                    sources.append(gen_stubs.output_path(out, unit))
                    continue
                # Stand-in for tools/tu_filter.
                text = (DECOMP / unit.source).read_text().split("\n")
                for f in unit.functions:
                    if f.definition is not None and f.overridden:
                        i = f.definition.line - 1
                        text[i] = re.sub(rf"\b{f.name}\s*\(", f"{f.name}__ps2(", text[i], count=1)
                filtered = out / (unit.name + ".filtered.c")
                filtered.write_text("\n".join(text))
                wrapper = out / (unit.name + ".wrap.c")
                wrapper.write_text(f'#include "{filtered.name}"\n'
                                   f'#include "{unit.name.rsplit("/", 1)[1]}.stubs.inc"\n'
                                   f'#include "{unit.name.rsplit("/", 1)[1]}.funcmap.inc"\n')
                sources.append(wrapper)
            exe = out / "harness.exe"
            flags = HOST_FLAGS + ["-Wno-unused-function", f"-I{DECOMP / 'include'}",
                                  f"-I{REPO / 'src'}"]
            build = subprocess.run([cc, *flags, "-o", str(exe), *map(str, sources)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)

            env = dict(os.environ, NUWORLD_STUBS="continue")
            run = subprocess.run([str(exe)], capture_output=True, text=True, env=env)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("FixMissing", run.stdout)
            self.assertIn("stub hit: FixMissing (nucore/fixcore, 0x00100140)", run.stderr)

            env.pop("NUWORLD_STUBS")
            ghost = subprocess.run([str(exe), "ghost"], capture_output=True, text=True, env=env)
            self.assertNotEqual(ghost.returncode, 0)
            self.assertIn("stub hit: FixGhost", ghost.stderr)


if __name__ == "__main__":
    unittest.main()
