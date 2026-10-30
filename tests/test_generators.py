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
sys.path.insert(0, str(REPO / "tests"))
import fixture_build  # noqa: E402
import gen_stubs  # noqa: E402
import nwunits  # noqa: E402
import stub_hits  # noqa: E402
import tu_filter  # noqa: E402

FIXTURE = REPO / "tests" / "fixtures" / "mini"
DECOMP = FIXTURE / "decomp"
UNITS = FIXTURE / "port" / "units.toml"
PORT_SRC = FIXTURE / "port" / "src"


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
                                 "game/fixdead": "standalone", "game/fixdata": "wrapper"})

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
            "FixDataUse": ("decomp", "matching"),
            "FixDataHidden": ("stub", "stub"),
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

    def test_stub_reasons(self):
        why = {n: gen_stubs.why(f) for n, f in self.fn.items() if f.provider == "stub"}
        self.assertEqual(why["FixMissing"], "no C in the decomp yet")
        self.assertEqual(why["FixDataHidden"], "override without a port body")
        self.assertEqual(why["FixAsmA"], "hle unit without a port body")
        self.assertEqual(why["FixDeadA"], "dead unit")

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

    def test_editor_runtime(self):
        text = UNITS.read_text().replace('"nucore/fixcore" = { treatment = "compile",',
                                         '"nucore/fixcore" = { treatment = "editor", runtime = ["FixAdd", "FixLocal"], '
                                         'called = ["FixOther"],')
        with tempfile.TemporaryDirectory() as tmp:
            units = Path(tmp) / "units.toml"
            units.write_text(text)
            off, on = load(units), load(units, editor=True)
        self.assertEqual(off.errors, [])
        fn = {f.name: f for f in off.functions()}
        self.assertEqual(fn["FixAdd"].unit.kind, "wrapper")
        self.assertEqual(fn["FixAdd"].provider, "decomp")
        self.assertEqual((fn["FixNear"].provider, gen_stubs.why(fn["FixNear"])), ("stub", "editor unit"))
        self.assertEqual(fn["FixLoad"].provider, "port")
        self.assertEqual({f.name: f.provider for f in on.functions()}["FixNear"], "decomp")

    def test_calls_into_dead_units(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp = Path(tmp) / "decomp"
            shutil.copytree(DECOMP, decomp)
            src = decomp / "src" / "game" / "fixgame.c"
            src.write_text(src.read_text().replace(
                "void FixGameTick(struct fixobj_s *obj)\n{\n",
                "static void FixGameHelp(void)\n{\n    FixDeadA(0);\n}\n\n"
                "void FixGameTick(struct fixobj_s *obj)\n{\n    FixGameHelp();\n"))
            units = Path(tmp) / "units.toml"
            units.write_text(UNITS.read_text())
            plain = nwunits.load(decomp, units, PORT_SRC)
            units.write_text(UNITS.read_text().replace('"game/fixdead" = { treatment = "dead" }',
                                                       '"game/fixdead" = { treatment = "dead", called = ["FixDeadA"] }'))
            allowed = nwunits.load(decomp, units, PORT_SRC)
            unused = load(units)
            units.write_text(UNITS.read_text().replace('"game/fixdata" = { treatment = "compile",',
                                                       '"game/fixdata" = { treatment = "compile", called = ["FixDataUse"],'))
            misplaced = load(units)
        self.assertEqual(plain.errors, ["src/game/fixgame.c:11: FixGameHelp calls FixDeadA, a stub of the dead unit "
                                        "game/fixdead; compile it (runtime) or list it under called"])
        self.assertEqual((allowed.errors, allowed.warnings), ([], []))
        self.assertEqual(unused.warnings, ["unit game/fixdead: called 'FixDeadA': no compiled code calls it"])
        self.assertIn("unit game/fixdata: called on a compile unit (editor and dead only)", misplaced.errors)

    def test_units_toml_errors(self):
        bad = {
            '"game/fixdead" = { treatment = "dead" }': '"game/fixdead" = { treatment = "gone" }',
            'override = ["FixLoad"], wrap = ["FixLocal"]':
                'override = ["FixLoad", "NoSuchFn*"], keep = ["Fix*"], wrap = ["FixLocal", "NoSuchWrap"]',
            'rename = { main = "nw_game_main" }': 'rename = { mian = "nw_game_main" }',
            '"game/fixdata" = { treatment = "compile",': '"game/fixdata" = { treatment = "compile", runtime = ["FixDataUse"],',
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
                       "a glob key takes only a treatment", "unit game/fixdata: runtime on a compile unit",
                       "wrap of unknown function 'NoSuchWrap'"):
            self.assertIn(needle, errors)

    def test_duplicate_global(self):
        text = UNITS.read_text().replace('rename = { main = "nw_game_main" }',
                                         'rename = { main = "nw_game_main", FixGameTick = "FixAdd" }')
        with tempfile.TemporaryDirectory() as tmp:
            units = Path(tmp) / "units.toml"
            units.write_text(text)
            errors = load(units).errors
        self.assertIn("global FixAdd is defined by several units: nucore/fixcore, game/fixgame; "
                      "rename it in all but one", errors)

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

    def test_port_copy_of_decomp_body(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "src"
            src.mkdir()
            text = (PORT_SRC / "fiximpl.c").read_text().replace("(void)name;\n    return 42;",
                                                                "/* copied */\n    return  name[0] ;")
            (src / "fiximpl.c").write_text(text)
            model = nwunits.load(DECOMP, UNITS, src)
        self.assertEqual(model.errors, [])
        self.assertEqual(model.warnings, ["src/fiximpl.c:4: the port's FixLoad is identical to the decomp C "
                                          "(nucore/fixcore); drop the override"])


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
        self.assertEqual(out[1], '#include "port/nw_prelude.h"')
        self.assertEqual(out[2], '#include "port/stub.h"')
        self.assertRegex(out[3], r'^#line 1 ".*/nucore/fixcore\.c"$')
        # The near-match FixNear counts its calls on the line of its brace.
        probe = '{ NW_NEAR("nucore/fixcore", "FixNear", 0x00100120u);'
        self.assertEqual(out[out.index("s32 FixNear(s32 a)") + 1], probe)
        self.assertEqual(sum("NW_NEAR" in line for line in out), 1)
        out[out.index(probe)] = "{"
        # The wrapped FixLocal keeps its body as FixLocal__retail, behind a prototype of the port's FixLocal.
        start = src.index("FixLocal(s32 v)")            # 1-based, the line of its "s32"
        i = out.index("s32 FixLocal(s32 v);")
        self.assertRegex(out[i + 1], rf'^#line {start} ".*/nucore/fixcore\.c"$')
        self.assertEqual(out[i + 2:i + 4], ["s32", "FixLocal__retail(s32 v)"])
        self.assertIn("s32 FixLoad(const char *name);", out)
        self.assertIn("    return FixLocal(v) + 1;", out)
        self.assertEqual(out[-5:], ['#line %d "w.wrap.c"' % (len(out) - 3),
                                    '#include "fixcore.stubs.inc"',
                                    '#include "fixcore.funcmap.inc"',
                                    '#include "fixcore.data.inc"', ""])
        # Every other line maps through #line to the same source line; the only source
        # lines left out are the overridden definition.
        body, cur, seen = out[4:-5], 1, set()
        for j, line in enumerate(body):
            if line.startswith("#line "):
                cur = int(line.split()[1])
                continue
            if j + 1 < len(body) and body[j + 1].startswith("#line "):
                continue                                   # an inserted prototype
            self.assertEqual(line.replace("FixLocal__retail", "FixLocal"), src[cur - 1])
            seen.add(cur)
            cur += 1
        gaps = [src[k - 1] for k in range(1, len(src)) if k not in seen]
        self.assertEqual(gaps, ["s32 FixLoad(const char *name)", "{", "    return name[0];", "}"])

    def test_expose(self):
        text = UNITS.read_text().replace('wrap = ["FixLocal"]', 'wrap = ["FixLocal", "FixHelper"], expose = ["FixHelper"]')
        text = text.replace('"game/fixdata" = { treatment = "compile",',
                            '"game/fixdata" = { treatment = "compile", expose = ["fixstatic"],')
        with tempfile.TemporaryDirectory() as tmp:
            units = Path(tmp) / "units.toml"
            units.write_text(text)
            src = Path(tmp) / "src"
            shutil.copytree(PORT_SRC, src)
            with (src / "fiximpl.c").open("a") as f:
                f.write("s32 FixHelper(s32 v) { return v; }\n")
            model = nwunits.load(DECOMP, units, src)
        self.assertEqual(model.errors, [])
        units = {u.name: u for u in model.units}
        helper = next(f for f in units["nucore/fixcore"].functions if f.name == "FixHelper")
        self.assertEqual((helper.static, helper.wrapped), (False, True))
        for unit, line in ((units["game/fixdata"], "s32 fixstatic[4] = { 1, 2, 3, 4 };"),
                           (units["nucore/fixcore"], "s32 FixHelper__retail(s32 v)")):
            path = DECOMP / unit.source
            exposed = self.parser.static_tokens(path, unit.expose)
            text, changes = tu_filter.render(unit, path.read_bytes(), self.parser.definitions(path),
                                             path.as_posix(), "w.wrap.c", exposed)
            out = text.decode().replace("\r\n", "\n").split("\n")
            self.assertIn(line, out)
            self.assertIn(f"{unit.expose[0]} exposed", changes)
        self.assertIn("s32 FixHelper(s32 v);", out)
        header = tu_filter.retail_header(units["nucore/fixcore"], self.parser.definitions(path)).split("\n")
        self.assertIn("s32 FixHelper(s32 v);", header)  # NuWorld's FixHelper must keep the decomp's signature
        self.assertIn("s32 FixHelper__retail(s32 v);", header)
        with self.assertRaisesRegex(tu_filter.FilterError, "no file-scope static FixAdd"):
            self.parser.static_tokens(DECOMP / units["nucore/fixcore"].source, ["FixAdd"])

    def test_prelude_skips_sdk_units(self):
        self.assertTrue(tu_filter.wants_prelude(nwunits.Unit(0, "game/main", "")))
        self.assertFalse(tu_filter.wants_prelude(
            nwunits.Unit(0, "sdk/newlib/libm/math/s_isinf", "")))

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
        out, changes = tu_filter.filter_source(text, unit, defs, "x.c")
        self.assertEqual(changes, ["Helper dropped", "main -> nw_game_main"])
        self.assertEqual(out.decode().split("\n")[:3],
                         ["static int Helper(int a);", '#line 5 "x.c"', "int nw_game_main(void)"])
        self.assertIn(b"int nw_game_main(void)\n{\n    return Helper(1);", out)

    def filtered(self, text, name, host, overridden=False):
        unit = nwunits.Unit(index=0, name="nu3d/x", mdebug="")
        unit.functions = [nwunits.Function(unit=unit, name=name, address=0, host=host,
                                           overridden=overridden)]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "x.c"
            path.write_bytes(text)
            defs = self.parser.definitions(path)
            return tu_filter.filter_source(text, unit, defs, "x.c")[0]

    def test_rename_every_reference(self):
        text = (b"struct s_s { int Vtx; };\n"
                b"void Vtx(int fh, struct s_s *s);\n"
                b"void Caller(struct s_s *s)\n{\n    Vtx(s->Vtx, s);\n"
                b"    { int Vtx = 0; (void)Vtx; }\n}\n"
                b'inline void Vtx(int fh, struct s_s *s)\n{\n    (void)fh; (void)s; (void)"Vtx";\n}\n')
        expect = (b"struct s_s { int Vtx; };\n"
                  b"void Vtx__x(int fh, struct s_s *s);\n"
                  b"void Caller(struct s_s *s)\n{\n    Vtx__x(s->Vtx, s);\n"
                  b"    { int Vtx = 0; (void)Vtx; }\n}\n"
                  b'inline void Vtx__x(int fh, struct s_s *s)\n{\n    (void)fh; (void)s; (void)"Vtx";\n}\n')
        self.assertEqual(self.filtered(text, "Vtx", "Vtx__x"), expect)

    def test_rename_through_macro_is_refused(self):
        text = (b"void Foo(void);\n#define CALL() Foo()\n"
                b"void Foo(void)\n{\n}\nvoid Bar(void)\n{\n    CALL();\n}\n")
        with self.assertRaisesRegex(tu_filter.FilterError, "macro"):
            self.filtered(text, "Foo", "Foo2")

    def test_directive_in_dropped_definition_is_refused(self):
        text = b"int Foo(void)\n{\n#if 1\n    return 1;\n#endif\n}\n"
        with self.assertRaisesRegex(tu_filter.FilterError, "preprocessor directive"):
            self.filtered(text, "Foo", "Foo", overridden=True)

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


class ListOutputs(unittest.TestCase):
    def outputs(self, tool):
        run = subprocess.run([sys.executable, str(REPO / "tools" / tool), "--decomp", str(DECOMP),
                              "--units", str(UNITS), "--port-src", str(PORT_SRC),
                              "--out", "gen", "--list-outputs"],
                             capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        return run.stdout.split()

    def test_generators(self):
        self.assertEqual(self.outputs("tu_filter.py"),
                         ["nucore/fixcore.wrap.c", "game/fixgame.wrap.c", "game/fixdata.wrap.c",
                          "nucore/fixcore.retail.h"])
        self.assertEqual(self.outputs("gen_stubs.py"),
                         ["nucore/fixcore.stubs.inc", "numath/fixasm.stubs.c",
                          "game/fixgame.stubs.inc", "game/fixdead.stubs.c", "game/fixdata.stubs.inc"])
        self.assertEqual(self.outputs("gen_funcmap.py"),
                         ["nucore/fixcore.funcmap.inc", "numath/fixasm.funcmap.inc",
                          "game/fixgame.funcmap.inc", "game/fixdead.funcmap.inc",
                          "game/fixdata.funcmap.inc", "funcmap.c"])


class StubHits(unittest.TestCase):
    def test_parse_runs(self):
        text = ("00001 NuDebugMsg - nufile.c(542) : Background Load Thread Created\n"
                "boot-check: stub and near-match hits in first-hit order (order, ...):\n"
                "   1 near nucore/fixcore                   FixNear      0x00100120 4\n"
                "   2 stub game/fixdead                     FixDeadA     0x00100400 1\n"
                "boot-check: 1 stubs and 1 near-match functions hit; the game thread parked\n"
                "   1 stub nucore/fixcore                   FixMissing   0x00100140 2\n")
        self.assertEqual(stub_hits.parse_runs(text),
                         [[(1, "nucore/fixcore", "FixNear", 0x00100120, 4),
                           (2, "game/fixdead", "FixDeadA", 0x00100400, 1)],
                          [(1, "nucore/fixcore", "FixMissing", 0x00100140, 2)]])

    def test_merge_ranks_by_run_then_order(self):
        runs = [[(1, "u", "A", 1, 2), (2, "u", "B", 2, 1)], [(1, "u", "C", 3, 1), (2, "u", "A", 1, 5)]]
        hits = stub_hits.merge(runs)
        self.assertEqual({h.name: (h.rank, h.hits, h.runs) for h in hits.values()},
                         {"A": (1, 7, 2), "B": (2, 1, 1), "C": (3, 1, 1)})

    def test_todo(self):
        todo = {f.name: stub_hits.todo(f) for f in load().functions()}
        self.assertEqual(todo, {
            "_start": "port:shim", "FixAdd": "done", "FixNear": "decomp:near", "FixMissing": "decomp:stub",
            "FixLoad": "done", "FixLocal": "done", "FixCallLocal": "done", "FixHelper": "done",
            "FixGhost": "decomp:stub", "FixOther": "decomp:stub", "FixAsmA": "port:hle", "FixAsmB": "done",
            "main": "decomp:stub", "FixGameTick": "done", "FixDeadA": "dead", "FixDataUse": "done",
            "FixDataHidden": "port:override"})
        self.assertEqual(stub_hits.todo(None, "sdk"), "port:shim")
        self.assertEqual(stub_hits.todo(None, "nu3d/gone"), "unknown")

    def test_worklist_follows_the_hits(self):
        runs = stub_hits.parse_runs("   1 stub game/fixgame FixOther 0x001001c0 1\n"
                                    "   2 stub game/fixdead FixDeadA 0x00100400 3\n"
                                    "   3 near nucore/fixcore FixNear 0x00100120 1\n")
        out = stub_hits.render(load(), stub_hits.merge(runs), len(runs))
        hits, work = out.split("\n\n")
        self.assertIn("   2  FixDeadA ", hits)
        self.assertIn("1 dead, 1 decomp:near, 1 decomp:stub", hits)
        rows = [line.split() for line in work.splitlines()[2:]]
        self.assertEqual([(r[0], r[1]) for r in rows],
                         [("1", "FixOther"), ("3", "FixNear"), ("-", "FixMissing"), ("-", "FixGhost"),
                          ("-", "main")])


class EndToEnd(unittest.TestCase):
    def test_generated_code_links_and_runs(self):
        cc = fixture_build.find_cc()
        if cc is None:
            self.skipTest("no clang (set CC)")
        if tu_filter.cindex is None:
            self.skipTest("no libclang (tools/requirements.txt)")
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            fixture_build.generate(out)
            exe = out / "harness.exe"
            build = fixture_build.build(cc, out, FIXTURE / "harness.c", exe)
            self.assertEqual(build.returncode, 0, build.stderr)

            env = dict(os.environ, NUWORLD_STUBS="continue")
            run = subprocess.run([str(exe)], capture_output=True, text=True, env=env)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("stub hit: FixMissing (nucore/fixcore, 0x00100140): no C in the decomp yet", run.stderr)
            self.assertIn("stub hit: FixDeadA (game/fixdead, 0x00100400): dead unit", run.stderr)
            self.assertNotIn("FixNear", run.stderr)
            # The near-match FixNear first, then the stub it calls, then FixDeadA: tools/stub_hits.py reads it.
            runs = stub_hits.parse_runs(run.stdout)
            self.assertEqual(runs, [[(1, "nucore/fixcore", "FixNear", 0x00100120, 1),
                                     (2, "nucore/fixcore", "FixMissing", 0x00100140, 1),
                                     (3, "game/fixdead", "FixDeadA", 0x00100400, 1)]])

            env.pop("NUWORLD_STUBS")
            ghost = subprocess.run([str(exe), "ghost"], capture_output=True, text=True, env=env)
            self.assertNotEqual(ghost.returncode, 0)
            self.assertIn("stub hit: FixGhost", ghost.stderr)


if __name__ == "__main__":
    unittest.main()
