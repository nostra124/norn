#!/bin/sh
# norn — Mainline DHT, secure sessions and cluster KV
#
# Universal installer script. Works on:
#   - Linux (Debian/Ubuntu, Fedora/RHEL/CentOS, Arch, OpenSUSE)
#   - macOS (Homebrew or MacPorts)
#   - FreeBSD (Ports)
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/nostra124/norn/master/scripts/install.sh | sh
#
# Or:
#   curl -fsSL https://raw.githubusercontent.com/nostra124/norn/master/scripts/install.sh -o install.sh
#   sh install.sh
#
# Options:
#   PREFIX=/usr/local     Install prefix (default: /usr/local)
#   VERSION=v0.12.0      Version to install (default: latest release)
#   BUILD_DIR=/tmp/norn-build   Build directory
#   NO_CLEANUP=1        Don't remove build directory after install
#   SKIP_DEPS=1         Skip dependency installation (assume installed)
#
# License: MIT

set -e

# ==============================================================================
# Configuration
# ==============================================================================

PREFIX="${PREFIX:-/usr/local}"
VERSION="${VERSION:-v0.12.0}"
BUILD_DIR="${BUILD_DIR:-/tmp/norn-build-$$}"
NO_CLEANUP="${NO_CLEANUP:-}"
SKIP_DEPS="${SKIP_DEPS:-}"
NO_SUDO="${NO_SUDO:-}"

# Colors (disabled if not a terminal)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    BLUE='\033[0;34m'
    NC='\033[0m' # No Color
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
fi

# ==============================================================================
# Utilities
# ==============================================================================

info() {
    printf "${BLUE}▶${NC} %s\n" "$*"
}

success() {
    printf "${GREEN}✓${NC} %s\n" "$*"
}

warn() {
    printf "${YELLOW}!${NC} %s\n" "$*" >&2
}

error() {
    printf "${RED}✗${NC} %s\n" "$*" >&2
}

die() {
    error "$@"
    exit 1
}

run() {
    if [ -n "$NO_SUDO" ]; then
        "$@"
    elif [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        sudo "$@"
    fi
}

# ==============================================================================
# Platform Detection
# ==============================================================================

detect_os() {
    case "$(uname -s)" in
        Linux*)  OS=linux ;;
        Darwin*) OS=macos ;;
        FreeBSD*) OS=freebsd ;;
        *)       die "Unsupported OS: $(uname -s)" ;;
    esac
}

detect_distro() {
    if [ "$OS" != "linux" ]; then
        DISTRO=unknown
        return
    fi

    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO="$ID"
    elif [ -f /etc/debian_version ]; then
        DISTRO=debian
    elif [ -f /etc/redhat-release ]; then
        DISTRO=rhel
    else
        DISTRO=unknown
    fi
}

detect_package_manager() {
    case "$OS" in
        linux)
            if command -v apt-get >/dev/null 2>&1; then
                PKG_MANAGER=apt
            elif command -v yum >/dev/null 2>&1; then
                PKG_MANAGER=yum
            elif command -v dnf >/dev/null 2>&1; then
                PKG_MANAGER=dnf
            elif command -v pacman >/dev/null 2>&1; then
                PKG_MANAGER=pacman
            elif command -v zypper >/dev/null 2>&1; then
                PKG_MANAGER=zypper
            elif command -v apk >/dev/null 2>&1; then
                PKG_MANAGER=apk
            else
                die "No supported package manager found"
            fi
            ;;
        macos)
            if command -v brew >/dev/null 2>&1; then
                PKG_MANAGER=brew
            elif command -v port >/dev/null 2>&1; then
                PKG_MANAGER=port
            else
                warn "Neither Homebrew nor MacPorts found"
                warn "Install dependencies manually: autoconf automake libtool libsodium"
                PKG_MANAGER=none
            fi
            ;;
        freebsd)
            PKG_MANAGER=pkg
            ;;
    esac
}

# ==============================================================================
# Dependency Installation
# ==============================================================================

install_deps_apt() {
    info "Installing dependencies with apt..."
    run apt-get update -qq
    run apt-get install -y -qq build-essential autoconf automake libtool pkg-config libsodium-dev curl
}

install_deps_yum() {
    info "Installing dependencies with yum..."
    run yum install -y -q gcc make autoconf automake libtool pkgconfig libsodium-devel curl
}

install_deps_dnf() {
    info "Installing dependencies with dnf..."
    run dnf install -y -q gcc make autoconf automake libtool pkgconfig libsodium-devel curl
}

install_deps_pacman() {
    info "Installing dependencies with pacman..."
    run pacman -S --noconfirm --quiet base-devel autoconf automake libtool pkg-config libsodium curl
}

install_deps_zypper() {
    info "Installing dependencies with zypper..."
    run zypper --non-interactive --quiet install gcc make autoconf automake libtool pkg-config libsodium-devel curl
}

install_deps_apk() {
    info "Installing dependencies with apk..."
    run apk add --quiet build-base autoconf automake libtool pkgconf libsodium-dev curl
}

install_deps_brew() {
    info "Installing dependencies with Homebrew..."
    brew install autoconf automake libtool pkg-config libsodium || true
}

install_deps_port() {
    info "Installing dependencies with MacPorts..."
    run port install autoconf automake libtool pkgconfig libsodium
}

install_deps_pkg() {
    info "Installing dependencies with FreeBSD pkg..."
    run pkg install -y automake autoconf libtool pkgconf libsodium curl
}

install_deps() {
    if [ -n "$SKIP_DEPS" ]; then
        warn "Skipping dependency installation"
        return
    fi

    case "$PKG_MANAGER" in
        apt)     install_deps_apt ;;
        yum)     install_deps_yum ;;
        dnf)     install_deps_dnf ;;
        pacman)  install_deps_pacman ;;
        zypper)  install_deps_zypper ;;
        apk)     install_deps_apk ;;
        brew)    install_deps_brew ;;
        port)    install_deps_port ;;
        pkg)     install_deps_pkg ;;
        none)    warn "Please ensure dependencies are installed: autoconf automake libtool libsodium" ;;
        *)       die "Unknown package manager: $PKG_MANAGER" ;;
    esac
}

# ==============================================================================
# Build & Install
# # ==============================================================================

download_source() {
    info "Downloading norn $VERSION..."
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    if [ "$VERSION" = "master" ] || [ "$VERSION" = "main" ]; then
        # Clone latest from git
        if ! command -v git >/dev/null 2>&1; then
            die "git is required to install from master branch"
        fi
        git clone --depth 1 https://github.com/nostra124/norn.git norn-src
        cd norn-src
    else
        # Download release tarball
        TARBALL_URL="https://github.com/nostra124/norn/archive/refs/tags/${VERSION}.tar.gz"
        curl -fsSL "$TARBALL_URL" -o norn.tar.gz
        tar xzf norn.tar.gz
        cd "norn-${VERSION#v}"
    fi
}

build() {
    info "Building norn..."
    
    # Run autogen if needed (from git clone)
    if [ -f "autogen.sh" ]; then
        ./autogen.sh
    fi
    
    # Configure
    ./configure --prefix="$PREFIX"
    
    # Build
    make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
}

run_tests() {
    info "Running tests..."
    if make check; then
        success "All tests passed"
    else
        warn "Some tests failed, but continuing with installation"
    fi
}

install() {
    info "Installing to $PREFIX..."
    run make install
}

# BUG-002: a stale libnorn.so* left in an alternative prefix by a prior install
# can shadow the freshly installed library on the linker search path and break
# nornd at runtime (undefined symbol). Before installing, scrub prior copies
# in well-known libdirs *other* than the current $PREFIX's libdir so this
# install is authoritative. The list covers the common defaults plus the
# Debian/Ubuntu multiarch libdir.
purge_stale_lib() {
    info "Removing stale libnorn from alternative prefixes..."

    # Candidate libdirs to scrub. Keep these as literal paths so both the test
    # and a reader can see exactly which directories are touched. The loop
    # skips any entry that lives under $PREFIX (so a /usr/local install does
    # not wipe its own files, and a /usr install does not wipe /usr).
    candidates="/usr/local/lib /usr/lib"
    # Append Debian multiarch libdirs (e.g. /usr/lib/x86_64-linux-gnu).
    for d in /usr/local/lib/*-linux-gnu /usr/lib/*-linux-gnu; do
        [ -d "$d" ] && candidates="$candidates $d"
    done

    prefix_lib="$PREFIX/lib"
    for libdir in $candidates; do
        [ "$libdir" = "$prefix_lib" ] && continue
        [ -d "$libdir" ] || continue
        # Remove every libnorn artifact a prior `make install` could have laid
        # down here. Errors are ignored (files may not exist).
        rm -f "$libdir"/libnorn.so \
              "$libdir"/libnorn.so.0 \
              "$libdir"/libnorn.so.0.0.0 \
              "$libdir"/libnorn.a \
              "$libdir"/libnorn.la 2>/dev/null || true
    done

    # Refresh the linker cache so the removals take effect immediately.
    if [ "$OS" = "linux" ] && [ -x /sbin/ldconfig ]; then
        run /sbin/ldconfig 2>/dev/null || true
    fi
}

# ==============================================================================
# Post-install
# ==============================================================================

post_install() {
    success "norn $VERSION installed successfully!"
    echo ""
    info "Installed binaries:"
    printf "  %-15s %s\n" "norn" "CLI (DHT operations + cluster client)"
    printf "  %-15s %s\n" "nornd" "Node daemon (cluster KV host)"
    printf "  %-15s %s\n" "norn-forward" "TCP-over-norn tunnel"
    printf "  %-15s %s\n" "libnorn" "C library (-lnorn, pkg-config norn)"
    echo ""
    info "Quick start:"
    echo "  norn keygen                              # Generate identity"
    echo "  nornd --identity ~/.ssh/id_ed25519 &      # Start daemon (background)"
    echo "  norn cluster put greet hello              # Store value in cluster KV"
    echo "  norn cluster get greet                    # Retrieve value"
    echo ""
    info "Documentation: https://github.com/nostra124/norn#readme"
    
    # Add to PATH if not already there
    if ! echo "$PATH" | grep -q "$PREFIX/bin"; then
        echo ""
        warn "Add $PREFIX/bin to your PATH:"
        if [ "$SHELL" = "/bin/zsh" ] || [ "$SHELL" = "/usr/bin/zsh" ]; then
            echo "  echo 'export PATH=\"$PREFIX/bin:\$PATH\"' >> ~/.zshrc"
        elif [ "$SHELL" = "/bin/bash" ] || [ "$SHELL" = "/usr/bin/bash" ]; then
            echo "  echo 'export PATH=\"$PREFIX/bin:\$PATH\"' >> ~/.bashrc"
        else
            echo "  export PATH=\"$PREFIX/bin:\$PATH\""
        fi
    fi
    
    # Update library cache on Linux
    if [ "$OS" = "linux" ] && [ -x /sbin/ldconfig ]; then
        run /sbin/ldconfig "$PREFIX/lib" 2>/dev/null || true
    fi
}

cleanup() {
    if [ -z "$NO_CLEANUP" ] && [ -d "$BUILD_DIR" ]; then
        info "Cleaning up build directory..."
        rm -rf "$BUILD_DIR"
    fi
}

# ==============================================================================
# Main
# ==============================================================================

main() {
    echo ""
    echo "  _   _ ____    _   _            _   "
    echo " | \\ | |  _ \\  | \\ | |   _ _ __| |_ "
    echo " |  \\| | | | | |  \\| |  | | '__| __|"
    echo " | |\\  | |_| | | |\\  |  |_| | |  |_  "
    echo " |_| \\_|____/  |_| \\_|\\__,_|_|   \\__|"
    echo ""
    echo "  Mainline DHT • Secure Sessions • Cluster KV"
    echo ""
    
    # Check dependencies
    command -v curl >/dev/null 2>&1 || command -v wget >/dev/null 2>&1 || die "curl or wget is required"
    command -v make >/dev/null 2>&1 || die "make is required"
    command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || die "C compiler is required"
    
    # Detect platform
    detect_os
    detect_distro
    detect_package_manager
    
    info "Platform: $OS (${DISTRO:-$PKG_MANAGER})"
    info "Prefix: $PREFIX"
    info "Version: $VERSION"
    info "Build dir: $BUILD_DIR"
    echo ""
    
    # Install dependencies
    install_deps
    echo ""
    
    # Download source
    download_source
    
    # Build
    build
    echo ""
    
    # Test (optional, don't fail on test errors)
    run_tests 2>/dev/null || true
    echo ""
    
    # Remove stale libnorn from alternative prefixes so it cannot shadow
    # the freshly installed library at runtime (BUG-002).
    purge_stale_lib
    echo ""
    
    # Install
    install
    echo ""
    
    # Cleanup
    cleanup
    
    # Post-install message
    post_install
}

# Entry point
trap 'error "Installation failed"; exit 1' INT TERM
main "$@"