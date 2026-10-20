"""Generator tests on the synthetic decomp in tests/fixtures/mini (libclang for
tu_filter, clang or $CC for the end-to-end test)."""
import os
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
import tu_filter  # noqa: E402

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
            "FixLocal": ("port", "port"),
            "FixCallLocal": ("decomp", "matching"),
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
            'override = ["FixLoad", "FixLocal"]': 'override = ["FixLoad", "NoSuchFn*"], keep = ["Fix*"]',
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


def wrap(parser, unit, wrapper=Path("w.wrap.c")):
    path = DECOMP / unit.source
    defs = parser.definitions(path)
    text, _renamed = tu_filter.render(unit, path.read_bytes(), defs, path.as_posix(),
                                      wrapper.as_posix())
    return text


@unittest.skipIf(tu_filter.cindex is None, "no libclang (tools/requirements.txt)")
class TuFilter(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = tu_filter.Parser(DECOMP)

    @classmethod
    def tearDownClass(cls):
        cls.parser.close()

    def parse(self, text):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "t.c"
            path.write_text(text)
            return self.parser.definitions(path)

    def test_wrapper(self):
        unit = next(u for u in load().units if u.name == "nucore/fixcore")
        errors = []
        tu_filter.check(unit, self.parser.definitions(DECOMP / unit.source), errors)
        self.assertEqual(errors, [])
        src = (DECOMP / unit.source).read_bytes().decode().replace("\r\n", "\n").split("\n")
        out = wrap(self.parser, unit).decode().replace("\r\n", "\n").split("\n")
        self.assertTrue(out[0].startswith("/* Generated by tools/tu_filter.py"))
        self.assertRegex(out[1], r'^#line 1 ".*/nucore/fixcore\.c"$')
        local = src.index("FixLocal(s32 v)")
        i = out.index("s32 FixLocal(s32 v);")
        self.assertRegex(out[i + 1], rf'^#line {local} ".*/nucore/fixcore\.c"$')
        self.assertEqual(out[i + 2:i + 4], ["s32", "FixLocal__ps2(s32 v)"])
        self.assertIn("s32 FixLoad__ps2(const char *name)", out)
        self.assertIn("    return FixLocal(v) + 1;", out)
        self.assertEqual(out[-4:], ['#line %d "w.wrap.c"' % (len(out) - 2),
                                    '#include "fixcore.stubs.inc"',
                                    '#include "fixcore.funcmap.inc"', ""])
        # Without the inserted prototype + #line pairs and the suffix, the source is unchanged.
        body = out[2:-4]
        inserted = {j for j, l in enumerate(body) if l.startswith("#line ")}
        inserted |= {j - 1 for j in inserted}
        kept = [l.replace("__ps2", "") for j, l in enumerate(body) if j not in inserted]
        self.assertEqual(kept + [""], src)

    def test_prototypes(self):
        defs = self.parse("struct v_s;\n"
                          "static inline struct v_s *\n"
                          "Multi(struct v_s *a, /* the count */\n"
                          "      int n)\n{ return a; }\n"
                          "int Knr(a, b) int a; char *b; { return a; }\n"
                          "void (*Fptr(int k))(int) { return 0; }\n"
                          "int Plain(void) { return 0; }\n")
        protos = {n: tu_filter.prototype(c, "Port") for n, c in defs.items()}
        self.assertEqual(protos, {
            "Multi": "static struct v_s * Port(struct v_s *a, int n);",
            "Knr": "int Port();",
            "Fptr": "void (*Port(int k))(int);",
            "Plain": "int Port(void);",
        })

    def test_rename_static_and_host_name(self):
        text = (b"static int Helper(int a)\n{\n    return a;\n}\n"
                b"int main(void)\n{\n    return Helper(1);\n}\n")
        unit = nwunits.Unit(index=0, name="game/x", mdebug="")
        helper = nwunits.Function(unit=unit, name="Helper", address=0, host="Helper",
                                  overridden=True)
        main = nwunits.Function(unit=unit, name="main", address=4, host="nw_game_main")
        unit.functions = [helper, main]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "x.c"
            path.write_bytes(text)
            defs = self.parser.definitions(path)
        out, renamed = tu_filter.filter_source(text, unit, defs, "x.c")
        self.assertEqual(renamed, ["Helper -> Helper__ps2", "main -> nw_game_main"])
        self.assertEqual(out.decode().split("\n")[:3],
                         ["static int Helper(int a);", '#line 1 "x.c"',
                          "static int Helper__ps2(int a)"])
        self.assertIn(b"int nw_game_main(void)\n{\n    return Helper(1);", out)

    def test_macro_name_is_refused(self):
        text = b"#define DEF(n) int n(void)\nDEF(Foo) { return 0; }\n"
        unit = nwunits.Unit(index=0, name="game/x", mdebug="")
        unit.functions = [nwunits.Function(unit=unit, name="Foo", address=0, host="Foo",
                                           overridden=True)]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "x.c"
            path.write_bytes(text)
            defs = self.parser.definitions(path)
        with self.assertRaisesRegex(tu_filter.FilterError, "not a plain token"):
            tu_filter.filter_source(text, unit, defs, "x.c")

    def test_disagreement_with_nwunits(self):
        unit = next(u for u in load().units if u.name == "nucore/fixcore")
        defs = self.parser.definitions(DECOMP / unit.source)
        del defs["FixAdd"]
        errors = []
        tu_filter.check(unit, defs, errors)
        self.assertEqual(errors, ["nucore/fixcore: only nwunits sees a definition of FixAdd"])


class EndToEnd(unittest.TestCase):
    def test_generated_code_links_and_runs(self):
        cc = find_cc()
        if cc is None:
            self.skipTest("no clang (set CC)")
        if tu_filter.cindex is None:
            self.skipTest("no libclang (tools/requirements.txt)")
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
            parser = tu_filter.Parser(DECOMP)
            try:
                for unit in built:
                    if unit.kind == "standalone":
                        sources.append(gen_stubs.output_path(out, unit))
                        continue
                    wrapper = tu_filter.output_path(out, unit)
                    tu_filter.write_if_changed(wrapper, wrap(parser, unit, wrapper))
                    sources.append(wrapper)
            finally:
                parser.close()
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
