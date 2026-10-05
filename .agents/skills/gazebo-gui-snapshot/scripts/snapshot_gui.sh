#!/usr/bin/env bash
# Capture one screenshot of the Gazebo GUI window (the "gz-sim-gui" X client).
#
# Run this in an environment that has ImageMagick (`import`) and x11-utils
# (`xwininfo`) and can reach the X server that hosts the window -- normally the
# HOST, not the container: ghcr.io/marinholab/gazebo:jazzy ships neither tool.
# See SKILL.md "Where to run the capture".
#
# Usage:  snapshot_gui.sh [OUT_PNG] [WINDOW_SUBSTRING]
#         OUT_PNG            output path (default: ./gz_snapshot.png)
#         WINDOW_SUBSTRING   window name to match, case-sensitive (default: auto)
#
# Exit 0 on a successful capture, non-zero otherwise.
set -uo pipefail

OUT="${1:-./gz_snapshot.png}"
MATCH="${2:-}"

# The Gazebo GUI creates two kinds of windows: the visible top-level one
# (named "Gazebo GUI" / "Gazebo Sim", class "gz-sim-gui") and tiny 1x1 or 3x3
# utility windows ("Gazebo GUI", "Qt Selection Owner for gz-sim-gui"). Only the
# large one holds the rendered scene, so filter on area and prefer the class.
find_window() {
  xwininfo -root -tree 2>/dev/null | awk -v m="$MATCH" '
    { id = $1 }
    # match a hex window id followed by a quoted name, and geometry WxH+X+Y
    id ~ /^0x[0-9a-f]+$/ && /"[^"]*"/ {
      line = $0
      # window name = text between the first pair of double quotes
      if (match(line, /"[^"]*"/)) {
        name = substr(line, RSTART + 1, RLENGTH - 2)
      } else next
      # geometry = last field of form WxH+X+Y
      n = split(line, f, /[ ]+/)
      geo = ""
      for (i = n; i >= 1; i--) if (f[i] ~ /[0-9]+x[0-9]+\+/) { geo = f[i]; break }
      if (geo == "") next
      split(geo, d, /[x+]/)
      w = d[1] + 0; h = d[2] + 0
      if (w * h < 40000) next            # skip 1x1 / 3x3 utility windows
      if (name ~ /guard window/) next    # compositor overlay, not a real window
      if (line ~ /mutter-x11-frames/ && line !~ /gz-sim-gui/) next  # WM frame
      if (m != "" && index(name, m) == 0) next
      score = 0
      if (line ~ /gz-sim-gui/) score += 1000000   # real Qt GUI window
      if (name ~ /Gazebo/)     score += 1000
      score += int(w * h / 10000)
      printf "%d\t%s\t%s (%dx%d)\n", score, id, name, w, h
    }' | sort -rn | head -1 | cut -f2
}

# Retry while the window is still mapping (the GUI takes a few seconds to appear).
for _ in $(seq 1 12); do
  WIN=$(find_window)
  [ -n "${WIN:-}" ] && break
  sleep 2
done

if [ -z "${WIN:-}" ]; then
  echo "ERROR: no Gazebo GUI window found. Is 'gz sim' running with its GUI" >&2
  echo "(i.e. without -s), and is DISPLAY=$DISPLAY the display it mapped to?" >&2
  echo "Windows visible right now:" >&2
  xwininfo -root -tree 2>/dev/null | grep -iE 'gazebo|gz-' >&2
  exit 1
fi

# -window takes the X window id; capture off the composited screen contents.
if ! import -window "$WIN" "$OUT" 2>/dev/null; then
  echo "WARN: direct window grab failed (redirected/obscured window);" >&2
  echo "      retrying after raising it." >&2
  xwininfo -id "$WIN" >/dev/null 2>&1 || { echo "ERROR: window vanished" >&2; exit 1; }
  # Fallback: grab the window's absolute region from the root window.
  ABS=$(xwininfo -id "$WIN" 2>/dev/null | awk '/^-geometry/{print $2}')
  if [ -z "$ABS" ]; then
    ABS=$(xwininfo -id "$WIN" 2>/dev/null |
      awk '/Absolute upper-left X/{x=$NF} /Absolute upper-left Y/{y=$NF}
           /Width/{w=$NF} /Height/{h=$NF} END{print w"x"h"+"x"+"y}')
  fi
  if [ -n "$ABS" ] && import -window root -crop "$ABS" "$OUT" 2>/dev/null; then
    echo "captured region $ABS of root -> $OUT"
  else
    echo "ERROR: could not grab window $WIN. Install ImageMagick, or use the" >&2
    echo "Gazebo Screenshot GUI plugin as described in SKILL.md." >&2
    exit 1
  fi
fi

[ -s "$OUT" ] || { echo "ERROR: $OUT is empty" >&2; exit 1; }
echo "captured $(xwininfo -id "$WIN" 2>/dev/null | awk '/Window id/{print $3" "$4}' || echo "$WIN") -> $OUT"
