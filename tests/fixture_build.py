"""The fixture program of tests/fixtures/mini, as the build makes the real one: wrappers, stubs, function map,
PS2 mirror structs, converters and the data image, compiled and linked with clang or $CC around a harness."""
import copy
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import gen_converters  # noqa: E402
import gen_dataimage  # noqa: E402
import gen_funcmap  # noqa: E402
import gen_ps2types  # noqa: E402
import gen_stubs  # noqa: E402
import nwunits  # noqa: E402
import tu_filter  # noqa: E402
import typedb  # noqa: E402

FIXTURE = REPO / "tests" / "fixtures" / "mini"
DECOMP = FIXTURE / "decomp"
UNITS = FIXTURE / "port" / "units.toml"
PORT_SRC = FIXTURE / "port" / "src"
FLAGS = ["-std=gnu11", "-fgnu89-inline", "-fno-strict-aliasing", "-fwrapv", "-ffp-contract=off",
         "-fsigned-char", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-DNW_HOST"] + \
        (["-mno-ms-bitfields"] if sys.platform == "win32" else [])


def find_cc():
    cc = os.environ.get("CC") or shutil.which("clang")
    return cc if cc and shutil.which(cc) else None


def fixture_db() -> dict:
    """The fixture's type DB without struct fixlong_s, whose long the converters refuse on purpose."""
    db = typedb.build(DECOMP)
    clean = copy.deepcopy(db)
    del clean["records"]["struct fixlong_s"]
    return clean


def generate(out: Path, db: dict | None = None, decomp: Path = DECOMP, units: Path = UNITS):
    """Writes every generated file under out; returns (model, image)."""
    model = nwunits.load(decomp, units, PORT_SRC)
    assert model.errors == [], model.errors
    built = [u for u in model.units if u.kind != "none"]
    for unit in built:
        nwunits.write_if_changed(gen_stubs.output_path(out, unit), gen_stubs.render(unit))
        nwunits.write_if_changed(out / (unit.name + ".funcmap.inc"), gen_funcmap.render_unit(unit))
    nwunits.write_if_changed(out / "funcmap.c", gen_funcmap.render_index(built))
    parser = tu_filter.Parser(decomp)
    try:
        for unit in built:
            if unit.kind == "wrapper":
                path = decomp / unit.source
                wrapper = tu_filter.output_path(out, unit)
                defs = parser.definitions(path)
                text, _names = tu_filter.render(unit, path.read_bytes(), defs, path.as_posix(), wrapper.as_posix())
                tu_filter.write_if_changed(wrapper, text)
                header = tu_filter.retail_header(unit, defs)
                if header is not None:
                    tu_filter.write_if_changed(tu_filter.retail_path(out, unit), header.encode())
    finally:
        parser.close()

    db = db or fixture_db()
    ann, errors = gen_converters.load_annotations(FIXTURE / "port" / "convert.toml")
    assert errors == [], errors
    data, errors = gen_dataimage.load_data_toml(FIXTURE / "port" / "data.toml")
    assert errors == [], errors
    mirror, mirror_out = gen_ps2types.generate(db)
    gen, conv_out = gen_converters.generate(db, ann)
    assert mirror.errors + gen.errors == [], mirror.errors + gen.errors
    image, image_out = gen_dataimage.generate(model, decomp, db, ann, data)
    assert image.errors == [], image.errors
    for name, text in {**mirror_out, **conv_out, **image_out}.items():
        nwunits.write_if_changed(out / name, text)
    return model, image


def build(cc: str, out: Path, harness: Path, exe: Path) -> subprocess.CompletedProcess:
    """Compiles the generated program around harness into exe."""
    model = nwunits.load(DECOMP, UNITS, PORT_SRC)
    sources = [out / "funcmap.c", out / "nw_convert.c", out / "dataimage.c", out / "dataimage_types.c",
               REPO / "src" / "port" / "stub.c", REPO / "src" / "port" / "funcmap.c",
               REPO / "src" / "data" / "convert.c", REPO / "src" / "data" / "elf.c",
               REPO / "src" / "data" / "image.c", FIXTURE / "port" / "xhooks.c", harness,
               *sorted(PORT_SRC.glob("*.c"))]
    for unit in model.units:
        if unit.kind == "standalone":
            sources.append(gen_stubs.output_path(out, unit))
        elif unit.kind == "wrapper":
            sources.append(tu_filter.output_path(out, unit))
    return subprocess.run([cc, *FLAGS, f"-I{DECOMP / 'include'}", f"-I{REPO / 'src'}", f"-I{out}",
                           "-o", str(exe), *map(str, sources)], capture_output=True, text=True)
