---
name: gazebo-headless-camera-test
description: This skill should be used when the user asks to "test Gazebo Harmonic --headless-rendering in a container", "verify camera image capture in headless Gazebo", "prove a Gazebo camera publishes gz.msgs.Image without a GPU/X server", or to "replicate the headless camera rendering test". It reproduces a verified end-to-end test that launches `gz sim --headless-rendering` inside the `ghcr.io/marinholab/gazebo:jazzy` container and confirms a camera sensor produces real (non-flat) image frames.
---

# Gazebo Harmonic Headless Camera Rendering Test

## Purpose

Reproduce a verified test that proves Gazebo **Harmonic** (`gz sim`, gz-sim 8.x) renders a
world and streams camera image frames using the official `--headless-rendering` flag,
**without a GPU and without an X server** (software OGRE2/EGL). The test is self-contained:
one small world file plus one stdlib-only Python helper. It was validated on
`ghcr.io/marinholab/gazebo:jazzy` (ROS 2 Jazzy + Gazebo Harmonic) on both arm64 and
would also run amd64 (the image is multi-arch).

### What the test proves
- `gz sim -s -r --headless-rendering <world>.sdf` boots with no crash and no render errors.
- A camera sensor publishes `gz.msgs.Image` on `/camera` (+ `/camera_info`).
- Frames are **real rendered geometry** (not a flat buffer): measured with a dynamic
  luminance range, a bright-vs-dark A/B comparison, and by writing the frame to a PNG.
- Sustained frame rate matches the sensor `update_rate` (e.g. 30 fps measured ~30.2 fps).

## Prerequisites

A Docker host (Docker Desktop or `dockerd` running) and the Gazebo Harmonic container.

```bash
# Base image (multi-arch: amd64 + arm64). Pulls natively to the host arch.
docker pull ghcr.io/marinholab/gazebo:jazzy
```

Inside the container the following are expected (all present in the image):
- `gz sim` version **8.11.0** (Gazebo Harmonic), `ROS_DISTRO=jazzy`
- OGRE2 render engine plugin: `libgz-rendering8-ogre2.so`
- Mesa EGL/GL libs: `libEGL.so`, `libGLESv2.so`
- **No `/dev/dri`** (no GPU) → EGL falls back to software (llvmpipe) rendering.
  This is fine for the test; it is CPU-bound, so keep resolution/rate modest.

Sanity-check the environment first:

```bash
docker run --rm ghcr.io/marinholab/gazebo:jazzy bash -c '
  uname -m;
  gz sim --version | head -1;
  echo "== headless flag =="; gz sim --help | grep -i headless-rendering;
  echo "== OGRE2 plugin =="; find / -name "libgz-rendering8-ogre2.so" 2>/dev/null | head -1;
  echo "== EGL =="; ls -1 /usr/lib/*/libEGL.so.1 2>/dev/null;
  echo "== GPU =="; ls -la /dev/dri 2>&1 | head;
'
```

## Replication steps

### 1. Create the world file

The world file is bundled at `assets/test_world.sdf` (identical to what is shown
below for reference). World = light + floor + target box + camera.
The key elements that make a camera actually publish in a world:
- a world-level **`Sensors` system plugin** (`filename="gz-sim-sensors-system"
  name="gz::sim::systems::Sensors"`) with `<render_engine>ogre2</render_engine>` —
  without this plugin a camera sensor does **not** publish;
- a `<sensor type="camera">` on a link with `<topic>/camera</topic>`, `<update_rate>`,
  `<image>` (width/height/format).

> Note: Gazebo's default camera looks along **+X** in its local frame (not -Z or -Y).
> Place the target in front of the camera along +X, e.g. camera at origin, box at
> `<pose>2.5 0 0.5 ...</pose>`.

Reference copy (identical to bundled `assets/test_world.sdf`):

```xml
<?xml version="1.0"?>
<!-- Final headless-rendering test world: light + floor + target box + camera.
     Demonstrates gz sim --headless-rendering producing real image frames. -->
<sdf version="1.11">
  <world name="final_test">
    <physics type="ode">
      <max_step_size>0.002</max_step_size>
      <real_time_factor>1.0</real_time_factor>
      <real_time_update_rate>500</real_time_update_rate>
    </physics>
    <plugin name="gz::sim::systems::Physics" filename="gz-sim-physics-system"/>
    <plugin name="gz::sim::systems::Sensors" filename="gz-sim-sensors-system">
      <render_engine>ogre2</render_engine>
    </plugin>
    <plugin name="gz::sim::systems::SceneBroadcaster" filename="gz-sim-scene-broadcaster-system"/>
    <plugin name="gz::sim::systems::UserCommands" filename="gz-sim-user-commands-system"/>

    <!-- LIGHT: directional sun + ambient scene light -->
    <light type="directional" name="sun">
      <cast_shadows>true</cast_shadows>
      <pose>2 2 8 0 0 0</pose>
      <diffuse>0.9 0.9 0.9 1</diffuse>
      <specular>0.3 0.3 0.3 1</specular>
      <direction>-0.35 -0.35 -0.87</direction>
      <intensity>1.0</intensity>
    </light>
    <scene>
      <ambient>0.6 0.6 0.6 1</ambient>
      <background>0.4 0.45 0.5 1</background>
      <grid>true</grid>
    </scene>

    <!-- FLOOR / ground plane -->
    <model name="floor">
      <static>true</static>
      <link name="link">
        <pose>0 0 0 0 0 0</pose>
        <collision name="col">
          <geometry><plane><normal>0 0 1</normal><size>20 20</size></plane></geometry>
        </collision>
        <visual name="vis">
          <geometry><plane><normal>0 0 1</normal><size>20 20</size></plane></geometry>
          <material>
            <ambient>0.5 0.5 0.55 1</ambient>
            <diffuse>0.5 0.5 0.55 1</diffuse>
            <specular>0.1 0.1 0.1 1</specular>
          </material>
        </visual>
      </link>
    </model>

    <!-- Target box sitting on the floor, in front of the camera (+X) -->
    <model name="target_box">
      <static>true</static>
      <pose>2.5 0 0.5 0 0.4 0</pose>
      <link name="link">
        <collision name="col">
          <geometry><box><size>1 1 1</size></box></geometry>
        </collision>
        <visual name="vis">
          <geometry><box><size>1 1 1</size></box></geometry>
          <material>
            <ambient>0.85 0.15 0.15 1</ambient>
            <diffuse>0.85 0.15 0.15 1</diffuse>
            <specular>0.2 0.2 0.2 1</specular>
          </material>
        </visual>
      </link>
    </model>

    <!-- CAMERA (default Gazebo camera looks along +X) -->
    <model name="camera">
      <static>true</static>
      <pose>0 0 1.2 0 0 0</pose>
      <link name="link">
        <sensor name="camera" type="camera">
          <pose>0 0 0 0 0 0</pose>
          <always_on>1</always_on>
          <update_rate>30</update_rate>
          <visualize>false</visualize>
          <topic>/camera</topic>
          <camera name="main">
            <horizontal_fov>1.047</horizontal_fov>
            <image>
              <width>1280</width>
              <height>720</height>
              <format>R8G8B8</format>
            </image>
            <clip>
              <near>0.1</near>
              <far>100</far>
            </clip>
          </camera>
        </sensor>
      </link>
    </model>
  </world>
</sdf>
```

### 2. Create the helper script

The helper decodes a single captured `gz topic -e` frame into a PNG and prints
diagnostics that prove the frame is a real rendered scene (dynamic luminance range,
a red-target pixel count, and a written PNG). It uses only the Python standard
library, so it runs inside the container with no `pip` install.

> Key gotcha: `gz topic -e` prints binary image `data` as **octal-escaped** text
> (e.g. `\313` = byte 0xD3). The helper must unescape octal to recover pixel bytes —
> a naive byte count on the raw text will under-count and produce an empty/flat image.

Reference copy (identical to bundled `assets/decode_png.py`; usage:
`python3 decode_png.py <gz_topic_output.txt> <out.png>`):

```python
import re, sys, zlib, struct


def unescape(txt):
    """Decode gz-topic octal-escaped binary (e.g. \\313 -> byte 0xD3)."""
    out = bytearray()
    i = 0
    while i < len(txt):
        c = txt[i]
        if c == "\\" and i + 1 < len(txt) and txt[i + 1] in "01234567":
            j = i + 1
            o = ""
            while j < len(txt) and len(o) < 3 and txt[j] in "01234567":
                o += txt[j]
                j += 1
            out.append(int(o, 8))
            i = j
            continue
        out.append(ord(c))
        i += 1
    return bytes(out)


def write_png(path, w, h, rgb):
    """Write an 8-bit RGB PNG (no external deps)."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter type 0
        raw.extend(rgb[y * w * 3:(y + 1) * w * 3])

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


raw = open(sys.argv[1], "rb").read().decode("latin-1")
out_png = sys.argv[2]
m = re.search(r"width: (\d+)\nheight: (\d+)\nstep: (\d+)", raw)
w, h, step = int(m.group(1)), int(m.group(2)), int(m.group(3))
dm = re.search(r"data: \"(.*?)\"\n\n", raw, re.S) or re.search(r"data: \"(.*?)\"", raw, re.S)
data = unescape(dm.group(1))
print(f"frame {w}x{h} step={step} decoded_len={len(data)} expected={w * h * 3}")

px = w * h
minv, maxv = 255, 0
red = 0
rows = {}
for y in range(h):
    for x in range(w):
        i = (y * w + x) * 3
        r, g, b = data[i], data[i + 1], data[i + 2]
        lum = (r + g + b) // 3
        minv = min(minv, lum)
        maxv = max(maxv, lum)
        if r > 120 and g < 90 and b < 90:
            red += 1
            rows[y] = rows.get(y, 0) + 1
print(f"luminance min={minv} max={maxv} (dynamic range => real scene)")
print(f"red px={red} ({100 * red / px:.1f}%)")
nr = [y for y, v in rows.items() if v > 0]
if nr:
    print(f"red box rows {min(nr)}..{max(nr)} of {h} (center {h // 2}) => target in view")
write_png(out_png, w, h, data)
print(f"wrote PNG -> {out_png}")
```

### 3. Run the sim and capture a frame (single container so transport shares a network)

Mount a temp dir with the world + helper into the container. The world and helper
are bundled in this skill's `assets/` directory, so copy those — the skill runs
without depending on any file outside the repository. Start the sim in the
background, wait for the render engine + sensor to spin up, capture exactly one
message, then kill the sim. (`SKILL_DIR` is the directory containing this
`SKILL.md`.)

```bash
SKILL_DIR=<path to this skill dir>   # .../gazebo-headless-camera-test
WORK=/tmp/gazebo_cam_test
mkdir -p "$WORK"
cp "$SKILL_DIR/assets/test_world.sdf" "$SKILL_DIR/assets/decode_png.py" "$WORK/"

docker run --rm -v "$WORK":/tmp/wt ghcr.io/marinholab/gazebo:jazzy bash -c '
  gz sim -s -r --headless-rendering /tmp/wt/test_world.sdf > /tmp/wt/sim.log 2>&1 &
  P=$!
  sleep 15                       # allow software EGL + sensor to initialize
  echo "== topics ==";  gz topic -l | grep -i cam
  echo "== one frame =="; gz topic -e -n 1 -t /camera > /tmp/wt/frame.txt 2>/dev/null
  python3 /tmp/wt/decode_png.py /tmp/wt/frame.txt /tmp/wt/frame.png
  echo "== render errors: $(grep -ciE "error|fail|crash|abort|unable to create|EGL init" /tmp/wt/sim.log) =="
  kill $P 2>/dev/null; wait $P 2>/dev/null
'
# copy the PNG out to view:  cp "$WORK/frame.png" ./headless_frame.png
```

### 4. (Optional) Sustained frame-rate measurement

To confirm the rate, capture for ~12 s and count frames:

```bash
docker run --rm -v "$WORK":/tmp/wt ghcr.io/marinholab/gazebo:jazzy bash -c '
  gz sim -s -r --headless-rendering /tmp/wt/test_world.sdf > /tmp/wt/sim.log 2>&1 &
  P=$!; sleep 15
  timeout 12 gz topic -e -t /camera > /tmp/wt/many.txt 2>/dev/null
  kill $P 2>/dev/null; wait $P 2>/dev/null
'
```
Count the `stamp {` occurrences in `many.txt`; divided by ~11.7 s should be ≈ the
`update_rate` (30).

### 5. (Optional) Decisive A/B proof of real rendering

Render two otherwise-identical worlds differing only in scene `<background>` /
`<ambient>` (bright `0.95` vs dark `0.05`). If `--headless-rendering` truly captures
the scene, the decoded luminance range of the bright world must be far larger than
the dark one (e.g. bright ≈ 49–249 with the red box visible at ~14% red pixels;
dark ≈ a narrow low range). A flat/constant output in both would indicate a
broken render path.

```bash
for bg in 0.05 0.95; do
  # generate a world with <scene><ambient>$bg $bg $bg 1</ambient>
  #                       <background>$bg $bg $bg 1</background></scene>
  gz sim -s -r --headless-rendering "w_$bg.sdf" & P=$!
  sleep 12
  gz topic -e -n 1 -t /camera > "msg_$bg.txt" 2>/dev/null
  python3 decode_png.py "msg_$bg.txt" "png_$bg.png"   # compare luminance min/max
  kill $P 2>/dev/null; wait $P 2>/dev/null
done
```

## Expected results (validated baseline)

- `gz topic -l | grep cam` → `/camera`, `/camera_info`
- `gz topic -i -t /camera` → `gz.msgs.Image`
- `decode_png.py` on a 1280×720 frame:
  - `decoded_len == 2764800 == 1280*720*3`
  - `luminance min≈70 max≈179` (real depth; a flat buffer would be one value)
  - a PNG is written and shows background + floor horizon + the target box
- Sustained rate ≈ `update_rate` fps with zero render errors in `sim.log`

## Interpretation

- **Flat/constant luminance, zero variation** → camera not seeing the scene
  (wrong camera aim, target behind the camera, or `<Sensors>` plugin missing).
- **No `/camera` topic** → missing `Sensors` system plugin, or sensor not
  `always_on`, or sim not started (needs `-r`/play).
- **Slow FPS / low RTF** → software rendering is CPU-bound; lower the camera
  resolution or `update_rate`, or provide a GPU (EGL uses it directly).
- **`EGL init` / render errors** → OGRE2 render engine or EGL libs unavailable;
  confirm `libgz-rendering8-ogre2.so` and `libEGL.so.1` are present.

## Adopting this for camera/video work

To turn this verification into a real image/video pipeline on top of a Gazebo
Harmonic world (e.g. inside a container built from this image):
1. Add the `<Sensors>` system plugin to the world SDF (a world without it will load
   camera sensors but they will not publish).
2. Add a camera sensor on a link (robot-mounted or a fixed scene camera).
3. Launch with `gz sim ... --headless-rendering` — no X server or Xvfb needed.
4. Expose frames to ROS 2 with `ros_gz_bridge`:
   `/camera@sensor_msgs/msg/Image@gz.msgs.Image`
   (and `/camera_info@sensor_msgs/msg/CameraInfo@gz.msgs.CameraInfo`).
5. For a recorded video file headless, there is no native headless video encoder;
   pipe the bridged `sensor_msgs/Image` stream into `ffmpeg` (or a small node). The
   built-in **Video Recorder** is a GUI plugin only (needs the GUI, e.g. under Xvfb).

## References (official Gazebo Harmonic)

- Headless rendering (EGL/OGRE2): `https://gazebosim.org/api/sim/8/headless_rendering.html`
  and the `headless_rendering.md` tutorial in `gazebosim/gz-sim` (gz-sim8).
- Sensors / camera SDF: `https://gazebosim.org/docs/harmonic/sensors`
- Canonical camera example world: `gazebosim/gz-sim` `examples/worlds/camera_sensor.sdf` (gz-sim8).
- `ros_gz_bridge` image pairing: `https://github.com/gazebosim/ros_gz/blob/ros2/ros_gz_bridge/README.md`

## Bundled files

The skill is self-contained and does not depend on any file outside the
repository. Everything needed to run the test lives in this skill directory:

- **`assets/test_world.sdf`** — the test world (light + floor + target box + camera)
  with the `Sensors` (ogre2) system plugin.
- **`assets/decode_png.py`** — stdlib-only helper that decodes a `gz topic -e`
  frame into a PNG and prints rendering diagnostics.

Both are also reproduced inline in the replication steps above for reference.
