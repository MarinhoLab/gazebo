#!/usr/bin/env python3
# Copyright (c) 2026 Murilo Marques Marinho
#
#    This file is part of gazebo (https://github.com/MarinhoLab/gazebo).
#
#    gazebo (https://github.com/MarinhoLab/gazebo) is free software: you can redistribute it and/or modify
#    it under the terms of the GNU Lesser General Public License as published by
#    the Free Software Foundation, either version 2.1 of the License, or
#    (at your option) any later version.
#
#    gazebo (https://github.com/MarinhoLab/gazebo) is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU Lesser General Public License for more details.
#
#    You should have received a copy of the GNU Lesser General Public License
#    along with gazebo (https://github.com/MarinhoLab/gazebo).  If not, see <https://www.gnu.org/licenses/>.

"""Container side of the gzif interface.

Listens on ONE fixed port; the Mac connects IN, because a Mac-initiated
connection to a published port is the only direction that reliably crosses the
Docker Desktop boundary. One connection then carries both directions.

Frames: JSON header line, 4-byte big-endian payload length, payload bytes.
Payloads are raw gz protobuf, so a base64 "data" field carries gz->TCP.
"""
import base64, json, socket, struct, sys, threading, time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9100
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("0.0.0.0", PORT))
srv.listen(4)
print(f"[sink] listening :{PORT}", flush=True)

lock = threading.Lock()
conn = None
stats = []


def frame(obj, payload=b""):
    with lock:
        if conn is None:
            return
        conn.sendall((json.dumps(obj, separators=(",", ":")) + "\n").encode()
                     + struct.pack(">I", len(payload)) + payload)


def recv_exact(c, n):
    b = b""
    while len(b) < n:
        d = c.recv(n - len(b))
        if not d:
            raise ConnectionError
        b += d
    return b


def handle(c):
    global conn
    conn = c
    print("[sink] gzif connected", flush=True)
    n, t0, last = 0, time.time(), time.time()
    while True:
        hdr = b""
        while not hdr.endswith(b"\n"):
            d = c.recv(1)
            if not d:
                return
            hdr += d
        h = json.loads(hdr.decode().strip())
        (ln,) = struct.unpack(">I", recv_exact(c, 4))
        payload = recv_exact(c, ln)
        if h.get("op") != "pub":
            print(f"[sink] REPLY topic={h.get('topic')} ok={h.get('ok')} "
                  f"payload={payload!r}", flush=True)
            continue
        n += 1
        if h["topic"].endswith("/stats"):
            stats.append(payload)
        now = time.time()
        if now - last > 1.0:
            print(f"[sink] {h['topic']:<26} {h.get('type',''):<24} "
                  f"{n/(now-t0):7.1f} msg/s  {ln}B", flush=True)
            last = now


def paused_now():
    """WorldStatistics.paused is field 5 (bool) -> tag byte 0x28."""
    for p in reversed(stats):
        if b"\x28\x01" in p:
            return True
        if b"\x28\x00" in p:
            return False
    return None


def driver():
    time.sleep(5)
    print("\n[sink] mirroring /world/empty/stats back over the boundary", flush=True)
    frame({"op": "sub", "topic": "/world/empty/stats",
           "type": "gz.msgs.WorldStatistics"})
    time.sleep(2)
    print(f"[sink] baseline                 : paused={paused_now()}", flush=True)

    print("\n[sink] gz SERVICE CALL from container -> native gz...", flush=True)
    frame({"op": "call", "topic": "/world/empty/control",
           "type": "gz.msgs.WorldControl", "reptype": "gz.msgs.Boolean",
           "data": base64.b64encode(b"\x10\x01").decode()})
    time.sleep(2)
    print(f"[sink] CHECK after pause        : paused={paused_now()}  expect True",
          flush=True)

    frame({"op": "call", "topic": "/world/empty/control",
           "type": "gz.msgs.WorldControl", "reptype": "gz.msgs.Boolean",
           "data": base64.b64encode(b"\x10\x00").decode()})
    time.sleep(2)
    print(f"[sink] CHECK after unpause      : paused={paused_now()}  expect False",
          flush=True)
    print("\n[sink] DONE", flush=True)


first = True
while True:
    c, a = srv.accept()
    print(f"[sink] connection from {a}", flush=True)
    threading.Thread(target=handle, args=(c,), daemon=True).start()
    if first:
        first = False
        threading.Thread(target=driver, daemon=True).start()
