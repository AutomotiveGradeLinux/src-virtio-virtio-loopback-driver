# virtio-loopback transport repository

This repository includes the beta version of the "virtio_loopback_transport" driver which is part of the Virtio Loopback Design presented in this [document](https://git.virtualopensystems.com/virtio-loopback/docs/-/blob/master/design_docs). This work carried on by Virtual Open Systems in the [Automotive Grade Linux](https://www.automotivegradelinux.org) community.

As described in the design document, the transport is only a part of a more complex architecture. If you want to see the implementation and build the other components, refer to the [virtio-loopback docs repository](https://git.virtualopensystems.com/virtio-loopback/docs/-/tree/beta-release).

## Build the virtio-loopback transport

In order to build this project the next commands need to be used:
- `make` for x86
- `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-` for arm64

**NOTE**: The `DEBUG=1` can be used in order to enable the driver's logs.

This driver is tested with Linux v5.10 and it is NOT be compatible with newer versions ("fcheck_files" function is replaced by "files_lookup_fd_rcu".

