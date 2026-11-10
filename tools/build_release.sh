#!/usr/bin/env bash
# Paczka gry do przekazania innym (Linux x86_64).
#
# Budowanie odbywa się w kontenerze Ubuntu (podman), a nie na systemie gospodarza, bo o tym, na jakich systemach
# ruszy binarka, decyduje wersja biblioteki C (glibc) systemu, na którym ją zbudowano - działa ona na tym samym
# albo NOWSZYM systemie. Fedora 45 ma bardzo nową glibc, więc binarka zbudowana na niej nie ruszy na Ubuntu.
#
# Co jest w paczce:
#   osadnicy/
#     uruchom.sh                    start gry (przechodzi do katalogu gry - ścieżki assets/ są względne)
#     bin/10_settlers_example       gra (libstdc++ i libgcc wkompilowane statycznie: -DENGINE3D_PORTABLE=ON)
#     bin/libengine3d.so            silnik - szukany obok binarki (RUNPATH $ORIGIN)
#     bin/assets/shaders/           skompilowane shadery SPIR-V
#     assets/...                    tylko katalogi używane przez grę
#
# U odbiorcy potrzebne są tylko: sterownik karty z Vulkan 1.3 (Mesa / NVIDIA), SDL3, SDL3_ttf i SDL3_image
#   Ubuntu 25.04+:  sudo apt install libsdl3-0 libsdl3-ttf0 libsdl3-image0 libvulkan1 mesa-vulkan-drivers
#   Fedora:         sudo dnf install SDL3 SDL3_ttf SDL3_image vulkan-loader mesa-vulkan-drivers
#
# Użycie:   tools/build_release.sh                  (obraz ubuntu:25.04 - najstarsze Ubuntu z SDL3 w repozytorium)
#           IMAGE=ubuntu:26.04 tools/build_release.sh
# Wynik:    dist/osadnicy-linux-x86_64.tar.gz
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=${IMAGE:-ubuntu:25.04}
EXAMPLE=10_settlers_example
BUILD_DIR=build-release            # katalog budowania w repo (nie miesza się z build/ i bin/)
OUT_BIN=$BUILD_DIR/bin
DIST=$ROOT/dist
PKG=$DIST/osadnicy

echo "== Budowanie w kontenerze $IMAGE =="
podman run --rm -v "$ROOT":/src:Z -w /src "$IMAGE" bash -euc "
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq g++ cmake make pkg-config libvulkan-dev glslang-tools libsdl3-dev libsdl3-ttf-dev libsdl3-image-dev nlohmann-json3-dev >/dev/null
    cmake -S /src -B /src/$BUILD_DIR -DCMAKE_BUILD_TYPE=Release -DENGINE3D_PORTABLE=ON -DENGINE3D_OUTPUT_DIR=/src/$OUT_BIN
    cmake --build /src/$BUILD_DIR -j\$(nproc) --target $EXAMPLE
    echo '== Zależności binarki =='
    ldd /src/$OUT_BIN/$EXAMPLE
    echo '== Najnowsza wymagana wersja glibc =='
    objdump -T /src/$OUT_BIN/$EXAMPLE /src/$OUT_BIN/libengine3d.so | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1
"

echo "== Składanie paczki w $PKG =="
rm -rf "$PKG"
mkdir -p "$PKG/bin/assets" "$PKG/assets/sound"
cp "$ROOT/$OUT_BIN/$EXAMPLE" "$ROOT/$OUT_BIN/libengine3d.so" "$PKG/bin/"
cp -r "$ROOT/$OUT_BIN/assets/shaders" "$PKG/bin/assets/"
# Assety używane przez Osadników (reszta katalogu assets/ to modele i tekstury innych przykładów).
for d in fonts textures woda skydome; do cp -r "$ROOT/assets/$d" "$PKG/assets/"; done
mkdir -p "$PKG/assets/teren"
cp -r "$ROOT/assets/teren/wyspa" "$PKG/assets/teren/"
cp -r "$ROOT/assets/sound/settlers" "$PKG/assets/sound/"

cat > "$PKG/uruchom.sh" <<'EOS'
#!/bin/sh
# Gra czyta pliki z assets/ względem bieżącego katalogu - przechodzimy do katalogu paczki.
cd "$(dirname "$0")" || exit 1
exec ./bin/10_settlers_example "$@"
EOS
chmod +x "$PKG/uruchom.sh"

cat > "$PKG/CZYTAJ.txt" <<'EOS'
Osadnicy - gra strategiczna na silniku engine3d (Vulkan + SDL3)

Wymagania: Linux x86_64, karta graficzna ze sterownikiem Vulkan 1.3, biblioteki SDL3, SDL3_ttf i SDL3_image.
  Ubuntu 25.04 i nowsze:  sudo apt install libsdl3-0 libsdl3-ttf0 libsdl3-image0 libvulkan1 mesa-vulkan-drivers
  Fedora:                 sudo dnf install SDL3 SDL3_ttf SDL3_image vulkan-loader mesa-vulkan-drivers
  (karty NVIDIA: własnościowy sterownik NVIDIA zawiera obsługę Vulkan)

Uruchomienie:  ./uruchom.sh
Pomoc w grze:  F1
EOS

tar -C "$DIST" -czf "$DIST/osadnicy-linux-x86_64.tar.gz" osadnicy
du -sh "$PKG" "$DIST/osadnicy-linux-x86_64.tar.gz"
echo "Gotowe: $DIST/osadnicy-linux-x86_64.tar.gz"
