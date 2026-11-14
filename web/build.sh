#!/usr/bin/env bash
# Build the browser simulator: real ZeroShell compiled to WebAssembly, inlined
# into one self-contained HTML page.
#   web/build.sh [out.html]        (needs Emscripten: source emsdk_env.sh first)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
OUT=$(realpath -m "${1:-$ROOT/build/web/zeroos-simulator.html}")
emcmake cmake -S "$ROOT/shell" -B "$ROOT/build/web" -DZS_BACKEND=web >/dev/null
cmake --build "$ROOT/build/web" -j"$(nproc)"
python3 - "$HERE/template.html" "$ROOT/build/web/zeroshell.js" "$OUT" <<'PY'
import sys
tpl, js, out = sys.argv[1:]
html = open(tpl).read()
code = open(js).read()
assert "</script" not in code
open(out, "w").write(html.replace("/*__ZEROSHELL_JS__*/", code))
print(f"{out} ({len(html) + len(code)} bytes)")
PY
