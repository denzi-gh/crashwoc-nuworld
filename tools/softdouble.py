#!/usr/bin/env python3
"""Compiler launcher: double arithmetic and C runtime float calls go through src/hle/eedouble.c."""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

CONSTRAINED = re.compile(r"@llvm\.experimental\.constrained\.(\w+)\.([\w.]+)\(")
HELPERS = {
    ("fadd", "f64"): ("nw_sd_add", 2),
    ("fsub", "f64"): ("nw_sd_sub", 2),
    ("fmul", "f64"): ("nw_sd_mul", 2),
    ("fdiv", "f64"): ("nw_sd_div", 2),
    ("fma", "f64"): ("nw_sd_fma", 3),
    ("fptrunc", "f32.f64"): ("nw_sd_to_f32", 1),
    ("sitofp", "f32.i64"): ("nw_sd_s64_to_f32", 1),
    ("uitofp", "f32.i64"): ("nw_sd_u64_to_f32", 1),
}
HELPER_DECLS = {
    "nw_sd_add": "double @nw_sd_add(double, double)",
    "nw_sd_sub": "double @nw_sd_sub(double, double)",
    "nw_sd_mul": "double @nw_sd_mul(double, double)",
    "nw_sd_div": "double @nw_sd_div(double, double)",
    "nw_sd_fma": "double @nw_sd_fma(double, double, double)",
    "nw_sd_to_f32": "float @nw_sd_to_f32(double)",
    "nw_sd_s64_to_f32": "float @nw_sd_s64_to_f32(i64)",
    "nw_sd_u64_to_f32": "float @nw_sd_u64_to_f32(i64)",
}
# Exact in any rounding.
EXACT = {"fpext", "fcmp", "fcmps", "fptosi", "fptoui", "floor", "ceil", "trunc", "round", "roundeven", "maxnum",
         "minnum", "maximum", "minimum", "lround", "llround"}
EXACT_INT = re.compile(r"f64\.i(8|16|32)$")
PLAIN = re.compile(r"=\s*(?:f(?:add|sub|mul|div|rem)\b[^=]*\bdouble\b|frem\b|fptrunc\s+(?:<[^>]*>\s*)?double\b|"
                   r"[su]itofp\s+(?:<[^>]*>\s*)?i64\b)")
# Lowered to C runtime calls after the rewrite.
LIBM = re.compile(r"@llvm\.(?:experimental\.constrained\.)?(?:sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|exp|"
                  r"exp2|exp10|log|log2|log10|pow|powi|frem)\.")
DEFINE = re.compile(r"^define\b[^@]*@\"?([\w.$]+)\"?\(")
# Called as nw_host_<name>.
HOST_FP = ("sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh", "exp", "exp2", "expm1",
           "log", "log2", "log10", "log1p", "pow", "fmod", "remainder", "cbrt", "hypot")
HOST = {*HOST_FP, *(f + "f" for f in HOST_FP), "atof", "strtod", "strtof"}
HOST_REF = re.compile(r"@(" + "|".join(sorted(HOST, key=len, reverse=True)) + r")(?![\w.$])")


class Unsupported(Exception):
    pass


def operands(text: str, start: int) -> tuple[list[str], int]:
    depth, args, cur = 0, [], start + 1
    for i in range(start, len(text)):
        c = text[i]
        if c in "([{<":
            depth += 1
        elif c in ")]}>":
            depth -= 1
            if depth == 0:
                args.append(text[cur:i].strip())
                return args, i + 1
        elif c == "," and depth == 1:
            args.append(text[cur:i].strip())
            cur = i + 1
    raise Unsupported("unterminated call")


def rewrite_line(line: str, fn: str, used: set[str]) -> str:
    out, pos = [], 0
    for m in CONSTRAINED.finditer(line):
        op, over = m.group(1), m.group(2)
        helper = HELPERS.get((op, over))
        if helper is None:
            if "f64" in over and op not in EXACT and not (op in ("sitofp", "uitofp") and EXACT_INT.search(over)):
                raise Unsupported(f"{fn}: no software double for {op}.{over}")
            continue
        name, n = helper
        args, end = operands(line, m.end() - 1)
        out.append(line[pos:m.start()])
        out.append(f"@{name}({', '.join(args[:n])})")
        pos = end
        used.add(name)
    out.append(line[pos:])
    return "".join(out)


def rewrite(ir: str) -> str:
    lines = ir.split("\n")
    defined = {m.group(1) for line in lines if (m := DEFINE.match(line))}
    used: set[str] = set()
    fn = None
    for i, line in enumerate(lines):
        if m := DEFINE.match(line):
            fn = m.group(1)
        elif line.startswith("}"):
            fn = None
        elif fn is not None:
            if PLAIN.search(line) or LIBM.search(line):
                raise Unsupported(f"{fn}: no software double for {line.strip()}")
            if "@llvm.experimental.constrained." in line:
                lines[i] = line = rewrite_line(line, fn, used)
        if "@" in line:
            host = HOST_REF.sub(lambda h: h.group(0) if h.group(1) in defined else "@nw_host_" + h.group(1), line)
            if host != line and line.startswith("declare dllimport "):
                host = "declare " + host[len("declare dllimport "):]
            lines[i] = host
    declared = {m.group(1) for line in lines if (m := re.match(r"declare\b[^@]*@([\w.$]+)\(", line))}
    lines += [f"declare {HELPER_DECLS[n]}" for n in sorted(used - declared)]
    return "\n".join(lines)


def split(args: list[str]) -> tuple[str, str]:
    obj = src = None
    for i, a in enumerate(args[:-1]):
        if a == "-o":
            obj = args[i + 1]
        elif a == "-c":
            src = args[i + 1]
    if obj is None or src is None:
        raise SystemExit(f"softdouble: expected -o <obj> -c <src> in {args}")
    return obj, src


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        raise SystemExit("usage: softdouble.py <clang> <args...> -o <obj> -c <src>")
    cc, args = argv[0], argv[1:]
    obj, src = split(args)
    ll = Path(obj + ".ll")
    first = []
    for i, a in enumerate(args):
        if a == "-c":
            first += ["-S", "-emit-llvm"]
        elif i > 0 and args[i - 1] == "-o":
            first.append(str(ll))
        else:
            first.append(a)
    r = subprocess.run([cc, *first])
    if r.returncode:
        return r.returncode
    try:
        ll.write_text(rewrite(ll.read_text(encoding="utf-8")), encoding="utf-8", newline="\n")
    except Unsupported as e:
        print(f"{src}: softdouble: {e}", file=sys.stderr)
        return 1
    opt = [a for a in args if re.fullmatch(r"-O[0-3sz]?", a)][-1:]
    codegen = [a for a in args if a.startswith(("-m", "--target", "-ffunction-sections", "-fdata-sections"))
               or a in ("-fPIC", "-fpic", "-fPIE", "-fpie", "-fno-pic", "-fno-pie")]
    r = subprocess.run([cc, *opt, *codegen, "-Wno-unused-command-line-argument", "-Xclang", "-disable-llvm-passes",
                        "-c", str(ll), "-o", obj])
    if r.returncode == 0:
        ll.unlink()
    return r.returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
