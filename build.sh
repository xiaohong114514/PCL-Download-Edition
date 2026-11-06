#!/usr/bin/env bash
# Build PCL-Download-Edition.exe with MinGW-w64 cross compiler
set -e
cd "$(dirname "$0")"

CXX=x86_64-w64-mingw32-g++
OUT=PCL-Download-Edition.exe

echo "[*] Compiling $OUT ..."
$CXX -std=c++17 -O2 -static -static-libgcc -static-libstdc++ \
    -municode -mwindows \
    -I. \
    main.cpp -o "$OUT" \
    -lcomctl32 -lwinhttp -lshell32 -lshlwapi -luser32 -lgdi32 -lcomdlg32 -lole32 -loleaut32 -luuid -ladvapi32

echo "[*] Done. Output:"
ls -lh "$OUT"
file "$OUT"
