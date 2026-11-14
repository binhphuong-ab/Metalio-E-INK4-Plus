#!/usr/bin/env bash
# Metalio E-INK4 Plus — restore a full-flash backup made by flash.sh (macOS / Linux)
#
#   ./restore.sh backups/metalio-e-ink4-plus-<mac>-<date>.bin [PORT]
#
# Overwrites the WHOLE 16 MB flash with the backup: firmware, settings, fonts.
# Only restore a backup taken from the same reader.
set -euo pipefail
cd "$(dirname "$0")"

say()  { printf '\n==> %s\n' "$*"; }
fail() { printf '\n[X] %s\n' "$*" >&2; exit 1; }

IMG="${1:-}"
[ -n "$IMG" ] || IMG=$(ls backups/metalio-e-ink4-plus-*.bin 2>/dev/null | head -1 || true)
[ -n "$IMG" ] && [ -f "$IMG" ] || fail "Usage: ./restore.sh backups/<backup>.bin"
[ "$(wc -c < "$IMG" | tr -d ' ')" = "16777216" ] || fail "$IMG is not a 16 MB full-flash backup."

if command -v esptool >/dev/null 2>&1; then ESPTOOL=(esptool)
elif command -v esptool.py >/dev/null 2>&1; then ESPTOOL=(esptool.py)
else ESPTOOL=(python3 -m esptool); fi

PORT="${2:-$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1 || true)}"
[ -n "$PORT" ] || fail "No device port found. Plug in the cable and wake the reader."
wait_port() { for _ in $(seq 1 40); do [ -e "$PORT" ] && return 0; sleep 0.5; done; return 1; }
esp() {
  local attempt
  for attempt in 1 2 3 4; do
    if wait_port && "${ESPTOOL[@]}" --port "$PORT" "$@"; then return 0; fi
    echo "   ... not connected yet (attempt $attempt), retrying"; sleep 3
  done
  return 1
}

say "Checking the reader on $PORT"
INFO=$(esp chip-id 2>&1) || { echo "$INFO"; fail "Could not talk to the reader."; }
grep -q "ESP32-S31" <<<"$INFO" || fail "Not an ESP32-S31 device."
MAC=$(grep -oiE 'MAC: *([0-9a-f]{2}:){5}[0-9a-f]{2}' <<<"$INFO" | head -1 | awk '{print $2}' | tr -d ':')
case "$IMG" in
  *"$MAC"*) ;;
  *) echo "WARNING: backup file name does not contain this reader's MAC ($MAC)." ;;
esac

read -r -p "Overwrite the ENTIRE flash on $PORT with $IMG ? Type YES: " ans
[ "$ans" = "YES" ] || fail "Cancelled."

say "Writing backup (about 2-3 minutes)"
esp write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x0 "$IMG" || fail "Write failed; run ./restore.sh again."
say "Verifying"
esp verify-flash 0x0 "$IMG" >/dev/null || fail "Verification failed; run ./restore.sh again."
say "Restored. The reader restarts by itself."
