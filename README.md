# virtio-loopback transport repository

This repository includes the latest version of the "virtio_loopback_transport" driver which is part of the Virtio Loopback Design presented in this [document](https://git.virtualopensystems.com/virtio-loopback/docs/-/blob/master/design_docs). This work carried on by Virtual Open Systems in the [Automotive Grade Linux](https://www.automotivegradelinux.org) community.

As described in the design document, the transport is only a part of a more complex architecture. If you want to see the implementation and build the other components, refer to the [virtio-loopback docs repository](https://git.virtualopensystems.com/virtio-loopback/docs/-/tree/epsilon-release).

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
