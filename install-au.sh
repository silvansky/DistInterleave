#!/bin/bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$PROJECT_DIR/build/DistInterleave_artefacts/Release/AU/DistInterleave.component"
DST="$HOME/Library/Audio/Plug-Ins/Components"
SYSTEM_INSTALL=false

for arg in "$@"; do
  case "$arg" in
    --user) DST="$HOME/Library/Audio/Plug-Ins/Components"; SYSTEM_INSTALL=false ;;
    --system) DST="/Library/Audio/Plug-Ins/Components"; SYSTEM_INSTALL=true ;;
    *) echo "usage: $0 [--user|--system]" >&2; exit 2 ;;
  esac
done

install_command() {
  if "$SYSTEM_INSTALL"; then
    sudo "$@"
  else
    "$@"
  fi
}

if [ ! -d "$SRC" ]; then
  echo "Not built: $SRC" >&2
  echo "Run: cmake --build build --config Release --target DistInterleave_AU --parallel 6" >&2
  exit 1
fi

# Use the bundle's actual identity when validating, rather than duplicating it.
PLIST="$SRC/Contents/Info.plist"
TYPE=$(/usr/bin/plutil -extract AudioComponents.0.type raw -o - "$PLIST")
SUBTYPE=$(/usr/bin/plutil -extract AudioComponents.0.subtype raw -o - "$PLIST")
MANUFACTURER=$(/usr/bin/plutil -extract AudioComponents.0.manufacturer raw -o - "$PLIST")

/usr/bin/codesign --force --sign - --timestamp=none "$SRC"
/usr/bin/codesign --verify --deep --strict "$SRC"

install_command mkdir -p "$DST"
install_command rm -rf "$DST/DistInterleave.component"
install_command /usr/bin/ditto "$SRC" "$DST/DistInterleave.component"
install_command /usr/bin/xattr -dr com.apple.quarantine "$DST/DistInterleave.component" 2>/dev/null || true
/usr/bin/codesign --verify --deep --strict "$DST/DistInterleave.component"
echo "Installed to $DST/DistInterleave.component"

# Refresh this user's registrar so auval and newly opened hosts see the bundle.
/usr/bin/killall -u "$(id -un)" AudioComponentRegistrar 2>/dev/null || true
# Discovery is asynchronous, especially on machines with many installed AUs.
VALIDATION_LOG=$(mktemp -t DistInterleave-auval)
trap 'rm -f "$VALIDATION_LOG"' EXIT
for attempt in {1..15}; do
  if /usr/bin/auval -v "$TYPE" "$SUBTYPE" "$MANUFACTURER" > "$VALIDATION_LOG" 2>&1; then
    cat "$VALIDATION_LOG"
    exit 0
  else
    VALIDATION_STATUS=$?
  fi
  # Retry only missing registration, never a plugin validation failure.
  if ! /usr/bin/grep -q "FATAL ERROR: didn't find the component" "$VALIDATION_LOG" \
      || [ "$attempt" -eq 15 ]; then
    cat "$VALIDATION_LOG" >&2
    exit "$VALIDATION_STATUS"
  fi
  sleep 2
done
