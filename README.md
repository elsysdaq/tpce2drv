# TPCE2 Linux Driver

`tpce2drv` is the Linux kernel module for TPCE Gen2 DAQ PCIe boards.

## Build With Make

Install build tools and kernel headers:

```sh
sudo apt install build-essential linux-headers-$(uname -r)
sudo apt install build-essential linux-headers-generic
```

Default build for the currently running kernel, build for a specific kernel, or clean:

```sh
make
make KERNELVER=6.11.0-29-generic
make clean
```

## Install With Make

Installs the module with modprobe:

```sh
sudo make install
```

Manual checks:

```sh
modinfo tpce2drv
lsmod | grep tpce2drv
dmesg | grep tpce2drv
```