#!/usr/bin/env bash
# Checks that everything needed to build SandBots is installed, and installs what is missing.
#
#   ./install_deps.sh            check, then offer to install anything missing
#   ./install_deps.sh --yes      install without asking
#   ./install_deps.sh --check    only check (exit status 1 if something is missing)
#
# Needs: a C++17 compiler, CMake >= 3.10, make (or ninja), pkg-config and the SDL2 development files.
set -u

YES=0
CHECK_ONLY=0
for arg in "$@"; do
    case "$arg" in
        -y|--yes) YES=1 ;;
        -c|--check) CHECK_ONLY=1 ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $arg (try --help)"; exit 2 ;;
    esac
done

if [ -t 1 ]; then OK=$'\033[32m'; BAD=$'\033[31m'; DIM=$'\033[2m'; END=$'\033[0m'; else OK=; BAD=; DIM=; END=; fi
have() { command -v "$1" >/dev/null 2>&1; }
ok()   { printf '  %s[ ok ]%s %s\n' "$OK" "$END" "$1"; }
fail() { printf '  %s[miss]%s %s\n' "$BAD" "$END" "$1"; }

# ---------------------------------------------------------------- what do we have to work with?
OS="$(uname -s)"
PM=""; PKGS=""; INSTALL=""
case "$OS" in
    Darwin)
        if have brew; then PM="Homebrew"; PKGS="cmake pkg-config sdl2"; INSTALL="brew install $PKGS"; fi ;;
    MINGW*|MSYS*)
        PM="MSYS2 pacman"
        PKGS="mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-SDL2 mingw-w64-x86_64-pkgconf make"
        INSTALL="pacman -S --needed --noconfirm $PKGS" ;;
    Linux)
        if   have apt-get; then PM="apt";    PKGS="build-essential cmake pkg-config libsdl2-dev";               INSTALL="apt-get install -y $PKGS"
        elif have dnf;     then PM="dnf";    PKGS="gcc-c++ make cmake pkgconf-pkg-config SDL2-devel";           INSTALL="dnf install -y $PKGS"
        elif have pacman;  then PM="pacman"; PKGS="base-devel cmake pkgconf sdl2";                              INSTALL="pacman -S --needed --noconfirm $PKGS"
        elif have zypper;  then PM="zypper"; PKGS="gcc-c++ make cmake pkg-config SDL2-devel";                   INSTALL="zypper --non-interactive install $PKGS"
        elif have apk;     then PM="apk";    PKGS="build-base cmake pkgconf sdl2-dev";                          INSTALL="apk add $PKGS"
        fi ;;
esac

# ---------------------------------------------------------------- checks
MISSING=0
TMP="$(mktemp -d 2>/dev/null || echo /tmp/sandbots-deps.$$)"; mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT

check_compiler() {
    CXX_FOUND=""
    for cxx in "${CXX:-}" g++ clang++ c++; do
        [ -n "$cxx" ] && have "$cxx" || continue
        printf 'int main() { auto f = [](auto x) { return x + 1; }; return f(0) - 1; }\n' > "$TMP/t.cpp"
        if "$cxx" -std=c++17 "$TMP/t.cpp" -o "$TMP/t" >/dev/null 2>&1 && "$TMP/t"; then CXX_FOUND="$cxx"; break; fi
    done
    if [ -n "$CXX_FOUND" ]; then ok "C++17 compiler: $CXX_FOUND ($("$CXX_FOUND" --version 2>/dev/null | head -1))"
    else fail "a C++17 compiler (g++ 7+ or clang++ 5+)"; MISSING=1; fi
}

check_cmake() {
    if have cmake; then
        ver="$(cmake --version | head -1 | awk '{print $3}')"
        if [ "$(printf '%s\n3.10\n' "$ver" | sort -V | head -1)" = "3.10" ]; then ok "CMake $ver"
        else fail "CMake >= 3.10 (found $ver)"; MISSING=1; fi
    else fail "CMake >= 3.10"; MISSING=1; fi
}

check_make() {
    if have make; then ok "make"
    elif have ninja; then ok "ninja"
    else fail "make (or ninja)"; MISSING=1; fi
}

check_pkgconfig() {
    if have pkg-config || have pkgconf; then ok "pkg-config"
    else fail "pkg-config"; MISSING=1; fi
}

check_sdl2() {
    # the real test: can a program that includes <SDL.h> be compiled and linked?
    local cxx="${CXX_FOUND:-g++}" cflags="" libs="-lSDL2"
    if have sdl2-config; then cflags="$(sdl2-config --cflags)"; libs="$(sdl2-config --libs)"
    elif have pkg-config && pkg-config --exists sdl2 2>/dev/null; then cflags="$(pkg-config --cflags sdl2)"; libs="$(pkg-config --libs sdl2)"; fi
    printf '#include <SDL.h>\nint main(int argc, char** argv) { SDL_version v; SDL_GetVersion(&v); return v.major < 2; }\n' > "$TMP/sdl.cpp"
    if have "$cxx" && "$cxx" -std=c++17 $cflags "$TMP/sdl.cpp" -o "$TMP/sdl" $libs >/dev/null 2>&1; then
        ok "SDL2 development files ($(pkg-config --modversion sdl2 2>/dev/null || sdl2-config --version 2>/dev/null || echo found))"
    else
        fail "SDL2 development files (SDL.h and libSDL2)"; MISSING=1
    fi
}

run_checks() {
    MISSING=0
    echo "Checking build dependencies for SandBots:"
    check_compiler
    check_cmake
    check_make
    check_pkgconfig
    check_sdl2
}

run_checks
if [ "$MISSING" -eq 0 ]; then
    echo
    echo "${OK}Everything needed is installed.${END} Build with:"
    echo "  cmake -S . -B build && cmake --build build -j"
    exit 0
fi

echo
if [ -z "$PM" ]; then
    echo "${BAD}No supported package manager found${END} (looked for apt, dnf, pacman, zypper, apk, Homebrew, MSYS2)."
    echo "Install by hand: a C++17 compiler, CMake >= 3.10, make, pkg-config and the SDL2 development package (libsdl2-dev / SDL2-devel)."
    [ "$OS" = "Darwin" ] && echo "On macOS install Homebrew (https://brew.sh) first, plus the Xcode command line tools: xcode-select --install"
    exit 1
fi

# installing needs root, except for Homebrew and MSYS2
SUDO=""
if [ "$PM" != "Homebrew" ] && [ "$PM" != "MSYS2 pacman" ] && [ "$(id -u)" -ne 0 ]; then
    if have sudo; then SUDO="sudo"; else echo "${BAD}Not root and sudo is missing.${END} Run as root:  $INSTALL"; exit 1; fi
fi
echo "Package manager: $PM"
echo "Install command: ${DIM}$SUDO $INSTALL${END}"

if [ "$CHECK_ONLY" -eq 1 ]; then
    echo "Run ./install_deps.sh to install the missing pieces."
    exit 1
fi
if [ "$YES" -eq 0 ]; then
    if [ ! -t 0 ]; then echo "Not interactive: re-run with --yes to install."; exit 1; fi
    printf 'Install now? [Y/n] '
    read -r answer
    case "$answer" in n|N|no|NO) echo "Skipped."; exit 1 ;; esac
fi

[ "$PM" = "apt" ] && $SUDO apt-get update
# shellcheck disable=SC2086
$SUDO $INSTALL || { echo "${BAD}The install command failed.${END}"; exit 1; }

echo
run_checks
if [ "$MISSING" -eq 0 ]; then
    echo
    echo "${OK}All set.${END} Build with:"
    echo "  cmake -S . -B build && cmake --build build -j"
    exit 0
fi
echo
echo "${BAD}Something is still missing${END} - see the lines marked [miss] above."
exit 1
