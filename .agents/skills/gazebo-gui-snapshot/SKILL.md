---
name: gazebo-gui-snapshot
description: This skill should be used when the user asks to "screenshot the Gazebo GUI", "take a snapshot of the Gazebo window", "capture what gz sim is showing", "take a screenshot from the gazebo container", "screenshot gz sim shapes.sdf", "record a video of gz sim", "screen record the Gazebo simulation", or wants an image or video of the rendered Gazebo Sim window running in `ghcr.io/marinholab/gazebo:jazzy`. Covers launching `gz sim` with its GUI in the container on the host's X server, finding the right X window, capturing stills with `import` and bounded video with `ffmpeg` — and warns that pixel/statistics checks are NOT a valid way to confirm the capture succeeded.
---

# Gazebo GUI Window Snapshot & Video

Capture a screenshot **or video** of the **Gazebo Sim GUI window** rendered by
`gz sim` inside `ghcr.io/marinholab/gazebo:jazzy` (ROS 2 Jazzy + Gazebo
Harmonic, gz-sim 8.11.0), on an Ubuntu host with a running X session.

This is the **headful** path: the GUI renders on the host's X server and the
window is grabbed with X tooling. For grabbing a *camera sensor* frame without
any display, see the sibling skill `gazebo-headless-camera-test` instead.

## Sample snapshots (verified output)

| Asset | Command | Notes |
|---|---|---|
| `assets/snapshot_shapes_world.png` | `gz sim shapes.sdf` | 1000x845, world with primitive shapes |
| `assets/snapshot_default_world.png` | `gz sim` (no world) | 960x540, default empty world |

Both were captured with the workflow below and **confirmed correct by the
maintainer**. Use them as the visual reference for what a good capture looks
like: gray Qt panels, a toolbar, a large light-gray viewport. Note that they
are overwhelmingly gray — *that is normal*, see the next section.

## ⚠️ Do not verify a GUI capture by pixel analysis

**This is the single most important point in this skill.** A screenshot of the
Gazebo GUI is mostly flat gray by definition, and that tells you nothing about
whether rendering works.

The Gazebo GUI is a Qt application using a light theme. Most of its pixels are
panel chrome, toolbars, sidebars and a pale viewport background — grays and
whites. Any of these checks will produce a **false alarm**:

- counting unique colors / distinct colors
- flagging that the dominant colors are `(245,245,245)`, `(238,238,238)`,
  `(231,231,231)` …
- concluding "it's all gray, so the GPU/render path is broken"

Measured from `assets/snapshot_shapes_world.png`, a capture confirmed to be
correct: the viewport region has ~11,600 distinct colors and its most common
colors are all achromatic grays. A real, correctly-rendered scene looks
exactly like a flat gray image under these statistics. A scene with *no* models
(plain `gz sim`) also looks the same. **You cannot distinguish these cases from
the histogram.**

Wasted time here is a real risk: in the session this skill was written from,
several steps were spent chasing a "flat gray = broken rendering" conclusion on
a capture that was perfectly fine. The maintainer confirmed it was fine.

**Instead, do this:**

1. Confirm the capture mechanically — the command exits 0 and the PNG has the
   dimensions of the GUI window (read them from `xwininfo`, not from the pixels).
2. **Show the image to the user and ask them to confirm it looks right.** This
   is the intended verification step; it takes one message and ends the guesswork.
3. Only investigate a render failure if the *user* says the image looks wrong.

The one case where a flat image *is* diagnostic: a capture from a **camera
sensor topic** (`gz.msgs.Image`) that is a *single* uniform color. That means
the sensor really did not render — see `gazebo-headless-camera-test`. This
exception applies to raw sensor buffers, not to GUI window screenshots.

## Prerequisites

- Docker on an Ubuntu host with a running X session (X11, not Wayland-only).
- The Gazebo image (multi-arch): `docker pull ghcr.io/marinholab/gazebo:jazzy`
- Host X access for the container's root user:

  ```bash
  xhost +local:root
  ```

  (Revoke later with `xhost -local:root`.)

- Capture tooling **on the host**: `sudo apt-get install -y x11-utils imagemagick`
  (provides `xwininfo` and `import`), plus `ffmpeg` if you want video
  (`sudo apt-get install -y ffmpeg`).

### Where to run the capture: the host, not the container

The `ghcr.io/marinholab/gazebo:jazzy` image ships **`xwininfo` only**. It has
**no** `xdotool`, `import`/ImageMagick, `scrot`, `gnome-screenshot`, `xwd`, `ffmpeg`,
or `python3-xlib`. So either:

- **run the capture on the host** (recommended — the window is a normal host
  window because `/tmp/.X11-unix` is bind-mounted and `DISPLAY` passed through), or
- `apt-get install -y xdotool imagemagick` **inside** the container first.

The bundled `scripts/snapshot_gui.sh` is written for the host.

## Workflow

### 1. Launch the container with the GUI

Using the repo's compose file (starts a bare `gz sim`, i.e. the default world):

```bash
mkdir -p ~/marinholab/docker/gazebo/jazzy/
cd ~/marinholab/docker/gazebo/jazzy/
curl -OL https://raw.githubusercontent.com/MarinhoLab/gazebo/refs/heads/main/jazzy/compose.yml
xhost +local:root
docker compose up
```

Key parts of that compose file: `DISPLAY` passthrough, `privileged: true`, and
`/tmp/.X11-unix` + `~/.Xauthority` bind mounts.

**Do not pass `-s`** — `gz sim -s` is server-only and starts no GUI.
**Do not add `--headless-rendering`** for this workflow.

### 2. Run a specific world (e.g. `shapes.sdf`)

The compose container runs `gz sim` with no world argument (it is the container's
main process), so to run a *different* world, start a separate container.
Gazebo's bundled example worlds live in the image at
`/usr/share/gz/gz-sim8/worlds/` (125 `.sdf` files, including `shapes.sdf` — list
them with `docker exec <c> ls /usr/share/gz/gz-sim8/worlds/`):

```bash
docker run -d --name gz_shapes --privileged -e DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix -v ~/.Xauthority:/root/.Xauthority \
  ghcr.io/marinholab/gazebo:jazzy \
  /bin/bash -c "cd /usr/share/gz/gz-sim8/worlds && gz sim shapes.sdf"
```

`cd` into the worlds directory (or pass the full path, or export
`GZ_SIM_RESOURCE_PATH`) so the world and anything it `#include`s resolve —
a bare relative filename is not reliably found from another working directory.

Avoid two `gz sim` instances sharing topics: stop the compose stack
(`docker compose down`) before starting the second container.

### 3. Confirm the window mapped

```bash
DISPLAY=:0 xwininfo -root -tree | grep -i gazebo
```

A typical result:

```
0x8004e8 "Gazebo Sim (on 67308d3b6e6c)": ("mutter-x11-frames" "mutter-x11-frames") 1028x911+1956+70
    0xa0001a "Gazebo Sim": ("gz-sim-gui" "Gazebo GUI")                              1000x845+14+49
0xa0001c "Gazebo GUI": ()                                                            1x1+0+0
0xa00004 "Qt Selection Owner for gz-sim-gui": ()                                     3x3+0+0
```

Reading that tree is the main gotcha — see below.

### 4. Capture

```bash
DISPLAY=:0 bash scripts/snapshot_gui.sh ./gz_snapshot.png
```

Or the manual equivalent, grabbing the **GUI** window id (not the WM frame):

```bash
DISPLAY=:0 import -window 0xa0001a ./gz_snapshot.png
```

Then **show the PNG to the user for confirmation** (see the warning above).
Copy it out with `docker cp` only if you captured inside the container.

## Recording a video

Same window selection as a still, one `ffmpeg` command — there is no separate
capture API to learn. Run it on the **host** (the container has no `ffmpeg`).

The script wraps the command below and enforces every rule in this section,
including refusing a non-finite duration:

```bash
DISPLAY=:0 bash scripts/record_gui.sh ./gz_sim.mp4 10
```

The manual equivalent:

```bash
# Resolve the GUI window id, geometry and size from the X tree (no hardcoded ids).
W=$(DISPLAY=:0 xwininfo -root -tree | grep gz-sim-gui | grep -oE '0x[0-9a-f]+' | head -1)
read -r GEO SIZE <<< $(DISPLAY=:0 xwininfo -id $W | awk '
  /Absolute upper-left X/{x=$NF} /Absolute upper-left Y/{y=$NF}
  /Width/{w=$NF} /Height/{h=$NF} END{print x","y" "w"x"h}')

# -t gives the recording a length. NEVER omit it in a scripted capture — see below.
ffmpeg -loglevel error -t 10 -f x11grab -video_size "$SIZE" -framerate 30 \
       -i ":0.0+$GEO" \
       -vf "scale=trunc(iw/2)*2:trunc(ih/2)*2" \
       -pix_fmt yuv420p -crf 20 -preset fast gz_sim.mp4
```

### Required: give the recording a length (`-t`)

**`-t <seconds>` is mandatory for any scripted capture, and it belongs before
`-i`.** `x11grab` is a live source with no end-of-stream, so without `-t` the
`ffmpeg` process never exits: it records until something kills it, and the
ffmpeg line blocks. A script or terminal-tool call containing the command above
minus `-t` therefore **hangs forever** while the encoder burns a core and the
output file grows without bound — the conversation appears wedged with
no new events, which looks like an agent failure rather than a missing flag.

Stopping it by hand does not rescue such a take either. `SIGINT`/`SIGTERM` kill
`ffmpeg` *before* it writes the MP4 `moov` index, so the file is unplayable, and
under `x11grab` the muxer is killed so early that **no output file is created at
all**. Measured on a 1000x845 GUI window, killed ~4 s in:

| Command | Wall | Result |
|---|---|---|
| `timeout -s INT 4 ffmpeg -f x11grab …` | 4.0 s | **no output file at all** |
| `ffmpeg -t 4 -f x11grab …` | 4.1 s | h264, 1000x844, 120 frames, 4.000 s |

So **do not** "fix" a runaway recording by wrapping it in `timeout`: that
guarantees you lose the clip. Let `-t` end the encode cleanly so `ffmpeg`
reaches its own trailer (`video:… bytes written`), and only then probe it.
Interactive use is the one exception — press `q` in ffmpeg's own terminal and
wait for that trailer line before moving on.

### Required: the even-dimension scale filter

**Keep `-vf "scale=trunc(iw/2)*2:trunc(ih/2)*2"`.** h264 with `yuv420p` requires
even width and height, and the Gazebo GUI window is frequently **odd** (observed:
1000x845). Without the filter, encoding aborts immediately:

```
[libx264] height not divisible by 2 (1000x845)
Error while opening encoder
```

The filter rounds both dimensions down (1000x845 → 1000x844). Alongside `-t`,
nothing else in the command is optional in the same way.

### Make sure something is actually moving

An h264 encode of a static scene is tiny (a motionless 4-second GUI clip measured
~7.7 KB) and indistinguishable from a still. Two launch-level causes:

- **The world is static.** The bundled `shapes.sdf` marks every model
  `<static>true</static>` and loads **no** physics plugin, so nothing moves no
  matter how long the sim runs. Pick a dynamic world to film motion.
- **The sim is paused.** `gz sim <world>.sdf` (as in the repo `compose.yml`)
  starts **paused**; add `-r` to run it. Check by sampling twice — iterations must
  climb:

  ```bash
  docker exec <c> bash -c 'gz topic -e -t /world/<w>/stats | grep -m1 iterations;
                           sleep 2; gz topic -e -t /world/<w>/stats | grep -m1 iterations'
  ```

  Relaunching with `-r` is the reliable way to get a running sim: publishing
  `gz.msgs.WorldControl` with `pause: false` via `gz topic -p` silently did
  **nothing** in testing (iterations kept climbing through a `pause: true`
  publish). Use the GUI's play button for ad-hoc resume, or just relaunch.

### Verifying a recording

Comparing **two frames of the same video** (a diff) is a valid motion check,
unlike the single-frame color statistics warned about above — it measures change
rather than appearance:

```bash
for t in 0 3; do ffmpeg -loglevel error -ss $t -i gz_sim.mp4 -frames:v 1 -y f_$t.png; done
compare -metric AE f_0.png f_3.png null: 2>&1; echo   # differing pixels
```

Zero differing pixels across the clip means the scene was static, not that capture
failed. As with stills, for anything you actually care about, **show the clip to
the user** rather than reasoning from byte counts.

### Alternative: Gazebo's own VideoRecorder plugin

`libVideoRecorder.so` ships in the image at
`/opt/ros/jazzy/opt/gz_sim_vendor/lib/gz-sim-8/plugins/gui/`. Adding a `<gui>`
block with that plugin renders offscreen, so it does not depend on the window
staying visible or unobscured — preferable for long or precisely-timed captures,
and it cannot produce the unbounded-recording hang above. For a specific
viewpoint (rather than the viewport camera), use a camera sensor topic instead:
see `gazebo-headless-camera-test`.

## Window-selection gotchas

From the tree in step 3, exactly one window holds the rendered scene. Pick wrong
and you get a slightly-wrong-size image or garbage:

| Window | What it is | Capture it? |
|---|---|---|
| `"Gazebo Sim (on <container-id>)"` / class `mutter-x11-frames` | the **window-manager frame** (title bar + borders), ~28×66 px larger | No — includes decorations |
| `"Gazebo Sim"` / class `gz-sim-gui` | **the actual Qt GUI** | **Yes** |
| `"Gazebo GUI"` 1x1 | X utility/ownership window | No |
| `"Qt Selection Owner for gz-sim-gui"` 3x3 | Qt clipboard helper | No |

Also:

- **The title depends on how `gz sim` was started.** Bare `gz sim` titled its
  window `"Gazebo quick start"`; `gz sim shapes.sdf` titled it `"Gazebo Sim"`.
  Newer builds append `" (on <12-hex-container-id>)"`. Match on the
  **`gz-sim-gui` class**, not a hardcoded title.
- `xdotool` is **absent** from the image (and often from the host), hence the
  `xwininfo -tree` + awk parsing in `scripts/snapshot_gui.sh`.
- If you score/sort candidates numerically, be careful: awk prints large numbers
  in scientific notation (`1.00108e+06`), which `sort -rn` reads as `1.00108`,
  silently ranking the correct window last. The script uses `printf "%d"`.
- A `mutter guard window` (e.g. 4480x1440) also passes naive size filters — it is
  a compositor overlay, exclude it.
- Window ids change every launch; never hardcode one across sessions.
- If `import -window <id>` fails with `Resource temporarily unavailable` (seen
  when grabbing the **root** window), fall back to cropping the window's absolute
  region out of the root window — the script does this automatically.

## Environment / applicability

Verified on: Ubuntu host with GNOME/mutter on X11 (`:0`),
`ghcr.io/marinholab/gazebo:jazzy` (gz-sim 8.11.0, `ROS_DISTRO=jazzy`).
Fragments that may differ elsewhere:

- Window title/class strings (Gazebo major version; mutter vs other WMs).
- `mutter-x11-frames` / `guard window` filtering is mutter-specific.
- On **Wayland** these X grabs won't work; use the compositor's own screenshot API.
- Package names assume Debian/Ubuntu `apt`.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `ERROR: no Gazebo GUI window found` | Check `gz sim` is running **without** `-s`, and that the container's `DISPLAY` matches the host display (`docker exec <c> printenv DISPLAY`) |
| Blank/black image | The GL context failed; check `docker logs <c>` for OGRE/EGL errors |
| `ffmpeg` never returns / mp4 grows forever | Missing `-t` before `-i` — `x11grab` is a live source with no EOF; see "give the recording a length" |
| `timeout: invalid option -- 'I'` | Use `timeout -s INT <secs>`, not `timeout -INT`; see "give the recording a length" |
| `height not divisible by 2` when recording | Odd window size; keep the `scale=trunc(iw/2)*2:trunc(ih/2)*2` filter |
| Video is a still image | World is static and/or sim is paused — see "Make sure something is actually moving" |
| `ImportImage ... Resource temporarily unavailable` | Root grab blocked; let the script fall back to a region crop |
| Permission denied talking to X | Re-run `xhost +local:root`; confirm `~/.Xauthority` is mounted |
| Container can't find the world file | Use the image path `/usr/share/gz/gz-sim8/worlds/`, or set `GZ_SIM_RESOURCE_PATH` |
| Two sims interfere | Stop one; they share `/world/...` and `/stats` topics |

## Bundled files

- `scripts/snapshot_gui.sh` — find the Gazebo GUI window and capture it to PNG
  (run on the host; retries while the window maps; excludes WM frame/utility/guard
  windows).
- `scripts/record_gui.sh` — record the same window to MP4 for a fixed number of
  seconds (run on the host; same window selection; refuses a non-finite duration
  so it can never record forever).
- `assets/snapshot_shapes_world.png`, `assets/snapshot_default_world.png` —
  reference captures of known-good output.
