#!/usr/bin/env bash
# Metalio E-INK4 Plus — offline English firmware 2.0.79-en.2 installer (macOS / Linux)
#
#   ./flash.sh              check device, back up the whole flash, then install
#   ./flash.sh --dry-run    run every check and the backup, but write nothing
#   ./flash.sh PORT         use a specific serial port (e.g. /dev/cu.usbmodem2301)
#
# What it writes:  app -> ota_0 (0x80000), UI fontpack -> font_data (0xae4000).
# What it keeps:   bootloader, partition table, NVS (settings, Wi-Fi), resources.
# Undo:            ./restore.sh backups/<your backup>.bin
#
# Requires esptool >= 5.0 (ESP32-S31 support):  pip install --upgrade esptool
set -euo pipefail
cd "$(dirname "$0")"

VERSION="2.0.79-en.2"
APP="metalio-e-ink4-plus-${VERSION}-app.bin"
FONT="metalio-e-ink4-plus-${VERSION}-fontpack.bin"
DRY_RUN=0
PORT=""
for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY_RUN=1 ;;
    -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
    *) PORT="$arg" ;;
  esac
done

say()  { printf '\n==> %s\n' "$*"; }
fail() { printf '\n[X] %s\n' "$*" >&2; exit 1; }

# ---- esptool ----------------------------------------------------------------
if command -v esptool >/dev/null 2>&1; then ESPTOOL=(esptool)
elif command -v esptool.py >/dev/null 2>&1; then ESPTOOL=(esptool.py)
elif python3 -c 'import esptool' >/dev/null 2>&1; then ESPTOOL=(python3 -m esptool)
else fail "esptool not found. Install it:  pip install --upgrade esptool"; fi
ESPTOOL_VER=$("${ESPTOOL[@]}" version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+' | head -1 || true)
[ "${ESPTOOL_VER%%.*}" -ge 5 ] 2>/dev/null || fail "esptool ${ESPTOOL_VER:-?} is too old; ESP32-S31 needs >= 5.0:  pip install --upgrade esptool"

# ---- files ------------------------------------------------------------------
say "Checking release files"
if command -v shasum >/dev/null 2>&1; then shasum -a 256 -c SHA256SUMS
else sha256sum -c SHA256SUMS; fi

# ---- port -------------------------------------------------------------------
if [ -z "$PORT" ]; then
  PORT=$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1 || true)
fi
[ -n "$PORT" ] || fail "No device port found. Plug in the USB-C cable, press the power button to wake the reader, and close any web flasher tab in your browser."
echo "Port: $PORT"

wait_port() { for _ in $(seq 1 40); do [ -e "$PORT" ] && return 0; sleep 0.5; done; return 1; }

# Each esptool run resets the chip and the USB port re-enumerates; a just-woken
# reader also re-enumerates once while booting. Wait for the port and retry.
esp() {
  local attempt
  for attempt in 1 2 3 4; do
    if wait_port && "${ESPTOOL[@]}" --port "$PORT" "$@"; then return 0; fi
    echo "   ... not connected yet (attempt $attempt), retrying"; sleep 3
  done
  return 1
}

# ---- device checks (one esptool session) ------------------------------------
say "Reading device information"
HEAD=$(mktemp)
INFO=$(esp read-flash 0x8000 0x79000 "$HEAD" 2>&1) || { echo "$INFO"; fail "Could not talk to the reader. Wake it, keep it on the home screen, and retry."; }
grep -q "ESP32-S31" <<<"$INFO" || fail "This is not an ESP32-S31 device. This firmware is only for Metalio E-INK4 Plus."
MAC=$(grep -oiE 'MAC: *([0-9a-f]{2}:){5}[0-9a-f]{2}' <<<"$INFO" | head -1 | awk '{print $2}' | tr -d ':')
echo "Chip: ESP32-S31   MAC: ${MAC:-unknown}"

python3 - "$HEAD" "$(wc -c < "$FONT")" <<'PY' || fail "Partition check failed (see above). The reader must run the stock Metalio firmware (or this build). Restore the stock firmware first."
import struct, sys
d = open(sys.argv[1], "rb").read(); font_size = int(sys.argv[2])
base = 0x8000; parts = {}
for i in range(0, 0xC00, 32):
    e = d[i:i + 32]
    if e[:2] != b"\xaa\x50": break
    parts[e[12:28].split(b"\0")[0].decode(errors="replace")] = struct.unpack("<II", e[4:12])
def need(name, off, min_size):
    o, s = parts.get(name, (None, 0))
    if o != off or s < min_size:
        print(f"   partition {name}: found {o and hex(o)}/{hex(s)}, expected {hex(off)} >= {hex(min_size)}")
        return False
    return True
ok = need("ota_0", 0x80000, 0x500000) & need("font_data", 0xae4000, font_size) & need("nvs", 0x9000, 0x4000)
app = d[0x80000 - base:]
proj = ""
if app[:1] == b"\xe9" and struct.unpack("<I", app[0x20:0x24])[0] == 0xABCD5432:
    ver = app[0x30:0x50].split(b"\0")[0].decode(errors="replace")
    proj = app[0x50:0x70].split(b"\0")[0].decode(errors="replace")
    print(f"   current firmware in ota_0: {proj} {ver}")
if proj != "Metalio-E-Ink4-Plus":
    print("   ota_0 does not hold the Metalio E-Ink4-Plus firmware (third-party firmware installed?)")
    ok = False
print("   partition layout OK" if ok else "")
sys.exit(0 if ok else 1)
PY
rm -f "$HEAD"

# ---- full backup ------------------------------------------------------------
mkdir -p backups
BACKUP="backups/metalio-e-ink4-plus-${MAC:-device}-$(date +%Y%m%d-%H%M%S).bin"
EXISTING=$(ls backups/metalio-e-ink4-plus-"${MAC:-device}"-*.bin 2>/dev/null | head -1 || true)
if [ -n "$EXISTING" ]; then
  say "Backup already exists for this reader: $EXISTING (skipping new backup)"
  BACKUP="$EXISTING"
else
  say "Backing up the whole 16 MB flash (about 2-3 minutes)"
  esp read-flash 0 ALL "$BACKUP" >/dev/null || fail "Backup failed; nothing was written."
  say "Verifying the backup against the chip"
  esp verify-flash 0x0 "$BACKUP" >/dev/null || { rm -f "$BACKUP"; fail "Backup verification failed; nothing was written."; }
  echo "Backup OK: $BACKUP   (keep this file! it is the way back to stock)"
fi

if [ "$DRY_RUN" = 1 ]; then
  say "Dry run finished: all checks passed, backup at $BACKUP. Nothing was written."
  exit 0
fi

# ---- install ----------------------------------------------------------------
say "Installing firmware ${VERSION}"
esp write-flash --flash-mode keep --flash-freq keep --flash-size keep \
  0x80000 "$APP" 0xae4000 "$FONT" || fail "Write failed. Run ./flash.sh again, or ./restore.sh $BACKUP to go back."

say "Done. The reader restarts by itself."
echo "Undo any time:  ./restore.sh $BACKUP"
