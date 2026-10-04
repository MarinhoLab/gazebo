# AGENTS.md — `tools/gzif`

Context for AI agents working on this tool. Everything below was **measured on a real
macOS + Docker Desktop machine**, not read from docs: the problem, the dead ends, and
the probes that prove each dead end. If you are about to spend an hour, check here
first.

## The problem in one paragraph

gz-transport's ZeroMQ backend cannot cross the Docker Desktop boundary. Discovery uses
UDP multicast (blocked), **and** — the part that wastes the most time — data
connections dial whichever address the peer advertised on the wire. Under Docker
Desktop that is the container-internal `172.17.x.x`, with **one ephemeral port per peer
pair**, so there is no fixed port to publish and no `-p` mapping that helps. `gzif`
sidesteps all of it by keeping the host as the permanent TCP client and collapsing the
interface onto one connection. See `README.md`.

## Negative results — do not re-derive these

### 1. Native gz-transport across the boundary: cannot work.

Probes that prove it, so the failure is recognisable rather than looking like a config
error:

- From a host shell with `GZ_IP=<container internal IP>`: `gz topic -l` **lists the
  topics**, `gz topic -i` **shows a publisher** — then `gz topic -e` returns
  `timeout while waiting for messages`. Discovery succeeds, delivery fails.
- `gz sim -l` from the host: `Unable to connect to the server`.

Both look like "almost working, one setting away". They are not.

### 2. `GZ_IP` must be set on macOS or gz silently delivers nothing.

The trap that cost the most time, and it bites **on a single machine**, not across
Docker: `gz topic -l` lists the topic, the subscriber connects, and **zero messages
arrive**. Fix: `GZ_IP=127.0.0.1` on *every* gz process. gz-transport picks a
non-loopback interface for its data channel and macOS does not loop it back the way
Linux does.

### 3. Zenoh as the gz-transport backend is a source build, not a switch.

Zenoh is TCP-first with *optional* multicast scouting, so it is the architecturally
correct answer for this boundary — measured 12,165 msg/s at 82 µs round trip through a
`zenohd` router on the same one-port, host-is-the-client topology, roughly 14× this
tool's throughput. But on current packaging it is not obtainable:

| Item | Result |
|---|---|
| gz-transport13 (Harmonic) | no Zenoh backend — 0 hits in headers and dylib |
| gz-transport14 (Ionic) | no Zenoh — 0 hits in CMakeLists |
| gz-transport15 (Jetty) | Zenoh **present** at source level (`GZ_TRANSPORT_ENABLE_ZENOH`) |
| Homebrew `gz-transport15` bottle | 14 deps, **no `zenohc`/`zenohcxx`** → `HAVE_ZENOH=OFF` |
| `gz-transport` inside ROS 2 Lyrical images | `nm -D` zenoh symbols: **0** |
| `GZ_TRANSPORT_IMPLEMENTATION=zenoh` there | inert; `LD_DEBUG=libs` never loads `libzenohc` |

Root cause in the ROS vendor packaging: `gz-transport/CMakeLists.txt` builds the
fallback path from `$ENV{ROS_DISTRO}` at **configure** time. A colcon vendor build has
no `ROS_DISTRO` set, the path resolves to `/opt/ros//opt/...`, the `EXISTS` check fails,
and `HAVE_ZENOH` is forced `OFF` — with only a `STATUS` message, so the build looks
clean. The env var string is still present in the binary, which is what makes it
misleading.

**Do not** use "`gz topic -l` returns 0 topics" as evidence Zenoh is broken: that also
happens whenever multicast scouting is off and no router is reachable. `nm -D` is the
decisive probe.

The backend is also a **compile-time** choice: one gz-transport build is ZeroMQ *or*
Zenoh, never both. Every component on both sides of a boundary must agree.

### 4. There is no `zenoh` Homebrew formula.

`brew install zenoh` fails ("did you mean zenith?"), and `osrf/simulation` has no
`zenoh-c`/`zenoh-cpp` formula. `brew install gz-sim10` installs Jetty but still drags
in the Zenoh-less `gz-transport15`. Get Zenoh from the standalone release assets
(`eclipse-zenoh/zenoh` → `zenohd`; `eclipse-zenoh/zenoh-c` → `libzenohc.dylib` +
`zenohcConfig.cmake`), or the `eclipse-zenoh` pip package for prototyping.

### 5. `pause: false` does not resume the clock in the gz8/macOS build.

`pause: true` works and is observable; the follow-up unpause returns `ok=true` but the
clock stays stopped. This reproduces **natively via the `gz service` CLI**, so it is
not a bridge defect. Launch `gz sim -s -r` (start unpaused) rather than driving
pause/unpause over the wire.

## Gotchas that are not failures, just confusing

- `GZ_PARTITION` defaults to `hostname:username` → host and container land in
  **different partitions** and see nothing of each other even on a working network.
- `gz sim -s` starts **paused**. A `/control` call that appears to do nothing may have
  succeeded — read `paused` from `/world/*/stats` before concluding otherwise.
- `gz service` on `/world/*/control` needs `--reptype gz.msgs.Boolean`. With the wrong
  type it times out, which looks exactly like a network failure.
- **Topic names are discoverable; topic types are not**, for a non-`gz` process.
  `gz topic -l` works (with `GZ_IP` set); `gz topic -i -t <topic>` does not. Hence
  `gzif`'s explicit `topic:type` manifest.
- `gz sim -v1` prints `Error` lines from `sensors_system.cc` / `heightmap_sdf_utils.cc`
  on any world. Noise, not failure.
- `timeout` is not available in Ubuntu 26.04 based images.

## Known wart in `gzif` itself

For a typeless `--sub /topic`, the CLI path labels forwarded messages with the declared
default type instead of each message's real type, because the CLI cannot express a
per-topic type. `MessageInfo::Type()` is available in the callback, so this is a
one-line fix — deliberately left alone so the published numbers describe the code as
shipped. Fix it and re-measure, rather than fixing it silently.

## Licensing note

LGPL-2.1+, matching this repository. gz-transport itself is **Apache-2.0**, not GPL —
linking it imposes no copyleft obligation on `gzif`.

## Alternatives worth knowing before extending this

- **Zenoh** (gz-transport15 + `zenohd`) — best long-term fit, needs the source build
  described above. One port, TCP-first, ~14× throughput.
- **DDS unicast peers / Fast DDS discovery server** — the well-trodden fix when what
  must cross the boundary is ROS 2 ↔ ROS 2. Does nothing for gz-transport, which is a
  separate stack.
- **Tailscale / Husarnet + unicast peer lists** — the only way to make *native* gz
  discovery work across the boundary, since it gives both sides a routable address.
  WireGuard meshes route unicast only, so multicast discovery must still be replaced
  with an explicit peer list (`GZ_IP` = the 100.x address, `GZ_RELAY` at the peer).
- **A VM with bridged networking** (colima/UTM) — cheapest way to sidestep the entire
  problem, at the cost of native rendering.
- **RoboStar Gazebo Bridge** — a maintained VS Code extension pair that does this job
  and more. See "Prior art" in `README.md`; try it before extending `gzif`.
