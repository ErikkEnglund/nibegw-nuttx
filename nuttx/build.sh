#!/usr/bin/env bash
#
# Configure, build, flash and monitor the NuttX nibegw firmware for the
# ESP32-C3-DevKitC-02.
#
# Usage: build.sh configure|build|flash|monitor|all
#
# Environment overrides:
#   NUTTX_WORKSPACE  directory holding nuttx/ and apps/
#   RISCV_TOOLCHAIN  directory with riscv-none-elf-gcc
#   ESPTOOL_DIR      directory with esptool
#   ESP_PORT         serial port of the board

set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
WORKSPACE=${NUTTX_WORKSPACE:-$HOME/projects/uiw/nuttx-workspace}
NUTTX=$WORKSPACE/nuttx
APPS=$WORKSPACE/apps
BOARD=esp32c3-devkit:wifi
FRAGMENT=$HERE/configs/nibegw.config

TOOLCHAIN=${RISCV_TOOLCHAIN:-$HOME/opt/riscv-none-elf-gcc-13.2.0-2/bin}
ESPTOOL_DIR=${ESPTOOL_DIR:-$HOME/opt/esptool-venv/bin}
export PATH=$TOOLCHAIN:$ESPTOOL_DIR:$PATH

find_port() {
  if [ -n "${ESP_PORT:-}" ]; then
    echo "$ESP_PORT"
    return
  fi

  local port
  port=$(ls /dev/serial/by-id/*CP2102N* 2>/dev/null | head -1 || true)
  if [ -z "$port" ]; then
    echo "No CP2102N serial port found; set ESP_PORT" >&2
    exit 1
  fi

  echo "$port"
}

link_apps() {
  local link=$APPS/external

  if [ -e "$link" ] && [ "$(readlink -f "$link")" != "$HERE/apps" ]; then
    echo "$link exists and does not point to $HERE/apps" >&2
    exit 1
  fi

  ln -sfn "$HERE/apps" "$link"
}

configure() {
  link_apps
  (cd "$NUTTX" && ./tools/configure.sh -E -l "$BOARD")

  # Replace the base settings that the fragment sets, then let Kconfig
  # resolve dependencies.

  local sym
  for sym in $(sed -n 's/^\(CONFIG_[A-Za-z0-9_]*\)=.*/\1/p' "$FRAGMENT"); do
    sed -i -e "/^$sym=/d" -e "/^# $sym is not set\$/d" "$NUTTX/.config"
  done

  cat "$FRAGMENT" >> "$NUTTX/.config"
  make -C "$NUTTX" olddefconfig > /dev/null

  # Fail if a dependency silently dropped one of our settings

  local missing=0
  while IFS= read -r line; do
    case "$line" in
      CONFIG_*=*)
        if ! grep -qxF "$line" "$NUTTX/.config"; then
          echo "not applied: $line" >&2
          missing=1
        fi
        ;;
    esac
  done < "$FRAGMENT"

  if [ "$missing" -ne 0 ]; then
    exit 1
  fi
}

build() {
  make -C "$NUTTX" -j"$(nproc)"
}

flash() {
  make -C "$NUTTX" flash ESPTOOL_PORT="$(find_port)"
}

monitor() {
  picocom -b 115200 "$(find_port)"
}

case "${1:-}" in
  configure) configure ;;
  build)     build ;;
  flash)     flash ;;
  monitor)   monitor ;;
  all)       configure; build; flash ;;
  *)
    echo "Usage: $0 configure|build|flash|monitor|all" >&2
    exit 1
    ;;
esac
