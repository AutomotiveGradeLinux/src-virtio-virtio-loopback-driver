# virtio-loopback transport repository

This repository includes the "virtio-loopback" driver which is part of the Virtio-loopback design. If you want to learn more about how to set up and test the whole virtio-loopback architecture, refer to the [virtio-loopback testing guide](https://gerrit.automotivelinux.org/gerrit/gitweb?p=src/virtio/virtio-loopback-adapter.git;a=blob;f=Documentation/testing_virtio_loopback_design.md;hb=HEAD).

This work carried on by Virtual Open Systems in the [Automotive Grade Linux](https://www.automotivegradelinux.org) community.

## Build the virtio-loopback transport

In order to build this project the next commands need to be used:
- `make` for x86
- `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-` for arm64
- `make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu-` for riscv64

**NOTE**: The `DEBUG=1` can be used in order to enable the driver's debug logs.

## Tested platforms
The driver has been tested with the following platforms (sorted by architecture):
- x86: QEMU (machine `pc`), Thinkpad e14 gen3, x86 servers etc.
- aarch64: QEMU (machine `virt`), Raspberry PI 4, AGL reference HW board (Rcar-H3)
- riscv64: LicheePi4A

## License

This project is licensed under the terms of the GNU General Public License (GPL), version 2 or (at your option) any later version. See the [COPYING](COPYING) file for details.

