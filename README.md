# GDM1602A LCD Driver for Embedded Linux

A Linux kernel I2C driver for the GDM1602A character LCD, developed and
tested on a Raspberry Pi 3 Model B running a custom Buildroot Linux system.

## Features

- Linux kernel I2C driver for the GDM1602A LCD.
- Device Tree integration using `custom,gdm1602a`.
- Character device interface at `/dev/lcd0`.
- ASCII text and selected Lithuanian UTF-8 characters.
- Longer messages displayed in pages with delayed page switching.
- Error reporting for failed I2C operations.

## Hardware and software

- Raspberry Pi 3 Model B
- GDM1602A character LCD
- I2C bus 1, address `0x3f`
- Linux kernel 6.6.0
- Buildroot and an ARM cross-compilation toolchain

## Device Tree

The LCD node is added under the I2C1 controller in
`bcm2837-rpi-3-b.dts`:

    &i2c1 {
        status = "okay";

        lcd@3f {
            compatible = "custom,gdm1602a";
            reg = <0x3f>;
        };
    };

The corresponding patch is in
`patches/0001-add-gdm1602a-lcd-device-tree.patch`.

Check the patch from the kernel source directory:

    cd ~/linux_embedded_tasks/Task4/linux
    git apply --check \
      ~/linux_embedded_tasks/Task7/patches/0001-add-gdm1602a-lcd-device-tree.patch

## Build

The Makefile uses the configured Linux kernel build directory and ARM
cross-compiler:

    cd ~/linux_embedded_tasks/Task7
    make

The resulting kernel module is `lcd_driver.ko`.

## Test on the Raspberry Pi

Load the module as root and check the kernel log and device node:

    insmod lcd_driver.ko
    dmesg | tail -n 30
    ls -l /dev/lcd0

Write text to the display:

    echo -n "Hello from Embedded Linux" > /dev/lcd0
    printf '%s' 'Labas, ąčęėįšųū!' > /dev/lcd0

Remove the module when finished, if it is not in use:

    rmmod lcd_driver

## Implementation notes

The driver uses the Linux I2C and misc-device APIs. It copies user-space
data into a bounded kernel buffer and displays longer messages in pages
using delayed work.

The LCD supports eight custom character slots in CGRAM. The current
implementation maps selected Lithuanian characters to these slots.

## Current limitations

- The Lithuanian character set is incomplete; `ž` is not yet supported.
- Uppercase and lowercase Lithuanian characters do not yet have distinct
  custom glyphs.

