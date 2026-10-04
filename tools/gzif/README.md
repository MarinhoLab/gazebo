# `gzif` — gz-transport across the Docker Desktop boundary

**Status: draft / proof of concept.** It works, it is ~170 lines, and it has solved a
real problem, but it is not hardened and it requires manual topic configuration.

## The problem

On macOS, `gz transport` cannot bridge a Linux container and a native (macOS)
`gz sim`. gz-transport's ZeroMQ backend does discovery over UDP multicast and then
opens a TCP connection between every peer pair, dialling the address each peer
*advertised over the wire*. Under Docker Desktop that advertised address is the
container's internal `172.17.x.x`, unreachable from the host, and there is no fixed
port to publish — it is one ephemeral port per peer pair, chosen at runtime. No
`-p` mapping can help.

## The trick

Do not fix discovery. Collapse the whole interface to **one TCP connection that the
macOS side always initiates**, and put the complexity into the wire format instead of
the transport handshake.

```
macOS host                               Docker (linux/arm64)
  gz sim (native, gz-transport13)           sink.py  listens 0.0.0.0:9100
      ^                                       ^
      | in-process gz-transport         -p 9100:9100
      |                                       |
    gzif  --------- one TCP client conn ------+
```

`gzif` runs on the **macOS host**, next to `gz sim`, and talks to it in-process over
normal gz-transport — no Docker involved on that hop. The **role inversion** is the
key idea: the host runs the TCP *client*, the container runs the *listener*, because a
host-initiated connection to a published port is the one direction that crosses the
Docker Desktop boundary reliably, and TCP is full-duplex, so that single socket carries
both directions.

## Wire format

One JSON header line, a 4-byte big-endian payload length, then the payload bytes.
Payloads are raw gz protobuf, base64-encoded inside the JSON when needed.

| `op` | Meaning |
|---|---|
| `sub` | container -> gzif: start forwarding this topic |
| `pub` | either direction: raw message bytes for a topic |
| `call` | container -> gzif: service call, answered by `reply` with `ok` |

gz-transport puts the message type in the frame header, so `gzif` **never
deserialises anything** — everything is `SubscribeRaw` / `PublishRaw` / `RequestRaw`
with a type string known only at runtime. That is why no `gz-msgs` types are linked in
and the binary stays small.

## Build and use

```console
brew install gz-transport13 pkg-config
make                     # or: make PKG=gz-transport15

# container side: publishes 9100 and runs sink.py as its listener
gzif --connect 127.0.0.1:9100 \
     --sub /world/empty/clock \
     --sub /world/empty/stats
     # --sub /topic:gz.msgs.Type is also accepted; the type is only a fallback
     # label, since MessageInfo::Type() supplies the real one per message.
```

## Known limitations

- **Topic selection is manual.** Because gz discovery is bypassed across the boundary,
  `--sub` is a list of what to forward. Types are **not** needed: gz-transport puts the
  message type in every frame header, so `MessageInfo::Type()` labels each forwarded
  message with its real type (verified — `--sub /world/empty/stats` alone yields
  `gz.msgs.WorldStatistics` on the wire). `topic:gz.msgs.Type` remains accepted, but
  only as a fallback label for messages that arrive without one. A dynamic `sub` frame
  from the container can also drive the mirror.
- `sink.py` is the **test harness / demo**, not production code. It has scripted
  pause/unpause checks against `/world/empty/*` baked in.
- `AGENTS.md` in this directory records the negative results and the probes that prove
  them — read it before spending time on a dead end.
- Sustained mirroring of `/world/empty/clock` measured **~3,000 msg/s** over loopback
  (33–35 B msgs) — the publisher's own rate, so the mirror is not the limiting hop. An
  earlier "~1,000 msg/s" figure here was the Python `sink.py` harness bottlenecking on
  byte-at-a-time header reads, not the bridge. Raw TCP over a published port measured
  p50 160 µs / p99 426 µs at ~6,240 round trips/s, so one connection comfortably carries
  a 500 Hz control loop.
- Verified against gz-transport **13** (gz-sim8/Harmonic on macOS). Untested against
  14/15.

## Prior art: a better established tool for the same problem

While developing this we found **RoboStar Gazebo Bridge**, a VS Code extension pair
that solves exactly this problem and is strictly more complete than `gzif`:

- [Gazebo Bridge (Host)](https://marketplace.visualstudio.com/items?itemName=RoboStar.gz-bridge-host)
  — runs on the local machine (this side of the boundary)
- [Gazebo Bridge (Remote)](https://marketplace.visualstudio.com/items?itemName=RoboStar.gz-bridge-remote)
  — runs inside the Dev Container / WSL / Codespace

It relays gz's UDP-multicast discovery **and** the dynamically negotiated TCP data
connections across the container boundary — i.e. it solves the general discovery
problem that `gzif` sidesteps with a manual `--sub` manifest — and additionally installs
a `gz` PATH shim so `gz sim ...` launched inside the container executes natively on the
host. Same core topology as `gzif`: the host side initiates, with a control channel that
forwards itself on demand, so there is no fixed port to declare.

Honest caveats, so "established" is not overread: it is self-described **early /
pre-alpha**, and adoption is small (Marketplace lists ~15–20 installs at the time of
writing). What is established is the *mechanism* — the core relay is proven on a real
cross-machine deployment, and as a piece of engineering it is more complete than
anything in this directory. **Try it before extending `gzif`.** If the VS Code
dependency is a problem, the design notes in its README are worth reading regardless.

For the wider survey of alternatives (Zenoh as the gz-transport backend, Tailscale /
Husarnet overlays with unicast peer lists, DDS discovery servers, a bridged-network VM),
see `AGENTS.md`. Zenoh is the architecturally right answer here — TCP-first, one
published port — but on current packaging it needs a gz-transport source build.

## Licence

LGPL-2.1+, matching this repository. Note that gz-transport itself is Apache-2.0, so
linking it imposes no copyleft obligation on `gzif`; the LGPL here is a choice to
match the rest of MarinhoLab.
