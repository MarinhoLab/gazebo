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
`zenohd` router on the same one-port, host-is-the-client topology, roughly 4× this
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
- **Topic types are discoverable, and introspection needs the publisher to be
  reachable.** `gz topic -i -t <topic>` prints the message type and
  `gz service --info --service <svc>` prints request/response types — verified on
  gz-transport 13 (macOS host, jazzy image) and 15 (lyrical image), and inside the
  frame header via `MessageInfo::Type()`. An earlier note here claimed types were not
  discoverable; that was a bad observation, and it is why the `topic:type` syntax
  exists at all. Caveat: introspection is itself a data connection to the publisher's
  *advertised* address, so it works within one network namespace but fails across the
  container boundary exactly like message delivery does (negative result 1).
- `gz sim -v1` prints `Error` lines from `sensors_system.cc` / `heightmap_sdf_utils.cc`
  on any world. Noise, not failure.
- `timeout` is not available in Ubuntu 26.04 based images.

## Fixed: `--sub` types are now a fallback, not a requirement

An earlier version labelled every forwarded message with the type declared on the
command line, so a bare `--sub /world/empty/stats` arrived tagged `gz.msgs.String`.
`MessageInfo::Type()` now supplies the real type per message, with the declared type
kept only as a fallback for frames that carry none. Verified on the wire: `--sub
/world/empty/stats` alone emits `type=gz.msgs.WorldStatistics`, `--sub
/world/empty/clock` emits `type=gz.msgs.Clock`.

Re-measured afterwards rather than trusting the change to be free: ~3,000 msg/s before
and after on the same build of gz-transport, so the extra string copy per message costs
nothing measurable. Note the earlier "~1,000 msg/s" figure quoted alongside this wart
was the `sink.py` harness byte-at-a-time header reads, not `gzif`; the bridge was always
faster than its demo consumer.

## Licensing note

LGPL-2.1+, matching this repository. gz-transport itself is **Apache-2.0**, not GPL —
linking it imposes no copyleft obligation on `gzif`.

## How this differs from RoboStar Gazebo Bridge

**They relay gz's wire protocol; `gzif` terminates it and re-expresses it.** The relay
passes gz's own bytes through — the UDP-multicast discovery frames, then the
dynamically negotiated data endpoints it reads out of the discovery protobuf — so gz
discovery completes end to end and neither process knows a boundary exists. `gzif`
breaks the connection at both ends, speaks a new JSON/length-prefixed protocol across
it, and substitutes a manual `--sub` manifest for discovery.

The difference is probably structural: the host half of the bridge is a VS Code
extension, i.e. Node.js, which cannot link `libgz-transport` — so moving gz traffic
means reimplementing its protocol. `gzif` is a C++ binary that links the real library,
gets gz's wire behaviour for free, and only has to move payloads. That single choice
explains most of the table below.

| | RoboStar Gazebo Bridge | `gzif` |
|---|---|---|
| Mechanism | protocol relay, protocol-aware at **discovery** | API gateway, zero **payload** deserialisation |
| gz discovery | runs end to end | bypassed, manual `--sub` |
| Services / `gz service -l` | should work unchanged | needs the `call`/`reply` path, types declared per topic |
| Published ports | **none** — control channel self-forwards over VS Code's existing channel | one, `-p 9100:9100` |
| Where `gz sim` runs | `gz` PATH shim runs it natively on the host | you start it yourself on the Mac |
| Worlds / meshes | synced into a host cache | your problem |
| Plugins | built natively, or fetched prebuilt via `package.xml` | **not addressed** |
| Runs under | VS Code remote host alive | any process; headless, CI |
| Coupled to | gz **wire format** — breaks silently on a format change | gz **API/ABI** — breaks loudly, rebuild per major |
| gz backend | ZeroMQ-specific (multicast + negotiated TCP); nothing to relay under Zenoh | backend-agnostic via the `Node` API |
| Licence | Apache-2.0 | LGPL-2.1 |

Two rows matter more than the rest for `sas_robot_driver_gazebo`:

- **Plugins.** A native macOS `gz sim` cannot load a container-built Linux `.so`, so a
  plugin like `AbsolutePosePublisher` silently publishes nothing under `gzif` until
  someone builds it for macOS. The bridge has a *Build Native Plugins* command for
  exactly this. `gzif` has no answer.
- **Resource paths.** Their resource sync covers the `Error Code 14` failure we hit with
  `ur3e_world.sdf` and missing UR meshes. With `gzif` that is manual `GZ_SIM_RESOURCE_PATH`
  wrangling.

And one row favours `gzif`: the relay's design assumes gz-transport's ZeroMQ
architecture, so gz-transport15 with a Zenoh backend would leave it nothing to forward,
whereas you would just run a `zenohd` router — which is the direction the stack is
moving. See negative result 3 above, and **Zenoh** in the list below.

Everything in the table is from the two Marketplace pages
([host](https://marketplace.visualstudio.com/items?itemName=RoboStar.gz-bridge-host),
[remote](https://marketplace.visualstudio.com/items?itemName=RoboStar.gz-bridge-remote));
the *why* behind the protocol-level choice and the claim that the relay wouldn't apply
to a Zenoh backend are **inference, not documentation**. Not yet verified hands-on for a
UR3e world, and status is pre-alpha with ~15–20 installs.

## Alternatives worth knowing before extending this

- **Zenoh** (gz-transport15 + `zenohd`) — best long-term fit, needs the source build
  described above. One port, TCP-first, ~4× throughput.
- **DDS unicast peers / Fast DDS discovery server** — the well-trodden fix when what
  must cross the boundary is ROS 2 ↔ ROS 2. Does nothing for gz-transport, which is a
  separate stack.
- **Tailscale / Husarnet + unicast peer lists** — the only way to make *native* gz
  discovery work across the boundary, since it gives both sides a routable address.
  WireGuard meshes route unicast only, so multicast discovery must still be replaced
  with an explicit peer list (`GZ_IP` = the 100.x address, `GZ_RELAY` at the peer).
- **A VM with bridged networking** (colima/UTM) — cheapest way to sidestep the entire
  problem, at the cost of native rendering.
- **RoboStar Gazebo Bridge** — a VS Code extension pair that solves the same problem by
  relaying gz's wire protocol instead of terminating it, and handles resource sync and
  native plugin builds. See "How this differs from RoboStar Gazebo Bridge" above; try it
  before extending `gzif`.
