#!/usr/bin/env bash
# build.sh — compile decode_png.cpp against the installed Gazebo transport/msgs.
#
# Auto-detects the versioned pkg-config modules (gz-transportNN / gz-msgsMM) so
# the same source builds on Gazebo Harmonic (gz-transport13 + gz-msgs10) as well
# as Jetty (gz-transport8 + gz-msgs5) and any other version present.
#
# Usage:  ./build.sh [output-binary]
# Output:  ./decode_png  (or the name you pass)

set -euo pipefail

OUT="${1:-decode_png}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/decode_png.cpp"

# ---- locate versioned pkg-config modules ------------------------------------
find_pkg() {  # find_pkg <prefix>  -> prints the base (unversioned-suffix) module
  local prefix="$1" mod
  mod="$(pkg-config --list-all 2>/dev/null | awk -v p="$prefix" '
         {n=split($1,a," "); name=a[1]
          if (index(name,p)==1 && name !~ /-[a-z]+$/) print name}' \
         | sort -t '-' -k3 -n | tail -1)"
  if [ -z "${mod:-}" ]; then
    echo "error: no pkg-config module matching '$prefix*' found (need gz-transport / gz-msgs dev headers)" >&2
    exit 1
  fi
  echo "$mod"
}

T="$(find_pkg gz-transport)"
M="$(find_pkg gz-msgs)"
echo "using $T + $M"

CFLAGS="$(pkg-config --cflags "$T" "$M")"
LIBS="$(pkg-config --libs "$T" "$M") -lz"

g++ -std=c++17 -O2 -Wall -Wextra "$SRC" $CFLAGS $LIBS -o "$OUT"

echo "built: $(pwd)/$OUT"
