#!/usr/bin/env python3
"""Upload a file to the Metalio reader's Wi-Fi transfer server (ws://<ip>/ws/upload).

usage: upload.py <ip> <remote_dir> <local_file> [--replace]
Protocol (firmware ap_transfer.cc): text {"op":"start",...} -> "ready", binary frames
of <= 4096 bytes, text {"op":"finish"} -> progress events until state done/failed.
"""
import json, sys, time, uuid
from pathlib import Path
import websocket

CHUNK = 4096


def wait_event(ws, want):
    while True:
        msg = ws.recv()
        if isinstance(msg, bytes):
            continue
        ev = json.loads(msg)
        if ev.get("event") == "error":
            sys.exit(f"device error: {ev.get('error')}")
        if want(ev):
            return ev


def main():
    args = [a for a in sys.argv[1:] if a != "--replace"]
    replace = "--replace" in sys.argv
    ip, remote_dir, local = args[0], args[1], Path(args[2])
    data = local.read_bytes()
    uid = "up-" + uuid.uuid4().hex[:12]
    ws = websocket.create_connection(f"ws://{ip}/ws/upload", timeout=60)
    ws.send(json.dumps({"op": "start", "id": uid, "path": remote_dir, "name": local.name,
                        "size": len(data), "replace": replace, "encoding": "identity"}))
    wait_event(ws, lambda e: e.get("event") == "ready")
    t0 = time.time()
    for off in range(0, len(data), CHUNK):
        ws.send_binary(data[off:off + CHUNK])
    ws.send(json.dumps({"op": "finish", "id": uid}))
    ev = wait_event(ws, lambda e: e.get("state") in ("done", "failed", "cancelled"))
    ws.close()
    dt = time.time() - t0
    if ev["state"] != "done":
        sys.exit(f"upload {ev['state']}: {ev.get('error')}")
    print(f"uploaded {local.name}: {ev['written']:,} B in {dt:.1f}s ({len(data) / dt / 1024:.0f} KB/s)")


if __name__ == "__main__":
    main()
