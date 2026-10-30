#!/usr/bin/env python3
"""Port patches over the decomp: the patched tree every host tool reads.

The submodule stays retail. apply copies it (LF), adds port/patches/files/, applies each concern's unified diffs in
series order (a hunk may move but is never fuzzed), then wraps the port/values rules' retail values in a macro.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PRISTINE = REPO / "decomp"
TREE = REPO / "build" / "decomp"
PATCHES = REPO / "port" / "patches"
VALUES = REPO / "port" / "values"
WORK = REPO / "build" / "patch-work"
STAMP = ".nw-tree.json"
SCHEMA = 1
HUNK = re.compile(rb"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@(.*)$")


class PatchError(Exception):
    pass


@dataclass
class Hunk:
    old_start: int
    new_start: int
    context: str
    old: list[bytes] = field(default_factory=list)
    new: list[bytes] = field(default_factory=list)


@dataclass
class Patch:
    origin: Path
    path: str
    hunks: list[Hunk]


@dataclass
class Concern:
    name: str
    reason: str
    patches: list[Patch]


def normalize(data: bytes) -> bytes:
    """LF line endings for text; binary files (a NUL byte) stay as they are."""
    return data if b"\0" in data else data.replace(b"\r\n", b"\n")


def git(decomp: Path, *args: str) -> bytes:
    return subprocess.run(["git", "-C", str(decomp), *args], capture_output=True, check=True).stdout


def decomp_sha(decomp: Path) -> str | None:
    if not (decomp / ".git").exists():
        return None
    try:
        return git(decomp, "rev-parse", "HEAD").decode().strip() or None
    except (OSError, subprocess.CalledProcessError):
        return None


def worktree_files(decomp: Path) -> dict[str, bytes]:
    """The checkout's files, committed or not (ignored ones excepted), local edits included."""
    if (decomp / ".git").exists():
        try:
            names = git(decomp, "ls-files", "-z", "-co", "--exclude-standard").decode().split("\0")
        except (OSError, subprocess.CalledProcessError) as e:
            detail = getattr(e, "stderr", b"").decode(errors="replace").strip() or e
            raise PatchError(f"git ls-files in {decomp} failed: {detail}") from None
    else:
        names = [p.relative_to(decomp).as_posix() for p in decomp.rglob("*")
                 if p.is_file() and ".git" not in p.relative_to(decomp).parts]
    out = {}
    for name in names:
        p = decomp / name
        if name and p.is_file():
            out[name] = normalize(p.read_bytes())
    return out


def commit_files(decomp: Path, rev: str) -> dict[str, bytes]:
    """The files of a decomp commit, from the object store (no checkout)."""
    listing = git(decomp, "ls-tree", "-r", "-z", rev).split(b"\0")
    blobs = []
    for entry in listing:
        if not entry:
            continue
        meta, name = entry.split(b"\t", 1)
        mode, kind, sha = meta.split()
        if kind == b"blob":
            blobs.append((name.decode(), sha))
    proc = subprocess.run(["git", "-C", str(decomp), "cat-file", "--batch"], capture_output=True, check=True,
                          input=b"".join(sha + b"\n" for _, sha in blobs))
    out, data, pos = {}, proc.stdout, 0
    for name, sha in blobs:
        header_end = data.index(b"\n", pos)
        size = int(data[pos:header_end].split()[2])
        start = header_end + 1
        out[name] = normalize(data[start:start + size])
        pos = start + size + 1
    return out


def parse_patch(origin: Path, path: str) -> Patch:
    data = origin.read_bytes()
    if b"\r" in data:
        raise PatchError(f"{origin}: CR line endings (patches are LF, see .gitattributes)")
    lines = data.split(b"\n")
    if len(lines) < 2 or not lines[0].startswith(b"--- ") or not lines[1].startswith(b"+++ "):
        raise PatchError(f"{origin}: not a unified diff of one file")
    hunks: list[Hunk] = []
    i = 2
    while i < len(lines):
        line = lines[i]
        if not line:
            i += 1
            continue
        m = HUNK.match(line)
        if not m:
            raise PatchError(f"{origin}:{i + 1}: expected a hunk header")
        old_len = int(m.group(2) or b"1")
        new_len = int(m.group(4) or b"1")
        h = Hunk(int(m.group(1)), int(m.group(3)), m.group(5).decode(errors="replace").strip())
        i += 1
        last = None
        while len(h.old) < old_len or len(h.new) < new_len:
            if i >= len(lines):
                raise PatchError(f"{origin}: hunk at old line {h.old_start} is truncated")
            body = lines[i] or b" "  # a blank context line whose space an editor dropped
            tag, text = body[:1], body[1:] + b"\n"
            if tag == b" ":
                h.old.append(text)
                h.new.append(text)
                last = (h.old, h.new)
            elif tag == b"-":
                h.old.append(text)
                last = (h.old,)
            elif tag == b"+":
                h.new.append(text)
                last = (h.new,)
            else:
                raise PatchError(f"{origin}:{i + 1}: bad hunk line")
            i += 1
            if i < len(lines) and lines[i].startswith(b"\\"):
                for side in last:
                    side[-1] = side[-1][:-1]
                i += 1
        if len(h.old) != old_len or len(h.new) != new_len:
            raise PatchError(f"{origin}: hunk at old line {h.old_start} has -{len(h.old)} +{len(h.new)} lines, "
                             f"its header says -{old_len} +{new_len}")
        hunks.append(h)
    return Patch(origin, path, hunks)


def apply_patch(patch: Patch, data: bytes) -> bytes:
    """Every hunk where the patch puts it, or at its one other exact match; nothing fuzzy."""
    old = data.splitlines(keepends=True)
    out: list[bytes] = []
    pos = offset = 0
    for n, h in enumerate(patch.hunks, 1):
        size = len(h.old)
        want = h.old_start - 1 + offset if size else h.old_start + offset
        if pos <= want and old[want:want + size] == h.old:
            at = want
        else:
            found = [p for p in range(pos, len(old) - size + 1) if old[p:p + size] == h.old]
            if len(found) != 1:
                where = f"hunk {n} (old line {h.old_start}{', ' + h.context if h.context else ''})"
                why = "matches nowhere" if not found else f"matches {len(found)} places"
                raise PatchError(f"{patch.origin}: {where} {why} in {patch.path}")
            at = found[0]
        out.extend(old[pos:at])
        out.extend(h.new)
        pos = at + size
        offset = at - (h.old_start - 1 if size else h.old_start)
    out.extend(old[pos:])
    return b"".join(out)


def load_series(patches: Path | None = PATCHES) -> tuple[list[Concern], dict[str, bytes]]:
    """The concerns in order and the new files (port/patches/files/); None: no series."""
    if patches is None:
        return [], {}
    files = {}
    for p in sorted((patches / "files").rglob("*")):
        if p.is_file():
            files[p.relative_to(patches / "files").as_posix()] = normalize(p.read_bytes())
    series = patches / "series.toml"
    if not series.is_file():
        return [], files
    doc = tomllib.loads(series.read_text(encoding="utf-8"))
    if doc.get("schema") != SCHEMA:
        raise PatchError(f"{series}: schema {doc.get('schema')}, expected {SCHEMA}")
    concerns, seen = [], set()
    for entry in doc.get("concern", []):
        name = entry["name"]
        if name in seen or name == "files" or not (patches / name).is_dir():
            raise PatchError(f"{series}: concern {name!r} is repeated, reserved or has no directory")
        seen.add(name)
        items = []
        for p in sorted((patches / name).rglob("*.patch")):
            items.append(parse_patch(p, p.relative_to(patches / name).as_posix()[:-len(".patch")]))
        concerns.append(Concern(name, entry.get("reason", ""), items))
    stray = {p.name for p in patches.iterdir() if p.is_dir()} - seen - {"files"}
    if stray:
        raise PatchError(f"{series}: directories not in the series: {', '.join(sorted(stray))}")
    return concerns, files


def patch_tree(files: dict[str, bytes], patches: Path | None = PATCHES,
               values: Path | None = VALUES) -> dict[str, bytes]:
    """The decomp files with the series and the value rules applied (None: without them)."""
    concerns, new = load_series(patches)
    tree = dict(files)
    for name, data in new.items():
        if name in tree:
            raise PatchError(f"{patches / 'files' / name}: the decomp has this file; patch it instead")
        tree[name] = data
    for c in concerns:
        for p in c.patches:
            if p.path not in tree:
                raise PatchError(f"{p.origin}: {p.path} is not in the decomp")
            tree[p.path] = apply_patch(p, tree[p.path])
    for rules in load_values(values):
        for path in rules.sites:
            if path not in tree:
                raise PatchError(f"{rules.origin}: {path} is not in the decomp")
            tree[path] = apply_values(rules, path, tree[path])
    return tree


def series_hash(patches: Path | None = PATCHES, values: Path | None = VALUES) -> str:
    h = hashlib.sha256()
    files = []
    if patches:
        files += [(p.relative_to(patches).as_posix(), p) for p in sorted(patches.rglob("*")) if p.is_file()]
    if values:
        files += [("values/" + p.name, p) for p in sorted(values.glob("*.toml"))]
    for name, p in files:
        h.update(name.encode() + b"\0")
        h.update(normalize(p.read_bytes()) + b"\0")
    return h.hexdigest()


def _replace(tmp: Path, dest: Path) -> None:
    for attempt in range(5):
        try:
            os.replace(tmp, dest)
            return
        except PermissionError:  # an editor or indexer holding the file on Windows
            if attempt == 4:
                raise
            time.sleep(0.2 * (attempt + 1))


def write_tree(tree: dict[str, bytes], out: Path, stamp: dict) -> int:
    """Writes the files that changed, removes the ones a previous apply wrote and this one does not."""
    old_stamp = out / STAMP
    previous = set()
    if old_stamp.is_file():
        previous = set(json.loads(old_stamp.read_text(encoding="utf-8")).get("files", []))
    written = 0
    for name, data in tree.items():
        dest = out / name
        if dest.is_file() and dest.read_bytes() == data:
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        tmp = dest.with_name(dest.name + ".nw-tmp")
        tmp.write_bytes(data)
        _replace(tmp, dest)
        written += 1
    for name in sorted(previous - tree.keys()):
        (out / name).unlink(missing_ok=True)
    stamp = {**stamp, "files": sorted(tree)}
    text = json.dumps(stamp, indent=1) + "\n"
    if not old_stamp.is_file() or old_stamp.read_text(encoding="utf-8") != text:
        old_stamp.write_text(text, encoding="utf-8", newline="\n")
    return written


def apply(decomp: Path = PRISTINE, out: Path = TREE, patches: Path | None = PATCHES,
          values: Path | None = VALUES) -> tuple[int, int]:
    """Builds or refreshes the patched tree (None: without the series or the value rules); returns (files, written)."""
    tree = patch_tree(worktree_files(decomp), patches, values)
    stamp = {"schema": SCHEMA, "decomp": decomp_sha(decomp), "series": series_hash(patches, values)}
    return len(tree), write_tree(tree, out, stamp)


def link_compiler(tree: Path, decomp: Path = PRISTINE) -> None:
    """The submodule's EE toolchain (setup_toolchain.py) under a patched tree, for the decomp's cc.py (Linux)."""
    link, target = tree / "compiler", decomp / "compiler"
    if not link.exists() and target.is_dir():
        link.symlink_to(target.resolve(), target_is_directory=True)


def tree_id(tree: Path) -> str | None:
    """The decomp commit and series a patched tree was built from (None for a plain checkout)."""
    stamp = tree / STAMP
    if not stamp.is_file():
        return None
    doc = json.loads(stamp.read_text(encoding="utf-8"))
    return f"{doc.get('decomp')}+{doc.get('series', '')[:12]}"


# -- value rules: single retail values wrapped in a macro ----------------------------------------------------------------

C_TOKEN = re.compile(r"""
    (?P<skip>\s+|/\*.*?\*/|//[^\n]*)
  | (?P<tok>[A-Za-z_]\w*
    | \.?[0-9](?:[eEpP][+-]|[\w.])*
    | "(?:\\.|[^"\\\n])*"
    | '(?:\\.|[^'\\\n])*'
    | ->|\+\+|--|<<=|>>=|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^]=|\.\.\.|\#\#
    | .)""", re.S | re.X)


@dataclass
class Site:
    fn: str
    pal: str
    nth: int
    of: int
    ntsc: str


@dataclass
class Values:
    origin: Path
    macro: str
    sites: dict[str, list[Site]]  # by decomp path


def c_tokens(text: str) -> list[tuple[int, int, str]]:
    """(start, end, spelling) of each token; comments and spacing are skipped."""
    return [(m.start(), m.end(), m.group("tok")) for m in C_TOKEN.finditer(text) if m.group("tok") is not None]


def function_bodies(toks) -> dict[str, tuple[int, int]]:
    """{function: (index of its "{", index of its "}")} for the definitions at file scope."""
    out, depth, i = {}, 0, 0
    while i < len(toks):
        s = toks[i][2]
        if s == "{" and depth == 0 and i > 0 and toks[i - 1][2] == ")":
            j, d = i - 1, 0
            while j >= 0:
                d += {")": 1, "(": -1}.get(toks[j][2], 0)
                if d == 0:
                    break
                j -= 1
            k, d = i, 0
            while k < len(toks):
                d += {"{": 1, "}": -1}.get(toks[k][2], 0)
                if d == 0:
                    break
                k += 1
            if j > 0:
                out.setdefault(toks[j - 1][2], (i, k))
            i = k + 1
            continue
        depth += {"{": 1, "}": -1}.get(s, 0)
        i += 1
    return out


def matches(spellings: list[str], pal: list[str]) -> list[int]:
    return [i for i in range(len(spellings) - len(pal) + 1) if spellings[i:i + len(pal)] == pal]


def load_values(values: Path | None = VALUES) -> list[Values]:
    out = []
    for path in sorted(values.glob("*.toml")) if values and values.is_dir() else []:
        doc = tomllib.loads(path.read_text(encoding="utf-8"))
        if doc.get("schema") != SCHEMA or not doc.get("macro"):
            raise PatchError(f"{path}: needs schema = {SCHEMA} and a macro")
        sites = {}
        for entry in doc.get("file", []):
            sites[entry["path"]] = [Site(s["fn"], s["pal"], s["nth"], s["of"], s["ntsc"]) for s in entry["sites"]]
            for s in sites[entry["path"]]:
                if not 0 <= s.nth < s.of:
                    raise PatchError(f"{path}: {entry['path']} {s.fn} {s.pal}: nth = {s.nth} is not one of its "
                                     f"{s.of} matches (0 to {s.of - 1})")
        out.append(Values(path, doc["macro"], sites))
    return out


def apply_values(rules: Values, path: str, data: bytes) -> bytes:
    """Each site's value, the nth of the `of` in its function, as macro(value, ntsc); a count that differs stops."""
    text = data.decode("latin-1")
    toks = c_tokens(text)
    bodies = function_bodies(toks)
    edits = []
    for site in rules.sites[path]:
        where = f"{rules.origin.name}: {path} {site.fn} {site.pal}"
        if site.fn not in bodies:
            raise PatchError(f"{where}: no function {site.fn}")
        first, last = bodies[site.fn]
        spellings = [s for _, _, s in toks[first:last + 1]]
        pal = [s for _, _, s in c_tokens(site.pal)]
        found = matches(spellings, pal)
        if len(found) != site.of:
            raise PatchError(f"{where}: {len(found)} in the function, the rule counts {site.of}")
        start, end = toks[first + found[site.nth]][0], toks[first + found[site.nth] + len(pal) - 1][1]
        edits.append((start, end, f"{rules.macro}({text[start:end]}, {site.ntsc})"))
    edits.sort()
    for (_, e0, _), (s1, _, _) in zip(edits, edits[1:]):
        if s1 < e0:
            raise PatchError(f"{rules.origin.name}: {path}: two sites overlap at offset {s1}")
    for start, end, insert in reversed(edits):
        text = text[:start] + insert + text[end:]
    return text.encode("latin-1")


# -- the work repository: edit, rebuild, rebase -----------------------------------------------------------------------

FILES = "files"  # subject of the commit with the new files
BASE = "decomp "  # subject prefix of a decomp commit
SERIES_HEADER = "# The port patch concerns in the order they apply; written by tools/patches.py rebuild.\n"
IDENTITY = {"GIT_AUTHOR_NAME": "patches", "GIT_AUTHOR_EMAIL": "patches@nuworld.invalid",
            "GIT_COMMITTER_NAME": "patches", "GIT_COMMITTER_EMAIL": "patches@nuworld.invalid",
            "GIT_AUTHOR_DATE": "946684800 +0000", "GIT_COMMITTER_DATE": "946684800 +0000"}
# Settings that change diff output, pinned so every machine writes the same patch files.
DIFF = ["-c", "diff.noprefix=false", "-c", "diff.mnemonicPrefix=false", "-c", "diff.suppressBlankEmpty=false",
        "-c", "core.quotepath=false", "diff", "--no-color", "--no-ext-diff", "--no-renames", "--no-relative",
        "--diff-algorithm=myers", "--indent-heuristic", "-U3"]


def wgit(work: Path, *args: str, data: bytes | None = None, check: bool = True) -> subprocess.CompletedProcess:
    env = {**os.environ, **IDENTITY, "GIT_EDITOR": ":", "GIT_SEQUENCE_EDITOR": ":"}
    proc = subprocess.run(["git", "-C", str(work), "-c", "core.autocrlf=false", "-c", "core.safecrlf=false", *args],
                          input=data, capture_output=True, env=env)
    if check and proc.returncode:
        raise PatchError(f"git {' '.join(args[:2])} in {work}: {proc.stderr.decode(errors='replace').strip()}")
    return proc


def _remove_tree(path: Path) -> None:
    def force(fn, p, _exc):  # git's read-only objects on Windows
        os.chmod(p, 0o700)
        fn(p)
    shutil.rmtree(path, **({"onexc": force} if sys.version_info >= (3, 12) else {"onerror": force}))


def _fast_import_commit(subject: str, body: str, parent: str | None, files: dict[str, bytes],
                        ref: str = "refs/heads/main", reset: bool = False) -> bytes:
    """One commit for git fast-import; without `parent` it follows the branch's last commit in the stream."""
    msg = (subject + ("\n\n" + body if body else "") + "\n").encode()
    out = [f"commit {ref}\n".encode(), b"author patches <patches@nuworld.invalid> 946684800 +0000\n",
           b"committer patches <patches@nuworld.invalid> 946684800 +0000\n", b"data %d\n" % len(msg), msg]
    if parent:
        out.append(f"from {parent}\n".encode())
    if reset:
        out.append(b"deleteall\n")
    for name, data in sorted(files.items()):
        out += [f"M 100644 inline {name}\n".encode(), b"data %d\n" % len(data), data, b"\n"]
    return b"".join(out)


def work_commits(work: Path) -> list[tuple[str, str, str]]:
    """(sha, subject, body) of the first-parent history, oldest first."""
    log = wgit(work, "log", "--first-parent", "--reverse", "--format=%H%x00%s%x00%b%x01", "main").stdout.decode()
    out = []
    for entry in log.split("\x01"):
        entry = entry.strip("\n")
        if entry:
            sha, subject, body = entry.split("\x00")
            out.append((sha, subject, body.strip()))
    return out


def work_layout(work: Path):
    """The last decomp commit, the files commit and the concern commits after it."""
    commits = work_commits(work)
    bases = [i for i, c in enumerate(commits) if c[1].startswith(BASE)]
    if not bases or len(commits) <= bases[-1] + 1 or commits[bases[-1] + 1][1] != FILES:
        raise PatchError(f"{work}: expected a '{BASE}<sha>' commit followed by '{FILES}' and the concerns")
    b = bases[-1]
    return commits[b], commits[b + 1], commits[b + 2:]


def work_busy(work: Path) -> str | None:
    """Why the work repository cannot be rebuilt or replaced, or None."""
    git_dir = work / ".git"
    if (git_dir / "rebase-merge").exists() or (git_dir / "rebase-apply").exists():
        return "a rebase is in progress (finish it with git rebase --continue, or --abort)"
    if wgit(work, "status", "--porcelain").stdout.strip():
        return "it has uncommitted edits (fold them into a concern with fixup)"
    return None


def setup(decomp: Path = PRISTINE, patches: Path = PATCHES, work: Path = WORK, force: bool = False) -> int:
    """(Re)creates the work repository from the decomp's HEAD and the series; returns the concern count."""
    if work.exists() and not force:
        why = work_busy(work) if (work / ".git").exists() else None
        if why is None and (work / ".git").exists() and rebuild_outputs(work) != current_outputs(patches):
            why = "its commits differ from port/patches (rebuild keeps them)"
        if why:
            raise PatchError(f"{work}: {why}; --force throws it away")
    if work.exists():
        _remove_tree(work)
    work.mkdir(parents=True)
    wgit(work, "init", "-q", "-b", "main")
    for key, value in (("core.autocrlf", "false"), ("user.name", "patches"), ("user.email", "patches@nuworld.invalid")):
        wgit(work, "config", key, value)
    sha = decomp_sha(decomp) or "HEAD"
    base = commit_files(decomp, "HEAD") if (decomp / ".git").exists() else worktree_files(decomp)
    concerns, new = load_series(patches)
    stream = [_fast_import_commit(f"{BASE}{sha}", "", None, base), _fast_import_commit(FILES, "", None, new)]
    tree = {**base, **new}
    for c in concerns:
        changed = {}
        for p in c.patches:
            if p.path not in tree:
                raise PatchError(f"{p.origin}: {p.path} is not in the decomp")
            tree[p.path] = changed[p.path] = apply_patch(p, tree[p.path])
        stream.append(_fast_import_commit(c.name, c.reason, None, changed))
    wgit(work, "fast-import", "--quiet", data=b"".join(stream))
    wgit(work, "reset", "-q", "--hard", "main")
    wgit(work, "tag", "-f", "base", "main~%d" % (len(concerns) + 1))
    return len(concerns)


def _split_diff(diff: bytes) -> dict[str, bytes]:
    """A multi-file git diff as one normalized patch per path."""
    out = {}
    for section in re.split(rb"(?m)^diff --git ", diff)[1:]:
        head, _, rest = section.partition(b"\n--- ")
        if not rest:
            raise PatchError(f"not a text change: {section.splitlines()[0].decode(errors='replace')}")
        if re.search(rb"(?m)^(new|deleted) file mode", head):
            raise PatchError(f"{section.splitlines()[0].decode(errors='replace')}: a concern only changes decomp "
                             "files (new files go in the files commit)")
        body = b"--- " + rest
        path = body.split(b"\n", 1)[0][len(b"--- a/"):].decode()
        out[path] = body
    return out


def rebuild_outputs(work: Path) -> dict[str, bytes]:
    """port/patches as the work repository's commits say: relative path -> bytes."""
    (base, _, _), (files_sha, _, _), concerns = work_layout(work)
    out = {}
    status = wgit(work, "diff", "--name-status", "-z", "--no-renames", base, files_sha).stdout.split(b"\0")
    for kind, name in zip(status[0::2], status[1::2]):
        if kind != b"A":
            raise PatchError(f"{FILES} commit: {name.decode()} is not a new file (patch it in a concern)")
        out[f"files/{name.decode()}"] = wgit(work, "show", f"{files_sha}:{name.decode()}").stdout
    series = [SERIES_HEADER, f"schema = {SCHEMA}\n"]
    parent = files_sha
    for sha, name, reason in concerns:
        if not re.fullmatch(r"[0-9]{2}-[a-z0-9-]+", name) or name == FILES:
            raise PatchError(f"concern commit {sha[:12]}: subject {name!r} is not a concern name like 70-widescreen")
        diffs = _split_diff(wgit(work, *DIFF, parent, sha).stdout)
        parent = sha
        if not diffs:  # a concern whose last change was folded out leaves the series
            continue
        for path, body in diffs.items():
            out[f"{name}/{path}.patch"] = body
        series.append(f"\n[[concern]]\nname = \"{name}\"\nreason = {json.dumps(reason, ensure_ascii=False)}\n")
    out["series.toml"] = "".join(series).encode()
    return out


def current_outputs(patches: Path) -> dict[str, bytes]:
    return {p.relative_to(patches).as_posix(): p.read_bytes() for p in patches.rglob("*") if p.is_file()}


def rebuild(work: Path = WORK, patches: Path = PATCHES) -> int:
    """Writes port/patches from the work repository; returns the number of files written or removed."""
    why = work_busy(work)
    if why:
        raise PatchError(f"{work}: {why}")
    new, old = rebuild_outputs(work), current_outputs(patches)
    changed = 0
    for name, data in new.items():
        if old.get(name) != data:
            dest = patches / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            changed += 1
    for name in sorted(old.keys() - new.keys()):
        (patches / name).unlink()
        changed += 1
    for d in sorted((p for p in patches.rglob("*") if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
        if not any(d.iterdir()):
            d.rmdir()
    return changed


def fixup(concern: str, reason: str | None = None, work: Path = WORK, patches: Path = PATCHES) -> int:
    """Commits the work repository's edits into `concern` (a new one at the end with `reason`), then rebuilds."""
    git_dir = work / ".git"
    if (git_dir / "rebase-merge").exists() or (git_dir / "rebase-apply").exists():
        raise PatchError(f"{work}: a rebase is in progress (finish it with git rebase --continue, or --abort)")
    wgit(work, "add", "-A")
    if not wgit(work, "diff", "--cached", "--quiet", check=False).returncode:
        raise PatchError(f"{work}: no edits to fold in")
    (base, _, _), files_commit, concerns = work_layout(work)
    target = {c[1]: c[0] for c in [files_commit, *concerns]}.get(concern)
    if target is None:
        if reason is None:
            raise PatchError(f"no concern {concern!r}; pass --new REASON to add it at the end of the series")
        wgit(work, "commit", "-q", "-m", concern, "-m", reason)
    else:
        wgit(work, "commit", "-q", "--fixup", target)
        proc = wgit(work, "rebase", "-q", "-i", "--autosquash", base, check=False)
        while (proc.returncode and (git_dir / "rebase-merge").exists()
               and not wgit(work, "diff", "--name-only", "--diff-filter=U").stdout.strip()):
            # The edits undo the whole concern: keep it as an empty commit, rebuild leaves it out.
            wgit(work, "commit", "-q", "--amend", "--allow-empty", "--no-edit")
            proc = wgit(work, "rebase", "--continue", check=False)
        if proc.returncode:
            raise PatchError(f"{work}: the edits conflict with a later concern; resolve the files there, "
                             "git add them, git rebase --continue, then tools/patches.py rebuild")
    return rebuild(work, patches)


def resolve_commit(decomp: Path, sha: str) -> str:
    try:
        # ^0, not ^{commit}: MSYS2's git, started from a Windows program, globs the braces away
        return git(decomp, "rev-parse", "--verify", f"{sha}^0").decode().strip()
    except subprocess.CalledProcessError:
        raise PatchError(f"{decomp} has no commit {sha} (git -C decomp fetch)") from None


def rebase(sha: str, decomp: Path = PRISTINE, patches: Path = PATCHES, work: Path = WORK) -> bool:
    """Moves the series onto decomp commit `sha`; False when git stopped at a conflict."""
    full = resolve_commit(decomp, sha)
    setup(decomp, patches, work)
    (old_base, _, _), _, _ = work_layout(work)
    stream = _fast_import_commit(f"{BASE}{full}", "", old_base, commit_files(decomp, full),
                                 ref="refs/heads/newbase", reset=True)
    wgit(work, "fast-import", "--quiet", data=stream)
    wgit(work, "tag", "-f", "base", "newbase")
    if wgit(work, "rebase", "-q", "--onto", "newbase", old_base, "main", check=False).returncode:
        return False
    rebuild(work, patches)
    return True


def cmd_apply(args) -> int:
    files, written = apply(args.decomp, args.out, args.patches)
    print(f"{files} files in {args.out}, {written} written")
    return 0


def cmd_setup(args) -> int:
    n = setup(args.decomp, args.patches, args.work, args.force)
    print(f"{args.work}: the decomp, {FILES} and {n} concerns; edit there, then tools/patches.py fixup <concern>")
    return 0


def cmd_fixup(args) -> int:
    changed = fixup(args.concern, args.new, args.work, args.patches)
    print(f"{args.concern}: {changed} files of {args.patches} rewritten")
    return 0


def cmd_rebuild(args) -> int:
    changed = rebuild(args.work, args.patches)
    print(f"{changed} files of {args.patches} rewritten")
    (_, subject, _), _, _ = work_layout(args.work)
    sha = subject[len(BASE):]
    if sha != decomp_sha(args.decomp):
        print(f"the series is on decomp {sha}: git -C {args.decomp} checkout {sha}")
    return 0


def cmd_rebase(args) -> int:
    full = resolve_commit(args.decomp, args.sha)
    if not rebase(full, args.decomp, args.patches, args.work):
        print(f"conflict: resolve the files in {args.work}, git add them, git -C {args.work} rebase --continue "
              f"(repeat until it ends), then tools/patches.py rebuild and git -C {args.decomp} checkout {full}",
              file=sys.stderr)
        return 1
    if git(args.decomp, "status", "--porcelain").strip():
        print(f"the series is on decomp {full}; the submodule has local edits: git -C {args.decomp} checkout {full}")
    else:
        git(args.decomp, "checkout", "-q", "--detach", full)
        print(f"the series and the submodule are on decomp {full}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--decomp", type=Path, default=PRISTINE, help="the decomp checkout (default: decomp/)")
    parser.add_argument("--patches", type=Path, default=PATCHES, help="the series (default: port/patches)")
    parser.add_argument("--work", type=Path, default=WORK, help="the work repository (default: build/patch-work)")
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("apply", help="build or refresh the patched tree")
    p.add_argument("--out", type=Path, default=TREE, help="the patched tree (default: build/decomp)")
    p.set_defaults(fn=cmd_apply)
    p = sub.add_parser("setup", help="make the work repository build/patch-work from port/patches")
    p.add_argument("--force", action="store_true", help="throw away the work repository's unsaved state")
    p.set_defaults(fn=cmd_setup)
    p = sub.add_parser("fixup", help="fold the work repository's edits into a concern, then rebuild")
    p.add_argument("concern", help=f"a concern of the series, or {FILES} for new files")
    p.add_argument("--new", metavar="REASON", help="add the concern at the end of the series")
    p.set_defaults(fn=cmd_fixup)
    p = sub.add_parser("rebuild", help="write port/patches from the work repository")
    p.set_defaults(fn=cmd_rebuild)
    p = sub.add_parser("rebase", help="move the series and the submodule onto another decomp commit")
    p.add_argument("sha")
    p.set_defaults(fn=cmd_rebase)
    args = parser.parse_args()
    try:
        return args.fn(args)
    except PatchError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
