#!/usr/bin/env bash
# Record a bounded video of the Gazebo GUI window (the "gz-sim-gui" X client).
#
# Run this in an environment that has ffmpeg and x11-utils (`xwininfo`) and can
# reach the X server that hosts the window -- normally the HOST, not the
# container: ghcr.io/marinholab/gazebo:jazzy ships neither tool. See SKILL.md
# "Where to run the capture".
#
# Usage:  record_gui.sh [OUT_MP4] [SECONDS] [WINDOW_SUBSTRING]
#         OUT_MP4            output path (default: ./gz_sim.mp4)
#         SECONDS            recording length (default: 10) -- REQUIRED to be finite
#         WINDOW_SUBSTRING   window name to match, case-sensitive (default: auto)
#
# Unlike a bare `ffmpeg -f x11grab`, this script cannot record forever: the
# duration is always finite and the process always terminates.
#
# Exit 0 on a successful recording, non-zero otherwise.
set -uo pipefail

OUT="${1:-./gz_sim.mp4}"
DUR="${2:-10}"
MATCH="${3:-}"

# x11grab is a live source with no end-of-stream, so a non-finite duration means
# "record until killed" -- and a killed x11grab encode produces no usable MP4 at
# all (the muxer dies before writing the moov index). Refuse rather than hang.
if ! awk -v d="$DUR" 'BEGIN{exit !(d ~ /^[0-9]+(\.[0-9]+)?$/ && d+0 > 0)}'; then
  echo "ERROR: duration must be a positive number of seconds, got '$DUR'." >&2
  echo "       x11grab has no EOF: without a finite length ffmpeg never exits" >&2
  echo "       and a killed encode yields an unplayable file." >&2
  exit 2
fi

# x11grab addresses the display as ":<n>+X,Y", so DISPLAY must be set and bare.
DISPLAY="${DISPLAY:-:0}"
export DISPLAY

command -v ffmpeg   >/dev/null || { echo "ERROR: ffmpeg not found (run on the host)."  >&2; exit 1; }
command -v xwininfo >/dev/null || { echo "ERROR: xwininfo not found; apt-get install x11-utils." >&2; exit 1; }
command -v ffprobe  >/dev/null || { echo "ERROR: ffprobe not found; apt-get install ffmpeg."    >&2; exit 1; }

# Window selection identical to snapshot_gui.sh -- see the notes there.
find_window() {
  xwininfo -root -tree 2>/dev/null | awk -v m="$MATCH" '
    { id = $1 }
    id ~ /^0x[0-9a-f]+$/ && /"[^"]*"/ {
      line = $0
      if (match(line, /"[^"]*"/)) {
        name = substr(line, RSTART + 1, RLENGTH - 2)
      } else next
      n = split(line, f, /[ ]+/)
      geo = ""
      for (i = n; i >= 1; i--) if (f[i] ~ /[0-9]+x[0-9]+\+/) { geo = f[i]; break }
      if (geo == "") next
      split(geo, d, /[x+]/)
      w = d[1] + 0; h = d[2] + 0
      if (w * h < 40000) next
      if (name ~ /guard window/) next
      if (line ~ /mutter-x11-frames/ && line !~ /gz-sim-gui/) next
      if (m != "" && index(name, m) == 0) next
      score = 0
      if (line ~ /gz-sim-gui/) score += 1000000
      if (name ~ /Gazebo/)     score += 1000
      score += int(w * h / 10000)
      printf "%d\t%s\t%s (%dx%d)\n", score, id, name, w, h
    }' | sort -rn | head -1 | cut -f2
}

for _ in $(seq 1 12); do
  WIN=$(find_window)
  [ -n "${WIN:-}" ] && break
  sleep 2
done

if [ -z "${WIN:-}" ]; then
  echo "ERROR: no Gazebo GUI window found. Is 'gz sim' running with its GUI" >&2
  echo "(i.e. without -s), and is DISPLAY=$DISPLAY the display it mapped to?" >&2
  xwininfo -root -tree 2>/dev/null | grep -iE 'gazebo|gz-' >&2
  exit 1
fi

GEOINFO=$(xwininfo -id "$WIN" 2>/dev/null)
X=$(awk '/Absolute upper-left X/{print $NF}' <<<"$GEOINFO")
Y=$(awk '/Absolute upper-left Y/{print $NF}' <<<"$GEOINFO")
W=$(awk '/^  Width/{print $NF}'            <<<"$GEOINFO")
H=$(awk '/^  Height/{print $NF}'           <<<"$GEOINFO")
if [ -z "${X:-}" ] || [ -z "${W:-}" ]; then
  echo "ERROR: could not read geometry for window $WIN" >&2
  exit 1
fi

# The grace timeout is a safety net only: ffmpeg self-terminates at -t $DUR, so
# this fires solely if the grab wedges (a dead X server), never on a normal take.
echo "recording ${W}x${H} at +${X}+${Y} for ${DUR}s -> $OUT"
timeout -k 5 "$(awk -v d="$DUR" 'BEGIN{printf "%d", d + 15}')" \
  ffmpeg -loglevel error -nostdin -t "$DUR" -f x11grab \
         -video_size "${W}x${H}" -framerate 30 -i ":${DISPLAY#:}+$X,$Y" \
         -vf "scale=trunc(iw/2)*2:trunc(ih/2)*2" \
         -pix_fmt yuv420p -crf 20 -preset fast -y "$OUT"
RC=$?
[ $RC -eq 0 ] || { echo "ERROR: ffmpeg exited $RC" >&2; exit $RC; }

# Confirm the muxer finished: a bounded clip has an index and >0 frames.
INFO=$(ffprobe -hide_banner -loglevel error \
        -show_entries format=duration -show_entries stream=nb_frames \
        -of default=nw=1 "$OUT" 2>&1) || { echo "ERROR: unreadable $OUT ($INFO)" >&2; exit 1; }
FRAMES=$(awk -F= '/nb_frames/{print $2}' <<<"$INFO" | head -1)
if [ -z "${FRAMES:-}" ] || [ "$FRAMES" -eq 0 ]; then
  echo "ERROR: $OUT has no frames -- capture produced an empty encode" >&2
  exit 1
fi
echo "recorded $(xwininfo -id "$WIN" | awk '/Window id/{print $3" "$4}') ${W}x${H} -> $OUT (${FRAMES} frames)"
echo "NOTE: a static world or paused sim records a still. See SKILL.md" >&2
echo "      'Make sure something is actually moving' before trusting the clip." >&2
