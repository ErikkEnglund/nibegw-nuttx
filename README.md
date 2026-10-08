# nibegw on NuttX (ESP32-C3)

A NIBE heat pump gateway on an ESP32-C3-DevKitC-02. It answers the heat
pump on the RS-485 accessory bus as a MODBUS 40 and relays frames over
UDP to Home Assistant or openHAB (the NibeGW protocol), like `nibegw.c`
does on Linux. It starts by itself at boot. NSH is available on the USB
console and over telnet.

The firmware has no site-specific settings built in. Wi-Fi and the host
to forward to are set from NSH and saved in flash.

## Repository

    nibegw/            the NuttX application
    configs/nibegw/    NuttX configuration for the esp32c3-devkit board
    tools/pumpsim.py   plays the heat pump for bench tests
    nibegw.c           the openHAB nibegw for Linux
    LOG.SET            logging setup for the F730's USB log
    entries.txt        the log entries wanted in LOG.SET

## Hardware

ESP32-C3-DevKitC-02 to a MAX485 module powered from 3V3:

| MAX485                 | ESP32-C3 | Function                 |
|------------------------|----------|--------------------------|
| DI                     | GPIO5    | UART1 TX                 |
| RO                     | GPIO6    | UART1 RX                 |
| DE + RE (tied)         | GPIO4    | RS-485 DIR (active high) |
| VCC                    | 3V3      |                          |
| GND                    | GND      |                          |

UART1 is moved off its default pins GPIO8/9 because those are strapping
pins. The console is UART0 (TX GPIO21, RX GPIO20) on the onboard CP2102N
USB port, 115200 baud.

MAX485 A/B go to the heat pump, on the NIBE F730 to input board AA3,
terminal X4: pin 11 = A, pin 10 = B, pin 9 = GND. If you only get
checksum errors or nothing at all, swap A and B. Switch the heat pump off
before connecting.

## Build and flash

Needs a NuttX workspace (`nuttx/` and `apps/` side by side), and the
xPack `riscv-none-elf-gcc` 13.2.0-2 and `esptool` on `PATH`.

Out-of-tree applications go in `apps/external`, which the apps tree
ignores: a plain directory with one symlink per application and two
wrapper files.

    apps/external/Makefile:
        MENUDESC = "External Applications"
        include $(APPDIR)/Directory.mk

    apps/external/Make.defs:
        include $(wildcard $(APPDIR)/external/*/Make.defs)

    ln -s <this repository>/nibegw apps/external/nibegw

Configure with `configs/nibegw` from this repository, build and flash:

    cd nuttx
    make distclean
    ./tools/configure.sh -l <this repository>/configs/nibegw
    make -j
    make flash ESPTOOL_PORT=/dev/serial/by-id/<CP2102N>

`configure.sh` uses the configuration where it is: it writes
`nuttx/.config` from the defconfig and links `nuttx/Make.defs` to
`configs/nibegw/Make.defs`, which only includes the build rules of the
in-tree esp32c3-devkit board. The NuttX trees get no tracked changes.

`make distclean` also deletes `apps/Kconfig`, which is generated only
when it is missing. Run it after adding or removing a link in
`apps/external`.

To change the configuration, run `make menuconfig`, then
`make savedefconfig` and copy `nuttx/defconfig` to
`configs/nibegw/defconfig`. On top of `esp32c3-devkit:wifi` it sets:

- UART1 on GPIO5/6 with RS-485 direction on GPIO4, 9600 baud
- Wi-Fi station with DHCP, with no credentials built in
- UDP checksums, off by default without IPv6. Some routers ignore DHCP
  packets without one when building their client list.
- the DHCP hostname `nibegw`
- NSH over telnet
- the MWDT0 hardware watchdog (`/dev/watchdog0`)
- the gateway, started together with NSH at boot
  (`CONFIG_INIT_ENTRYPOINT="nibegwboot_main"`)

The apps tree needs the DHCP client fix "netutils/dhcpc: Send the
REQUEST before using the offered address" (branch
`dhcpc-request-source-address` until it is merged upstream). Without it the
board sends its DHCP REQUEST from the offered address, and TP-Link Deco
routers then treat it as a static-IP device: it shows as offline and
cannot get an address reservation.

Host unit tests for the frame parser:

    make -C nibegw/test

## Flash layout

4 MB flash: firmware from `0x0`, a 1 MB SPIFFS partition at
`0x180000` mounted as `/data`. Erased flash mounts as an empty file
system, so a new board needs no formatting. If the partition holds
something else from earlier use, erase the chip once before the first
flash:

    esptool -c esp32c3 -p <port> erase-flash

Flashing new firmware does not touch `/data`, so settings survive
updates.

## First-time setup

On the USB console (115200 baud, e.g. `picocom -b 115200 <port>`):

    nsh> wapi psk wlan0 <password> 3
    nsh> wapi essid wlan0 <ssid> 1
    nsh> renew wlan0
    nsh> ifconfig wlan0
    nsh> wapi save_config wlan0
    nsh> nibegw set host <Home Assistant IP>

`wapi save_config` writes `/data/wapi.conf`, which is used at every boot.
`nibegw set` writes `/data/nibegw.conf` and restarts the gateway.

If there is no address at boot, for example because the router comes
back slower than the board after a power cut, a watchdog task retries
Wi-Fi and DHCP every 30 seconds until it gets one. Once connected, the
Wi-Fi driver reconnects by itself after drops. The same task renews the
DHCP lease at the time the router asks for (half the lease by default),
which NuttX's boot-time DHCP never does. The gateway answers the heat
pump the whole time; only forwarding waits for the network.

The board asks DHCP for the hostname `nibegw`. Reserve an address for its
MAC (`ifconfig wlan0` shows it) in the router, so Home Assistant can
reach it.

In Home Assistant: Settings > Devices & services > Add integration >
Nibe Heat Pump > NibeGW, with the board's address as remote IP, listening
port 9999, read port 10000 and write port 10001.

## The nibegw command

    nibegw                  state and counters
    nibegw config           saved settings
    nibegw set <key> <val>  save a setting and restart
                            (host, port, readport, writeport, addr;
                            "host none" stops forwarding)
    nibegw stop | start | restart
    nibegw start -a <ip> -v run with temporary overrides, print frames

Without a host set the gateway still answers the heat pump, it just
doesn't forward anything.

The gateway loop feeds the hardware watchdog (`/dev/watchdog0`, 30 s).
If the gateway task or the whole system hangs, the board resets and
starts over. `nibegw stop` stops the watchdog so a deliberate stop does
not reset the board; `nibegw status` shows whether it is on.

## Debugging

    telnet nibegw     (or the board's IP)
    nsh> nibegw
    nsh> ps
    nsh> free

Telnet has no password: anyone on the LAN can get a shell.

## Bench test without the heat pump

With a USB-RS485 adapter on the laptop wired to the MAX485, the laptop
can play the heat pump:

    nsh> nibegw stop
    nsh> nibegw start -a <laptop IP>
    $ tools/pumpsim.py --gateway <board IP>
    nsh> nibegw restart

It checks ACK/NAK replies and their latency, that frames for other
addresses get no reply, that a UDP read request goes out on the next read
token, and that forwarded frames arrive unchanged.

## Connecting to the heat pump

Start the gateway first, then enable "modbus" in the heat pump's service
menu 5.2 (hold Back for 7 seconds on the start menu to show menu 5). To
disconnect, disable "modbus" before stopping the gateway, or the heat
pump raises an alarm.
