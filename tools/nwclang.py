"""libclang setup shared by the tools: the decomp as the PS2 compiles it
(`ps2`: mipsel, no NW_HOST) or as the host does (`host`: x86_64, NW_HOST,
stand-in libc headers, so no system headers are needed)."""
from __future__ import annotations

import tempfile
from pathlib import Path

try:
    from clang import cindex
except ImportError:
    cindex = None

# Just enough libc for the NW_HOST branches of the decomp headers.
SYSROOT = {
    "stddef.h": "typedef __SIZE_TYPE__ size_t;\ntypedef __PTRDIFF_TYPE__ ptrdiff_t;\n"
                "#define NULL ((void *)0)\n#define offsetof(t, m) __builtin_offsetof(t, m)\n",
    "stdint.h": "".join(f"typedef __{p}INT{n}_TYPE__ {p.lower()}int{n}_t;\n"
                        for p in ("", "U") for n in (8, 16, 32, 64))
                + "typedef __INTPTR_TYPE__ intptr_t;\ntypedef __UINTPTR_TYPE__ uintptr_t;\n",
    "stdarg.h": "typedef __builtin_va_list va_list;\n"
                "#define va_start(ap, last) __builtin_va_start(ap, last)\n"
                "#define va_arg(ap, t) __builtin_va_arg(ap, t)\n"
                "#define va_end(ap) __builtin_va_end(ap)\n",
    "stdio.h": "#include <stddef.h>\ntypedef struct nw_file FILE;\n",
    "stdlib.h": "#include <stddef.h>\n",
    "string.h": "#include <stddef.h>\n",
    "strings.h": "#include <stddef.h>\n",
    "math.h": "",
}
COMMON_FLAGS = ["-x", "c", "-std=gnu11", "-fgnu89-inline", "-fsigned-char", "-nostdinc",
                "-ferror-limit=0", "-w"]
VIEWS = {
    "ps2": ["--target=mipsel-linux-gnu"],
    "host": ["--target=x86_64-linux-gnu", "-DNW_HOST"],
}


class ClangError(Exception):
    pass


class Clang:
    """One libclang index with the flags of a view over decomp/include."""

    def __init__(self, decomp: Path, view: str):
        if cindex is None:
            raise ClangError("libclang is missing: python -m pip install -r tools/requirements.txt")
        self.view = view
        self.include = decomp / "include"
        self.args = COMMON_FLAGS + VIEWS[view]
        self._tmp = None
        if view == "host":
            self._tmp = tempfile.TemporaryDirectory(prefix="nw_clang_")
            for name, text in SYSROOT.items():
                (Path(self._tmp.name) / name).write_text(text)
            self.args += ["-isystem", self._tmp.name]
        self.args += ["-I", str(self.include)]
        self.index = cindex.Index.create()

    def close(self) -> None:
        if self._tmp is not None:
            self._tmp.cleanup()

    def parse(self, path: Path, text: str | None = None):
        """Translation unit of `path`, or of `text` stored under that name."""
        unsaved = [(str(path), text)] if text is not None else None
        tu = self.index.parse(str(path), args=self.args, unsaved_files=unsaved)
        fatal = [d for d in tu.diagnostics if d.severity >= cindex.Diagnostic.Fatal]
        if fatal:
            raise ClangError(f"{path}: {fatal[0]}")
        return tu

    @staticmethod
    def errors(tu) -> list[str]:
        return [str(d) for d in tu.diagnostics if d.severity >= cindex.Diagnostic.Error]
