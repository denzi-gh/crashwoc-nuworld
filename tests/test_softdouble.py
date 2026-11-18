"""softdouble's IR rewrite."""
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import softdouble as tool  # noqa: E402

MD = 'metadata !"round.tonearest", metadata !"fpexcept.maytrap"'


def fn(*body: str, name: str = "f") -> str:
    return "\n".join([f"define dso_local float @{name}(float %0, i64 %1) #0 {{", *body, "}"])


class Rewrite(unittest.TestCase):
    def test_double_arithmetic_and_conversions(self):
        ir = fn('  %3 = tail call double @llvm.experimental.constrained.fpext.f64.f32(float %0, '
                'metadata !"fpexcept.maytrap") #9',
                "  %4 = tail call double @llvm.experimental.constrained.fmul.f64(double %3, double 1.000000e-01, "
                f"{MD}) #9, !dbg !12",
                f"  %5 = tail call float @llvm.experimental.constrained.fptrunc.f32.f64(double %4, {MD}) #9",
                f"  %6 = tail call float @llvm.experimental.constrained.sitofp.f32.i64(i64 %1, {MD}) #9",
                f"  %7 = tail call float @llvm.experimental.constrained.fadd.f32(float %5, float %6, {MD}) #9",
                "  ret float %7")
        out = tool.rewrite(ir)
        self.assertIn("@llvm.experimental.constrained.fpext.f64.f32(", out)
        self.assertIn("%4 = tail call double @nw_sd_mul(double %3, double 1.000000e-01) #9, !dbg !12", out)
        self.assertIn("%5 = tail call float @nw_sd_to_f32(double %4) #9", out)
        self.assertIn("%6 = tail call float @nw_sd_s64_to_f32(i64 %1) #9", out)
        self.assertIn("@llvm.experimental.constrained.fadd.f32(float %5", out)
        self.assertIn("declare double @nw_sd_mul(double, double)", out)
        self.assertIn("declare float @nw_sd_to_f32(double)", out)
        self.assertEqual(out.count("declare"), 3)

    def test_exact_double_operations_stay(self):
        ir = fn("  %3 = tail call i1 @llvm.experimental.constrained.fcmps.f64(double 1.0, double 2.0, "
                'metadata !"olt", metadata !"fpexcept.maytrap") #9',
                f"  %4 = tail call double @llvm.experimental.constrained.sitofp.f64.i32(i32 7, {MD}) #9",
                "  %5 = tail call i32 @llvm.experimental.constrained.fptosi.i32.f64(double %4, "
                'metadata !"fpexcept.maytrap")',
                "  ret float %0")
        self.assertEqual(tool.rewrite(ir), ir)

    def test_unsupported_double_operations_fail(self):
        for line in (f"  %3 = tail call double @llvm.experimental.constrained.sqrt.f64(double 2.0, {MD})",
                     f"  %3 = tail call double @llvm.experimental.constrained.sitofp.f64.i64(i64 %1, {MD})",
                     f"  %3 = tail call <2 x double> @llvm.experimental.constrained.fadd.v2f64(<2 x double> %a, "
                     f"<2 x double> %b, {MD})",
                     "  %3 = fadd double 1.0, 2.0",
                     "  %3 = fptrunc double 1.0 to float",
                     f"  %3 = tail call float @llvm.experimental.constrained.pow.f32(float %0, float %0, {MD})"):
            with self.subTest(line=line), self.assertRaises(tool.Unsupported):
                tool.rewrite(fn(line, "  ret float %0"))

    def test_host_runtime_calls_go_through_wrappers(self):
        ir = "\n".join([fn("  %3 = tail call float @powf(float %0, float 2.0) #9",
                           "  %4 = tail call double @atof(ptr @s) #9",
                           "  store ptr @fmodf, ptr @table",
                           "  %5 = tail call float @powf2(float %0) #9",
                           "  ret float %3"),
                        "declare float @powf(float noundef, float noundef) local_unnamed_addr #1"])
        out = tool.rewrite(ir)
        self.assertIn("@nw_host_powf(float %0, float 2.0)", out)
        self.assertIn("@nw_host_atof(ptr @s)", out)
        self.assertIn("store ptr @nw_host_fmodf", out)
        self.assertIn("@powf2(float %0)", out)
        self.assertIn("declare float @nw_host_powf(", out)

    def test_wrapped_runtime_declarations_are_not_imported(self):
        ir = "\n".join([fn("  %3 = tail call float @powf(float %0, float 2.0) #9", "  ret float %3"),
                        "declare dllimport float @powf(float noundef, float noundef) local_unnamed_addr #1",
                        "declare dllimport i32 @puts(ptr noundef) local_unnamed_addr #1"])
        out = tool.rewrite(ir)
        self.assertIn("declare float @nw_host_powf(float noundef, float noundef)", out)
        self.assertIn("declare dllimport i32 @puts(ptr noundef)", out)

    def test_a_unit_defining_a_runtime_name_keeps_it(self):
        ir = fn("  ret float %0", name="atof") + "\n" + fn("  %3 = call double @atof(ptr null)", "  ret float %0")
        self.assertNotIn("nw_host_atof", tool.rewrite(ir))

    def test_split(self):
        self.assertEqual(tool.split(["-O3", "-MD", "-MT", "a.obj", "-MF", "a.d", "-o", "a.obj", "-c", "a.c"]),
                         ("a.obj", "a.c"))


if __name__ == "__main__":
    unittest.main()
