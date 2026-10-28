#!/usr/bin/env bash
# Runs in a Linux container on the self-hosted runner: /src is the runner's checkout (CRLF on Windows),
# so the tree is re-checked out from its objects into /w. /out carries files between the two steps.
set -euo pipefail

git config --global --add safe.directory '*'
git init -q /w
cd /w
git -c uploadpack.allowAnySHA1InWant=true fetch -q --depth 1 /src HEAD
git checkout -q FETCH_HEAD
git submodule init -q
git config submodule.decomp.url /src/.git/modules/decomp
git -c protocol.file.allow=always -c uploadpack.allowAnySHA1InWant=true submodule update -q --depth 1

case "$1" in
build)
  python3 tools/deny_gamedata.py

  python3 tools/gen_stubs.py --check
  python3 tools/tu_filter.py --out build/gen
  python3 tools/gen_stubs.py --out build/gen
  python3 tools/gen_funcmap.py --out build/gen
  python3 tools/typedb.py --out build/gen
  python3 tools/gen_ps2types.py --check
  python3 tools/gen_converters.py --check
  python3 tools/gen_dataimage.py --check
  CC=clang python3 -m unittest discover -s tests -v

  cmake --preset release
  cmake --build --preset release
  ctest --test-dir build/release --output-on-failure
  test "$(./build/release/crashwoc-nuworld --version)" = "CrashWOC: NuWorld 0.7.0"

  data_home="$(mktemp -d)"
  status=0
  XDG_DATA_HOME="$data_home" ./build/release/crashwoc-nuworld --verify > "$data_home/verify.log" 2>&1 || status=$?
  test "$status" = 1
  test -d "$data_home/crashwoc-nuworld"
  test ! -e "$data_home/NuWorld"
  grep -F "$data_home/crashwoc-nuworld/disc/" "$data_home/verify.log"

  for mode in --check-data --check-scenes "--scene LEVELS/B/HUB/HUB.GSC" --boot-check; do
    status=0
    ./build/release/crashwoc-nuworld --data-dir "$(mktemp -d)" $mode || status=$?
    test "$status" = 2
  done

  cp build/release/crashwoc-nuworld build/gen/typedb.json /out/
  ;;
probe)
  mkdir -p build/gen
  cp /out/typedb.json build/gen/
  (cd decomp && python3 tools/setup_toolchain.py --download)
  NUWORLD_PROBE_DIRECT=1 python3 tools/typedb_probe.py
  ;;
esac
