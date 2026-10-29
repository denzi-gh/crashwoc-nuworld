"""Shared model for gen_stubs.py and gen_funcmap.py: one record per retail
function with its unit's treatment and who provides it on the host (decomp C,
port or stub). Reads only committed decomp files, never game data.

Unit kinds: wrapper (decomp source + <unit>.stubs.inc + <unit>.funcmap.inc),
standalone (generated <unit>.stubs.c), none (shim).
"""
from __future__ import annotations

import fnmatch
import hashlib
import re
import tomllib
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_DECOMP = REPO / "build" / "decomp"  # the patched tree (tools/patches.py)
DEFAULT_UNITS = REPO / "port" / "units.toml"
DEFAULT_PORT_SRC = REPO / "src"
VERSION = "pal103"

TREATMENTS = ("compile", "editor", "hle", "replace", "dead", "shim")
UNIT_KEYS = {"treatment", "override", "keep", "rename", "wrap", "runtime", "called"}
HOST_DEFINES = frozenset({"NW_HOST"})

STATE_PROVENANCE = {"matching": "matching", "equivalent": "equivalent", "asm": "near"}
PROVENANCES = ("matching", "equivalent", "near", "port", "stub")


def unit_name(mdebug_path: str) -> str:
    """objdiff unit name of an mdebug path (decomp tools/gen_objdiff.py)."""
    norm = mdebug_path.replace("\\", "/")
    stem = re.sub(r"\.[A-Za-z]+$", "", norm.rsplit("/", 1)[-1])
    if "nu2crash.ps2/" in norm:
        name = re.sub(r"\.[A-Za-z]+$", "", norm.split("nu2crash.ps2/", 1)[1])
    elif norm.startswith("./") or norm == "vu/vu.c" or norm.endswith(".i"):
        name = f"game/{stem}"
    elif "/newlib/" in norm:
        name = "sdk/newlib/" + re.sub(r"\.[A-Za-z]+$", "", norm.split("/newlib/", 1)[1])
    elif "/gcc/" in norm or stem in ("dp-bit", "fp-bit"):
        name = f"sdk/gcc/{stem}"
    else:
        name = f"sdk/sce/{stem}"
    return re.sub(r"[^A-Za-z0-9_.\-/]", "_", name)


@dataclass
class Function:
    unit: "Unit"
    name: str
    address: int
    state: str | None = None
    host: str = ""
    provider: str = ""              # decomp | port | stub | shim
    provenance: str = ""
    overridden: bool = False
    wrapped: bool = False           # the decomp C compiles as <name>__retail, the port's <name> takes the calls
    static: bool = False
    definition: "Decl | None" = None
    proto: "Decl | None" = None
    erased: bool = False            # proto types replaced by ABI equivalents
    origin: str = ""
    include: str | None = None


@dataclass
class Unit:
    index: int
    name: str
    mdebug: str
    treatment: str = ""
    override: tuple[str, ...] = ()
    keep: tuple[str, ...] = ()
    rename: dict[str, str] = field(default_factory=dict)
    wrap: tuple[str, ...] = ()
    runtime: tuple[str, ...] = ()   # editor unit: compiled in every build, the rest only with the editor
    editor_dropped: bool = False    # the functions outside runtime are dropped (no editor build)
    called: tuple[str, ...] = ()    # editor/dead unit: stubs compiled code may call (guarded or unreachable)
    functions: list[Function] = field(default_factory=list)
    source: str | None = None
    kind: str = ""                  # wrapper | standalone | none

    @property
    def symbol(self) -> str:
        return "nw_funcmap_unit_" + re.sub(r"[^A-Za-z0-9_]", "_", self.name)

    def is_overridden(self, name: str) -> bool:
        if self.editor_dropped and not any(fnmatch.fnmatchcase(name, p) for p in self.runtime):
            return True
        hit = any(fnmatch.fnmatchcase(name, p) for p in self.override)
        return hit and not any(fnmatch.fnmatchcase(name, p) for p in self.keep)

    def is_wrapped(self, name: str) -> bool:
        return name in self.wrap


def load_registries(decomp: Path, version: str) -> list[Unit]:
    config = decomp / "config" / version
    raw_units = tomllib.loads((config / "units.toml").read_text())["unit"]
    raw_funcs = tomllib.loads((config / "functions.toml").read_text())["function"]
    by_unit = defaultdict(list)
    for f in raw_funcs:
        by_unit[int(f["unit"])].append((int(f["address"]), f["name"]))
    units = []
    for u in raw_units:
        index = int(u["index"])
        if index not in by_unit:
            continue
        unit = Unit(index=index, name=unit_name(u["name"]), mdebug=u["name"])
        for address, name in sorted(by_unit[index]):
            unit.functions.append(Function(unit=unit, name=name, address=address))
        units.append(unit)
    counts = Counter(u.name for u in units)
    for u in units:
        if counts[u.name] > 1:
            u.name = f"{u.name}_{u.index:03d}"
    units.sort(key=lambda u: u.functions[0].address)
    return units


def load_states(decomp: Path, version: str, units: list[Unit], errors: list[str]) -> None:
    by_index = {u.index: u for u in units}
    for path in sorted((decomp / "config" / version / "status").rglob("*.toml")):
        data = tomllib.loads(path.read_text())
        index = int(data["unit"].rsplit("-", 1)[1])
        unit = by_index.get(index)
        if unit is None:
            errors.append(f"{path.name}: manifest for unit {index}, which owns no .text code")
            continue
        if data.get("source") != f"src/{unit.name}.c":
            errors.append(f"unit {index}: manifest source {data.get('source')!r} "
                          f"does not match the unit name {unit.name!r}")
        states = {}
        for entry in data.get("function", []):
            _version, _unit, vram, _name = entry["id"].split(":", 3)
            states[int(vram, 16)] = entry["state"]
        for f in unit.functions:
            f.state = states.get(f.address)


_TOKENS = re.compile(r"(/\*.*?\*/)|(//[^\n]*)|(\"(?:\\.|[^\"\\\n])*\")|('(?:\\.|[^'\\\n])*')", re.S)
_DIRECTIVE = re.compile(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", re.M)
_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.M)
# As in decomp tools/check_prototypes.py.
_SIGNATURE = re.compile(
    r"^[ \t]*((?:(?:extern|static|inline|__inline__|__inline)\s+)*)"
    r"([A-Za-z_][\w\s\*]*?[\s\*])"
    r"([A-Za-z_]\w*)\s*"
    r"\(([^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)\s*([;{])",
    re.M)
_CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
_C_TOKEN = re.compile(r"[A-Za-z_]\w*|\.?\d[\w.]*|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'|\S")
_NOT_A_TYPE = frozenset(("if", "while", "for", "switch", "return", "sizeof", "do",
                         "else", "case", "goto", "typedef"))
QUALIFIERS = frozenset(("const", "volatile", "register", "restrict", "__restrict"))
C_TYPE_WORDS = frozenset(("void", "char", "short", "int", "long", "float", "double",
                          "signed", "unsigned", "_Bool"))
KNOWN_TYPEDEFS = frozenset(("u8", "s8", "u16", "s16", "u32", "s32", "u64", "s64", "u128",
                            "f32", "f64", "nuintptr", "nusintptr", "size_t", "ptrdiff_t",
                            "intptr_t", "uintptr_t", "int8_t", "uint8_t", "int16_t",
                            "uint16_t", "int32_t", "uint32_t", "int64_t", "uint64_t"))


def _blank(text: str) -> str:
    return re.sub(r"[^\n]", " ", text)


def _inactive_lines(text: str) -> set[int]:
    """Lines outside the host's #if branches; unknown conditions count as true."""
    inactive, stack = set(), []          # [active, branch_taken]
    for i, line in enumerate(text.split("\n")):
        m = re.match(r"[ \t]*#[ \t]*(ifdef|ifndef|if|elif|else|endif)\b(.*)", line)
        parent = all(a for a, _ in stack)
        if m:
            kind, arg = m.group(1), m.group(2).strip()
            if kind in ("ifdef", "ifndef", "if"):
                if kind == "ifdef":
                    cond = arg.split()[0] in HOST_DEFINES if arg else True
                elif kind == "ifndef":
                    cond = arg.split()[0] not in HOST_DEFINES if arg else True
                else:
                    cond = arg != "0"
                stack.append([cond, cond])
            elif kind == "elif" and stack:
                stack[-1] = [not stack[-1][1], True]
            elif kind == "else" and stack:
                stack[-1] = [not stack[-1][1], True]
            elif kind == "endif" and stack:
                stack.pop()
            inactive.add(i)
            continue
        if not parent:
            inactive.add(i)
    return inactive


@dataclass(frozen=True)
class Decl:
    path: str
    line: int
    name: str
    ret: str
    params: str         # "" = unprototyped
    static: bool
    is_def: bool
    body: str = ""      # sha256 of a definition's body tokens (comments and spacing ignored)
    calls: frozenset[str] = frozenset()

    @property
    def where(self) -> str:
        return f"{self.path}:{self.line}"

    @property
    def prototyped(self) -> bool:
        return self.params.strip() != ""


@dataclass
class CFile:
    path: str
    decls: list[Decl]
    includes: list[str]
    typedefs: dict[str, str]


def scan_c(text: str, path: str) -> CFile:
    no_comments = _TOKENS.sub(
        lambda m: _blank(m.group(0)) if m.group(1) or m.group(2) else m.group(0), text)
    lines = no_comments.split("\n")
    for i in _inactive_lines(no_comments):
        lines[i] = " " * len(lines[i])
    active = "\n".join(lines)
    includes = _INCLUDE.findall(active)
    code = _TOKENS.sub(lambda m: m.group(0)[0] + " " * (len(m.group(0)) - 2) + m.group(0)[-1]
                       if m.group(3) or m.group(4) else m.group(0), active)
    code = _DIRECTIVE.sub(lambda m: _blank(m.group(0)), code)
    with_strings = None

    decls = []
    for m in _SIGNATURE.finditer(code):
        storage, ret, name, params, end = m.groups()
        ret_words = ret.split()
        if name in _NOT_A_TYPE or (ret_words and ret_words[0] in _NOT_A_TYPE):
            continue
        line = code.count("\n", 0, m.start(3)) + 1
        body, calls = "", frozenset()
        if end == "{":
            if with_strings is None:
                with_strings = _DIRECTIVE.sub(lambda d: _blank(d.group(0)), active)
            close = _closing_brace(code, m.end() - 1)
            tokens = _C_TOKEN.findall(with_strings, m.end(), close)
            body = hashlib.sha256("\0".join(tokens).encode("utf-8", "surrogateescape")).hexdigest()
            calls = frozenset(_CALL.findall(code, m.end(), close))
        decls.append(Decl(path=path, line=line, name=name, ret=" ".join(ret_words),
                          params=" ".join(params.split()), static="static" in storage.split(),
                          is_def=end == "{", body=body, calls=calls))
    return CFile(path=path, decls=decls, includes=includes, typedefs=_typedefs(code))


def _closing_brace(code: str, start: int) -> int:
    depth = 0
    for i in range(start, len(code)):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return len(code)


def _split_top(text: str) -> list[str]:
    parts, depth, cur = [], 0, []
    for ch in text:
        depth += {"(": 1, "[": 1, "{": 1, ")": -1, "]": -1, "}": -1}.get(ch, 0)
        if ch == "," and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append("".join(cur))
    return [p.strip() for p in parts if p.strip()]


def _typedefs(code: str) -> dict[str, str]:
    out = {}
    for m in re.finditer(r"\btypedef\b", code):
        depth, i = 0, m.end()
        while i < len(code):
            ch = code[i]
            depth += {"{": 1, "}": -1, "(": 1, ")": -1}.get(ch, 0)
            if ch == ";" and depth == 0:
                break
            i += 1
        decl = " ".join(code[m.end():i].split())
        fnptr = re.search(r"\(\s*\*\s*(\w+)\s*\)\s*\(", decl)
        if fnptr:
            out.setdefault(fnptr.group(1), "void *")
            continue
        body = re.match(r"(struct|union|enum)\s*(\w*)\s*\{.*\}(.*)$", decl)
        if body:
            base = f"{body.group(1)} {body.group(2)}".strip()
            declarators = _split_top(body.group(3))
        else:
            parts = _split_top(decl)
            first = re.match(r"(.*?)[\s\*]*\b[A-Za-z_]\w*\s*(?:\[[^\]]*\]\s*)*$", parts[0]) if parts else None
            if not first or not first.group(1).strip():
                continue
            base = first.group(1).strip()
            declarators = [parts[0][len(first.group(1)):], *parts[1:]]
        for part in declarators:
            m = re.match(r"\s*([\s\*]*)([A-Za-z_]\w*)\s*((?:\[[^\]]*\]\s*)*)$", part)
            if m:
                pointer = "*" in m.group(1) or m.group(3)
                out.setdefault(m.group(2), base + (" *" if pointer else ""))
    return out


def param_name(param: str) -> str | None:
    fnptr = re.search(r"\(\s*\*\s*([A-Za-z_]\w*)?\s*\)\s*\(", param)
    if fnptr:
        return fnptr.group(1)
    tokens = re.findall(r"[A-Za-z_]\w*|\*", param.split("[", 1)[0])
    words = [t for t in tokens if t not in QUALIFIERS]
    if not words or words[-1] == "*":
        return None
    idents = [t for t in words if t != "*"]
    if len(idents) < 2 or idents[-1] in C_TYPE_WORDS:
        return None
    if len(idents) == 2 and idents[0] in ("struct", "union", "enum"):
        return None
    return idents[-1]


def _named(param: str, name: str) -> str:
    fnptr = re.search(r"\(\s*\*\s*\)", param)
    if fnptr:
        return param[:fnptr.start()] + f"(*{name})" + param[fnptr.end():]
    if "[" in param:
        head, tail = param.split("[", 1)
        return f"{head.rstrip()} {name}[{tail}"
    return param + ("" if param.endswith("*") else " ") + name


def definition_params(params: str) -> str:
    plist = _split_top(params)
    if not plist or plist == ["void"]:
        return "void"
    out = []
    for i, p in enumerate(plist):
        if p == "..." or param_name(p):
            out.append(p)
        else:
            out.append(_named(p, f"nw_p{i}"))
    return ", ".join(out)


def head(ret: str, name: str, params: str) -> str:
    return f"{ret}{'' if ret.endswith('*') else ' '}{name}({params})"


def erase_type(text: str, typedefs: dict[str, str], depth: int = 0) -> str | None:
    """ABI-equivalent type needing no header; None for a struct by value."""
    text = " ".join(text.split())
    if "*" in text or "[" in text or "(" in text:
        return "void *"
    words = [w for w in re.findall(r"[A-Za-z_]\w*", text) if w not in QUALIFIERS]
    if not words:
        return None
    if words[0] in ("struct", "union"):
        return None
    if words[0] == "enum":
        return "int"
    if all(w in C_TYPE_WORDS for w in words):
        return " ".join(words)
    if len(words) == 1:
        if words[0] in KNOWN_TYPEDEFS:
            return words[0]
        target = typedefs.get(words[0])
        if target is not None and depth < 8:
            return erase_type(target, typedefs, depth + 1)
    return None


def erase(decl: Decl, typedefs: dict[str, str]) -> Decl | None:
    ret = erase_type(decl.ret, typedefs)
    if ret is None:
        return None
    plist = _split_top(decl.params)
    if not decl.prototyped or plist == ["void"]:
        params = "void"
    else:
        out = []
        for p in plist:
            if p == "...":
                out.append(p)
                continue
            name = param_name(p)
            ptype = p
            if name:
                ptype = re.sub(r"\b" + re.escape(name) + r"\b(?!.*\b" + re.escape(name) + r"\b)",
                               "", p, count=1)
            erased = erase_type(ptype, typedefs)
            if erased is None:
                return None
            out.append(erased)
        params = ", ".join(out)
    return Decl(path=decl.path, line=decl.line, name=decl.name, ret=ret, params=params,
                static=False, is_def=False)


@dataclass
class Model:
    decomp: Path
    units: list[Unit]
    editor: bool
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    def functions(self):
        for u in self.units:
            yield from u.functions


def load_units_toml(path: Path, units: list[Unit], errors: list[str]) -> None:
    """Exact key wins, else the longest matching glob."""
    data = tomllib.loads(path.read_text())
    if data.get("schema") != 1:
        errors.append(f"{path.name}: schema must be 1")
        return
    entries = data.get("units", {})
    names = {u.name for u in units}
    for key, entry in entries.items():
        if not isinstance(entry, dict):
            errors.append(f"units.{key}: must be a table")
            continue
        unknown = set(entry) - UNIT_KEYS
        if unknown:
            errors.append(f"units.{key}: unknown keys {sorted(unknown)}")
        if entry.get("treatment") not in TREATMENTS:
            errors.append(f"units.{key}: treatment must be one of {', '.join(TREATMENTS)}")
        if any(c in key for c in "*?[") and set(entry) - {"treatment"}:
            errors.append(f"units.{key}: a glob key takes only a treatment")
        if not any(fnmatch.fnmatchcase(n, key) for n in names):
            errors.append(f"units.{key}: matches no decomp unit")

    for unit in units:
        if unit.name in entries:
            entry = entries[unit.name]
        else:
            globs = [k for k in entries if fnmatch.fnmatchcase(unit.name, k) and k != unit.name]
            if not globs:
                errors.append(f"unit {unit.name}: no entry in {path.name}")
                continue
            globs.sort(key=len, reverse=True)
            if len(globs) > 1 and len(globs[0]) == len(globs[1]):
                errors.append(f"unit {unit.name}: ambiguous globs {globs[:2]}")
            entry = entries[globs[0]]
        unit.treatment = entry.get("treatment", "")
        unit.override = tuple(entry.get("override", ()))
        unit.keep = tuple(entry.get("keep", ()))
        unit.rename = dict(entry.get("rename", {}))
        unit.wrap = tuple(entry.get("wrap", ()))
        unit.runtime = tuple(entry.get("runtime", ()))
        if unit.runtime and unit.treatment != "editor":
            errors.append(f"unit {unit.name}: runtime on a {unit.treatment} unit (editor only)")
        unit.called = tuple(entry.get("called", ()))
        if unit.called and unit.treatment not in ("editor", "dead"):
            errors.append(f"unit {unit.name}: called on a {unit.treatment} unit (editor and dead only)")
        if unit.keep and not unit.override:
            errors.append(f"unit {unit.name}: keep without override")
        fnames = [f.name for f in unit.functions]
        for kind, patterns in (("override", unit.override), ("keep", unit.keep), ("runtime", unit.runtime),
                               ("called", unit.called)):
            for p in patterns:
                if not any(fnmatch.fnmatchcase(n, p) for n in fnames):
                    errors.append(f"unit {unit.name}: {kind} {p!r} matches no function")
        for old in unit.rename:
            if old not in fnames:
                errors.append(f"unit {unit.name}: rename of unknown function {old!r}")
        for name in unit.wrap:
            if name not in fnames:
                errors.append(f"unit {unit.name}: wrap of unknown function {name!r}")


def load(decomp: Path = DEFAULT_DECOMP, units_toml: Path = DEFAULT_UNITS,
         port_src: Path | None = DEFAULT_PORT_SRC, editor: bool = False,
         version: str = VERSION) -> Model:
    units = load_registries(decomp, version)
    model = Model(decomp=decomp, units=units, editor=editor)
    load_states(decomp, version, units, model.errors)
    load_units_toml(units_toml, units, model.errors)
    if model.errors:
        return model

    files: dict[str, CFile] = {}
    for root in ("src", "include"):
        for p in sorted((decomp / root).rglob("*.[ch]")):
            rel = p.relative_to(decomp).as_posix()
            files[rel] = scan_c(p.read_text(errors="replace"), rel)
    typedefs: dict[str, str] = {}
    for f in files.values():
        for k, v in f.typedefs.items():
            typedefs.setdefault(k, v)
    decls_by_name: dict[str, list[Decl]] = defaultdict(list)
    for f in files.values():
        for d in f.decls:
            decls_by_name[d.name].append(d)

    port_defs: dict[str, Decl] = {}
    if port_src is not None and port_src.is_dir():
        for p in sorted(port_src.rglob("*.c")):
            rel = p.relative_to(port_src.parent).as_posix()
            for d in scan_c(p.read_text(errors="replace"), rel).decls:
                if d.is_def and not d.static:
                    port_defs.setdefault(d.name, d)

    def visible(start: str) -> list[CFile]:
        seen, order = set(), []

        def walk(path: str) -> None:
            if path in seen or path not in files:
                return
            seen.add(path)
            order.append(files[path])
            here = path.rsplit("/", 1)[0]
            for inc in files[path].includes:
                for cand in (f"{here}/{inc}", f"include/{inc}"):
                    if cand in files:
                        walk(cand)
                        break
        walk(start)
        return order

    for unit in units:
        if unit.treatment == "shim":
            unit.kind = "none"
        elif unit.treatment == "compile" or (unit.treatment == "editor" and (editor or unit.runtime)):
            unit.kind = "wrapper"
            unit.editor_dropped = unit.treatment == "editor" and not editor
        else:
            unit.kind = "standalone"
        src = f"src/{unit.name}.c"
        unit.source = src if src in files else None
        if unit.kind == "wrapper" and unit.source is None:
            model.errors.append(f"unit {unit.name}: treatment {unit.treatment} but no {src}")
            continue
        tu = visible(unit.source) if unit.kind == "wrapper" else []
        tu_decls: dict[str, list[Decl]] = defaultdict(list)
        for cf in tu:
            for d in cf.decls:
                tu_decls[d.name].append(d)
        module_header = f"include/{unit.name}.h"

        for f in unit.functions:
            f.host = unit.rename.get(f.name, f.name)
            f.overridden = unit.is_overridden(f.name)
            if unit.kind == "none":
                f.provider = "shim"
                continue
            defs = [d for d in tu_decls.get(f.name, ()) if d.is_def]
            f.definition = defs[0] if defs else None
            f.wrapped = unit.is_wrapped(f.name)
            if f.wrapped:
                why = ("it is also overridden" if f.overridden
                       else "the decomp has no C for it" if f.definition is None
                       else "it is static in the decomp" if f.definition.static
                       else "it is also renamed" if f.host != f.name
                       else "the port does not define it" if f.host not in port_defs
                       else f"{unit.name} is no wrapper unit" if unit.kind != "wrapper" else None)
                if why:
                    model.errors.append(f"unit {unit.name}: wrap {f.name}: {why}")
                f.provider, f.provenance = "port", "port"
                _choose_proto(f, unit, tu_decls, decls_by_name, typedefs, module_header)
                continue
            if f.definition and not f.overridden:
                f.provider = "decomp"
                f.provenance = STATE_PROVENANCE.get(f.state or "asm", "near")
                f.static = f.definition.static
                continue
            if f.host in port_defs:
                f.provider, f.provenance = "port", "port"
                if f.definition is not None and port_defs[f.host].body == f.definition.body:
                    model.warnings.append(
                        f"{port_defs[f.host].where}: the port's {f.host} is identical to the decomp C "
                        f"({unit.name}); drop the override")
                if unit.kind == "wrapper" and not f.overridden:
                    model.warnings.append(
                        f"{port_defs[f.host].where}: the port defines {f.host} ({unit.name}, "
                        f"no C in the decomp yet) without an override entry")
            else:
                f.provider, f.provenance = "stub", "stub"
            _choose_proto(f, unit, tu_decls, decls_by_name, typedefs, module_header)
            if f.provider == "port" and f.static:
                model.warnings.append(f"{unit.name}: the port defines {f.name}, static in the "
                                      f"decomp; that needs an upstream NW_STATIC seam")

    _check_globals(model, units, port_defs)
    _check_calls(model, units, files)
    return model


def _choose_proto(f: Function, unit: Unit, tu_decls, decls_by_name, typedefs,
                  module_header: str) -> None:
    if f.definition is not None:
        f.proto, f.origin = f.definition, f.definition.where
        if f.definition.static:
            f.static = True
        return
    in_tu = tu_decls.get(f.name, [])
    if in_tu:
        in_tu = sorted(in_tu, key=lambda d: not d.prototyped)
        f.proto, f.origin = in_tu[0], in_tu[0].where
        f.static = any(d.static for d in in_tu) and f.name == f.host
        return
    others = [d for d in decls_by_name.get(f.name, ()) if not d.static]
    headers = sorted((d for d in others if d.path.startswith("include/")),
                     key=lambda d: (d.path != module_header, d.path, d.line))
    if headers and unit.kind == "standalone":
        f.proto, f.origin, f.include = headers[0], headers[0].where, headers[0].path[len("include/"):]
        return
    sources = [d for d in others if not d.path.startswith("include/")]
    erased = [(e, d) for d in headers + sources if (e := erase(d, typedefs)) is not None]
    if erased:
        best = Counter(head(e.ret, "", e.params) for e, _d in erased).most_common(1)[0][0]
        e, d = next((e, d) for e, d in erased if head(e.ret, "", e.params) == best)
        f.proto, f.erased, f.origin = e, True, d.where
        return
    if others:
        f.origin = "unresolved: " + others[0].where
    else:
        f.origin = "no declaration"
        f.static = f.provider == "stub" and f.name == f.host
    f.proto = Decl(path="", line=0, name=f.name, ret="void", params="void",
                   static=f.static, is_def=False)


def _check_calls(model: Model, units: list[Unit], files: dict[str, CFile]) -> None:
    """Compiled decomp code calls a stub of an editor or dead unit only where that unit lists it under called.
    Walks each wrapper's kept definitions and the file's helpers outside the registry they call."""
    stubs = {f.name: f for u in units if u.treatment in ("editor", "dead")
             for f in u.functions if f.provider == "stub"}
    used = set()
    for unit in units:
        if unit.kind != "wrapper":
            continue
        defs = {d.name: d for d in files[unit.source].decls if d.is_def}
        registry = {f.name: f for f in unit.functions}
        todo = [f.name for f in unit.functions if f.name in defs and not f.overridden]
        seen = set()
        while todo:
            name = todo.pop()
            if name in seen:
                continue
            seen.add(name)
            for c in sorted(defs[name].calls):
                if c in defs and c not in registry:
                    todo.append(c)
                    continue
                f = stubs.get(c)
                if f is None:
                    continue
                if any(fnmatch.fnmatchcase(c, p) for p in f.unit.called):
                    used.add(c)
                    continue
                model.errors.append(
                    f"{defs[name].where}: {name} calls {c}, a stub of the {f.unit.treatment} unit {f.unit.name}; "
                    "compile it (runtime) or list it under called")
    for u in units:
        for p in u.called:
            if any(fnmatch.fnmatchcase(n, p) for n in stubs) and not any(fnmatch.fnmatchcase(n, p) for n in used):
                model.warnings.append(f"unit {u.name}: called {p!r}: no compiled code calls it")


def _check_globals(model: Model, units: list[Unit], port_defs: dict[str, Decl]) -> None:
    owners: dict[str, list[str]] = defaultdict(list)
    for u in units:
        for f in u.functions:
            if f.provider in ("decomp", "stub") and not f.static:
                owners[f.host].append(u.name)
            if f.provider == "decomp" and f.host in port_defs and not f.static:
                model.errors.append(
                    f"{port_defs[f.host].where}: the port defines {f.host}, which {u.name} "
                    f"compiles from the decomp; add it to that unit's override list")
    for host, where in sorted(owners.items()):
        if len(where) > 1:
            model.errors.append(f"global {host} is defined by several units: {', '.join(where)}; "
                                "rename it in all but one")


def write_if_changed(path: Path, text: str) -> bool:
    if path.is_file() and path.read_text() == text:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, newline="\n")
    return True


def print_outputs(out: Path, paths) -> None:
    """--list-outputs: the files a generator writes, relative to --out (read by CMake)."""
    for p in paths:
        print(p.relative_to(out).as_posix())


def decomp_tree(path: Path | None) -> Path:
    """The decomp tree a tool reads: `path`, else the patched tree, first refreshed from the submodule."""
    if path is not None:
        return path
    import patches
    try:
        patches.apply()
    except patches.PatchError as e:
        raise SystemExit(f"error: {e}") from None
    return DEFAULT_DECOMP


def add_common_args(parser) -> None:
    parser.add_argument("--decomp", type=Path, default=None,
                        help="decomp tree (default: the patched tree build/decomp, refreshed first)")
    parser.add_argument("--units", type=Path, default=DEFAULT_UNITS,
                        help="unit treatments (default: port/units.toml)")
    parser.add_argument("--port-src", type=Path, default=DEFAULT_PORT_SRC,
                        help="the port's sources, scanned for definitions (default: src/)")
    parser.add_argument("--editor", action="store_true",
                        help="build the editor units (default: they are stubs)")
    parser.add_argument("--list-outputs", action="store_true",
                        help="print the files that would be written, relative to --out")


def report(model: Model, out) -> bool:
    for w in model.warnings:
        print(f"warning: {w}", file=out)
    for e in model.errors:
        print(f"error: {e}", file=out)
    return not model.errors
