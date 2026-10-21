"""gen_converters on tests/fixtures/mini (libclang, and clang or $CC)."""
import copy
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import gen_converters  # noqa: E402
import gen_ps2types  # noqa: E402
import typedb  # noqa: E402
from test_gen_ps2types import find_cc  # noqa: E402

FIXTURE = REPO / "tests" / "fixtures" / "mini"
DECOMP = FIXTURE / "decomp"
ANNOTATIONS = FIXTURE / "port" / "convert.toml"
FLAGS = ["-std=gnu11", "-fgnu89-inline", "-fno-strict-aliasing", "-fwrapv", "-fsigned-char", "-DNW_HOST",
         "-Wall", "-Wextra", "-Werror"] + (["-mno-ms-bitfields"] if sys.platform == "win32" else [])


def annotations(text):
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "convert.toml"
        path.write_text("schema = 1\n" + text)
        return gen_converters.load_annotations(path)


class Annotations(unittest.TestCase):
    def test_entries(self):
        ann, errors = annotations('[unions]\n"union a_u" = { member = "i" }\n'
                                  '[arrays]\n"struct b_s.tail" = { hook = true }\n')
        self.assertEqual(errors, [])
        self.assertEqual(ann, {"union a_u": {"member": "i", "table": "unions"},
                               "struct b_s.tail": {"hook": True, "table": "arrays"}})

    def test_errors(self):
        _ann, errors = annotations('[unions]\n"union a_u" = { hook = false }\n'
                                   '[arrays]\n"struct b_s.tail" = { member = "x" }\n[other]\n')
        self.assertEqual(errors, [
            'convert.toml: unions.union a_u: takes member = "<name>" or hook = true',
            "convert.toml: arrays.struct b_s.tail: takes hook = true",
            "convert.toml: unknown tables ['other']"])

    def test_list_outputs(self):
        run = subprocess.run([sys.executable, str(REPO / "tools" / "gen_converters.py"),
                              "--out", "gen", "--list-outputs"], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout.split(), ["nw_convert.h", "nw_convert.c", "nw_convert_test.inc"])


@unittest.skipIf(typedb.cindex is None, "no libclang (tools/requirements.txt)")
class Fixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.db = typedb.build(DECOMP)
        cls.clean = copy.deepcopy(cls.db)
        del cls.clean["records"]["struct fixlong_s"]
        cls.ann, errors = gen_converters.load_annotations(ANNOTATIONS)
        assert errors == [], errors

    def errors(self, db, ann):
        gen, outputs = gen_converters.generate(db, ann)
        self.assertEqual(outputs == {}, bool(gen.errors))
        return gen.errors

    def test_long_is_an_error(self):
        self.assertEqual(self.errors(self.db, self.ann),
                         ["nucore/fixtypes.h:31: struct fixlong_s.wide: long has no PS2 mirror type"])

    def test_unannotated(self):
        mixed = "a union that mixes pointers with other members needs a member or a hook in " \
                "port/convert.toml"
        self.assertEqual(self.errors(self.clean, {}), [
            "nucore/fixtypes.h:96: struct fixunion_s.tail: a [0]/[] array needs a hook in "
            "port/convert.toml",
            f"nucore/fixtypes.h:73: union fixhook_u: {mixed}",
            f"nucore/fixtypes.h:68: union fixmixed_u: {mixed}"])

    def test_wrong_annotations(self):
        ann = dict(self.ann)
        ann["union fixptr_u"] = {"member": "any", "table": "unions"}
        ann["union fixmixed_u"] = {"member": "nope", "table": "unions"}
        ann["struct gone_s.tail"] = {"hook": True, "table": "arrays"}
        self.assertEqual(self.errors(self.clean, ann), [
            "nucore/fixtypes.h:68: union fixmixed_u: port/convert.toml names no member 'nope'",
            "nucore/fixtypes.h:57: union fixptr_u: needs no annotation in port/convert.toml",
            "convert.toml: arrays.struct gone_s.tail matches nothing to annotate"])

    def test_name_collision(self):
        db = copy.deepcopy(self.clean)
        db["records"]["union FixPair"] = db["records"]["FixPair"]
        self.assertEqual(self.errors(db, self.ann),
                         ["nw_ps2_FixPair would mirror FixPair and union FixPair"])

    def test_render(self):
        gen, outputs = gen_converters.generate(self.clean, self.ann)
        self.assertEqual(gen.errors, [])
        src = outputs["nw_convert.c"]
        for line in [
            '    dst->next = nw_ptr_in(ctx, src->next, &nw_type_fixnode_s, "next");',
            "    NW_XBYTES(dst, 8, src, 4, 4);",
            "    dst->named.a = src->named.a;",
            "    dst->tag = src->tag;",
            '    dst->cb = (__typeof__(dst->cb))nw_fn_in(ctx, src->cb, "cb");',
            '    dst->cb = nw_fn_out(ctx, (NwFuncPtr)src->cb, "cb");',
            '    dst->nodes = nw_ptr_in(ctx, src->nodes, &nw_type_p_fixnode_s, "nodes");',
            '    dst->rows = nw_ptr_in(ctx, src->rows, &nw_type_a4_s32, "rows");',
            '    dst->cbs = nw_ptr_in(ctx, src->cbs, &nw_type_fnptr, "cbs");',
            '    dst->addr = ctx->addr_in(ctx, src->addr, 0, "addr");',
            '    dst->opaque = nw_ptr_in(ctx, src->opaque, &nw_type_fixopaque_s, "opaque");',
            '    dst->any = nw_ptr_in(ctx, src->any, &nw_type_void, "any");',
            '    NW_XNEST(ctx, "ref", -1, nw_x_fixref_s(&dst->ref, &src->ref, ctx));',
            "    dst->i = src->i;",
            "    NW_XCOPY(dst->plain, src->plain);",
            '        NW_XNEST(ctx, "nodes", (int)i, nw_x_fixnode_s(&dst->nodes[i], &src->nodes[i], ctx));',
            "    NW_XCOPY(dst->pairs, src->pairs);",
            '            dst->grid[i][j] = nw_ptr_in(ctx, src->grid[i][j], &nw_type_fixnode_s, "grid");',
            "    nw_xh_fixunion_s__tail(dst, src, ctx);",
            "    nw_xh_fixhook_u(dst, src, ctx);",
            "    memcpy(dst, src, sizeof *dst);",
            '_Static_assert(offsetof(struct fixnode_s, named.a) == 12 && '
            'sizeof(((struct fixnode_s *)0)->named.a) == 4, "struct fixnode_s.named.a");',
            'const NwType nw_type_p_fixnode_s = { "struct fixnode_s *", 4, 4, sizeof(void *), '
            "_Alignof(void *), 0, nw_xv_p_fixnode_s, nw_xrv_p_fixnode_s, nw_spans_p_fixnode_s, 1u };",
            'const NwType nw_type_fixopaque_s = { "struct fixopaque_s", 0, 1, 0, 1, NW_TYPE_OPAQUE, NULL, '
            "NULL, NULL, 0u };",
        ]:
            self.assertIn(line + "\n", src)
        spans = src[src.index("static const NwSpan nw_spans_fixunion_s[]"):]
        self.assertEqual(spans[:spans.index("};")].splitlines()[1:], [
            "    { 0u, 0u, 4u, 8u, 1u, NW_SPAN_RECORD, &nw_type_fixptr_u },",
            "    { 4u, 8u, 8u, 16u, 1u, NW_SPAN_RECORD, &nw_type_fixref_s },",
            "    { 12u, 24u, 4u, 8u, 1u, NW_SPAN_RECORD, &nw_type_fixmixed_u },",
            "    { 16u, 32u, 4u, 4u, 1u, NW_SPAN_BYTES, NULL },",
            "    { 24u, 40u, 40u, 40u, 2u, NW_SPAN_RECORD, &nw_type_fixnode_s },",
            "    { 104u, 120u, 32u, 32u, 1u, NW_SPAN_BYTES, NULL },",
            "    { 136u, 152u, 4u, 8u, 6u, NW_SPAN_SLOT, NULL },",
            "    { 160u, 200u, 4u, 4u, 1u, NW_SPAN_BYTES, NULL },"])
        self.assertEqual(sorted(gen.hooks), ["fixhook_u", "fixunion_s__tail"])
        self.assertIn("void nw_xh_fixunion_s__tail(struct fixunion_s *dst, const nw_ps2_fixunion_s *src, "
                      "NwXCtx *ctx);", outputs["nw_convert.h"])

    def test_end_to_end(self):
        cc = find_cc()
        if cc is None:
            self.skipTest("no clang (set CC)")
        mirror, mirror_out = gen_ps2types.generate(self.clean)
        gen, outputs = gen_converters.generate(self.clean, self.ann)
        self.assertEqual(mirror.errors + gen.errors, [])
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            for name, text in {**mirror_out, **outputs}.items():
                (out / name).write_text(text)
            exe = out / "test_convert.exe"
            sources = [REPO / "tests" / "c" / "test_convert.c", out / "nw_convert.c",
                       REPO / "src" / "data" / "convert.c", REPO / "src" / "port" / "funcmap.c",
                       FIXTURE / "port" / "xhooks.c"]
            build = subprocess.run([cc, *FLAGS, f"-I{DECOMP / 'include'}", f"-I{REPO / 'src'}", f"-I{out}",
                                    "-o", str(exe), *map(str, sources)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout.strip(), "9 types: 8 round trips, 1 stop at a hook, "
                                                 "5 error paths, 0 failures")


if __name__ == "__main__":
    unittest.main()
