"""typedb_probe without a compiler: STABS joining, probe source, layout compare
(needs the decomp submodule for declib)."""
import copy
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import typedb_probe  # noqa: E402

try:
    from declib.stabs import TypeTable
except ImportError:
    TypeTable = None


def field(name, kind, bit, bits, **extra):
    lay = {"offset": bit // 8, "size": bits // 8}
    if bits % 8 or extra.pop("bitfield", False):
        lay.update(bit=bit, bits=bits, size=4)
    return {"name": name, "kind": kind, "ps2": lay, **extra}


DB = {
    "headers": ["nucore/fix.h"],
    "records": {
        "struct fix_s": {"where": "nucore/fix.h:3", "ps2": {"size": 12}, "fields": [
            field("next", "pointer", 0, 32),
            field("", "record", 32, 32, ref="struct fix_s$1"),
            field("tag", "int", 64, 3),
            field("", "int", 67, 2),
            field("arr", "array", 72, 16, element={"kind": "int"}),
        ]},
        "struct fix_s$1": {"where": "nucore/fix.h:5", "ps2": {"size": 4}, "fields": [
            field("i", "int", 0, 32), field("f", "float", 0, 32)]},
        "FixPair": {"where": "nucore/fix.h:12", "ps2": {"size": 8}, "fields": [
            field("x", "float", 0, 32), field("y", "float", 32, 32)]},
    },
}
STABS = [
    "int:t1=r1;-2147483648;2147483647;",
    "float:t2=r1;4;0;",
    "char:t3=r3;0;127;",
    "fix_s:T20=s12next:21=*20,0,32;:22=u4i:1,0,32;f:2,0,32;;,32,32;tag:1,64,3;"
    "arr:23=ar1;0;1;3,72,16;;",
    "FixPair:t24=s8x:2,0,32;y:2,32,32;;",
]


class Stabs(unittest.TestCase):
    def test_continuations_are_joined(self):
        asm = ('\t.stabs "a:T20=s8x:1,0,32;\\\\",128,0,0,0\n'
               '\t.text\n'
               '\t.stabs "y:1,32,32;;",128,0,0,0\n'
               '\t.stabs "nw_probe_0:G20",32,0,4,0\n')
        self.assertEqual(typedb_probe.stab_strings(asm),
                         ["a:T20=s8x:1,0,32;y:1,32,32;;", "nw_probe_0:G20"])

    def test_probe_source(self):
        text, names = typedb_probe.probe_source(DB)
        self.assertEqual(names, {"nw_probe_0": "FixPair", "nw_probe_1": "struct fix_s"})
        self.assertEqual(text, '#include "nucore/fix.h"\nFixPair nw_probe_0;\n'
                               'struct fix_s nw_probe_1;\n')


@unittest.skipIf(TypeTable is None, "no decomp submodule (declib)")
class Compare(unittest.TestCase):
    def check(self, db):
        table = TypeTable()
        refs = {}
        for s in STABS + ["nw_probe_0:G24", "nw_probe_1:G20"]:
            name, _desc, ref = table.add(s)
            refs[name] = ref
        checker = typedb_probe.Checker(db, table, "default")
        checker.record("FixPair", checker.struct(refs["nw_probe_0"]))
        checker.record("struct fix_s", checker.struct(refs["nw_probe_1"]))
        return checker

    def test_match(self):
        checker = self.check(DB)
        self.assertEqual(checker.mismatches, [])
        self.assertEqual(checker.fields, 8)

    def test_mismatches(self):
        db = copy.deepcopy(DB)
        rec = db["records"]
        rec["struct fix_s"]["fields"][2]["ps2"]["bit"] = 65
        rec["struct fix_s$1"]["ps2"]["size"] = 8
        rec["FixPair"]["fields"][1]["name"] = "z"
        self.assertEqual(self.check(db).mismatches, [
            "nucore/fix.h:12: FixPair.z: field: clang present, default missing",
            "nucore/fix.h:5: struct fix_s$1: size: clang 8, default 4",
            "nucore/fix.h:3: struct fix_s.tag: bit offset/size: clang (65, 3), default (64, 3)",
        ])


if __name__ == "__main__":
    unittest.main()
