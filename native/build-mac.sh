#!/bin/sh
# Cross-builds native/build/version.dll (x86, static CRT) on macOS, same flags as build.ps1.
# Needs: brew install llvm lld xwin; xwin --accept-license --arch x86 splat --output ~/.xwin
# Usage: sh native/build-mac.sh [-Version 0.1.5] [-GameDir "/path/to/Forager"]   (-GameDir also copies the DLL there)
set -e
cd "$(dirname "$0")"
VERSION=dev; GAMEDIR=
while [ $# -gt 0 ]; do case "$1" in -Version) VERSION="${2#v}"; shift 2;; -GameDir) GAMEDIR="$2"; shift 2;; *) echo "unknown arg $1"; exit 1;; esac; done
case "$VERSION" in *[!0-9A-Za-z.+-]*) echo "bad version '$VERSION'"; exit 1;; esac  # goes into a C string literal

LLVM=/opt/homebrew/opt/llvm/bin; LLD=/opt/homebrew/opt/lld/bin; X="$HOME/.xwin"
# Source list is taken from build.ps1 so there is only one list to maintain.
SRCS=$(grep -oE '\$(root|mh)\\[^"]+\.(c|cpp)' build.ps1 | sed -e 's/^\$root\\//' -e 's/^\$mh\\/third_party\/minhook\\/' -e 's/\\/\//g')
mkdir -p build

"$LLVM/clang-cl" --target=i686-pc-windows-msvc -fuse-ld=lld /nologo /O2 /MT /W3 /EHsc /std:c++17 \
  /DWIN32_LEAN_AND_MEAN /DNDEBUG "/DCOOP_VERSION=\"$VERSION\"" \
  /imsvc "$X/crt/include" /imsvc "$X/sdk/include/ucrt" /imsvc "$X/sdk/include/um" /imsvc "$X/sdk/include/shared" \
  /Ithird_party/minhook/include /Fobuild/ $SRCS /LD /Febuild/version.dll \
  /link "/LIBPATH:$X/crt/lib/x86" "/LIBPATH:$X/sdk/lib/um/x86" "/LIBPATH:$X/sdk/lib/ucrt/x86" \
  /DEF:src/version.def /SAFESEH:NO
echo "built native/build/version.dll ($VERSION)"

if [ -n "$GAMEDIR" ]; then cp build/version.dll "$GAMEDIR/version.dll"; echo "installed to $GAMEDIR"; fi
