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
     --sub /world/empty/stats:gz.msgs.WorldStatistics
```

## Known limitations

- **Discovery is manual.** Because gz discovery is bypassed, `--sub` is a manifest of
  what to forward. Topic *names* are discoverable (`gz topic -l` works from a host
  shell with `GZ_IP=127.0.0.1`); topic *types* are not, hence the `topic:type` syntax.
  A dynamic `sub` frame from the container can also drive the mirror.
- `sink.py` is the **test harness / demo**, not production code. It has scripted
  pause/unpause checks against `/world/empty/*` baked in.
- Measured ~1,000 msg/s sustained mirroring of `/world/empty/clock` (33–34 B msgs).
  Separately, raw TCP over a published port measured p50 160 µs / p99 426 µs at
  ~6,240 round trips/s, so one connection comfortably carries a 500 Hz control loop.
  The 1,000 msg/s is the PoC's ceiling, not the transport's.
- Verified against gz-transport **13** (gz-sim8/Harmonic on macOS). Untested against
  14/15.

## Licence

LGPL-2.1+, matching this repository. Note that gz-transport itself is Apache-2.0, so
linking it imposes no copyleft obligation on `gzif`; the LGPL here is a choice to
match the rest of MarinhoLab.
