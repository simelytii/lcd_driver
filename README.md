# GDM1602A LCD Driver for Embedded Linux

A Linux kernel I2C driver for the GDM1602A 16x2 character LCD. Developed
and tested on a Raspberry Pi 3 Model B running a custom Buildroot Linux
system with Linux kernel 6.6.0.

## Features

- Linux kernel I2C driver integrated through the Device Tree.
- Character device interface exposed as `/dev/lcd0`.
- ASCII text and Lithuanian UTF-8 character handling.
- Separate custom glyphs for uppercase and lowercase Lithuanian letters,
  including ą, č, ę, ė, į, š, ų, ū, ž and their uppercase forms.
- Dynamic use of the LCD controller's eight CGRAM custom-character slots.
- UTF-8-aware text processing and page splitting.
- Messages longer than the 16x2 display are shown in pages, with automatic
  page switching every five seconds.
- Error reporting for failed I2C operations.
- Bounded kernel buffering and delayed-work synchronization.

The LCD controller has only eight custom-character slots. When a page
contains more distinct Lithuanian glyphs than can fit in CGRAM, additional
glyphs fall back to their corresponding base ASCII letters.

## Hardware and software

- Raspberry Pi 3 Model B
- GDM1602A 16x2 character LCD
- I2C bus 1, address `0x3f`
- Linux kernel 6.6.0
- Buildroot
- ARM cross-compilation toolchain

## Device Tree

The LCD node is added under the I2C1 controller in
`bcm2837-rpi-3-b.dts`:

```dts
&i2c1 {
    status = "okay";

    lcd@3f {
        compatible = "custom,gdm1602a";
        reg = <0x3f>;
    };
};
```

The corresponding Device Tree patch is located at
`patches/0001-add-gdm1602a-lcd-device-tree.patch`.

Check whether the patch applies cleanly from the kernel source directory:

```bash
cd ~/linux_embedded_tasks/Task4/linux
git apply --check \
  ~/linux_embedded_tasks/Task7/patches/0001-add-gdm1602a-lcd-device-tree.patch
```

## Build

The Makefile uses the configured Linux kernel build directory and ARM
cross-compilation toolchain.

```bash
cd ~/linux_embedded_tasks/Task7
make
```

The build produces the kernel module `lcd_driver.ko`. The kernel build
directory, architecture and cross-compiler must match the target system.

## Test on the Raspberry Pi

Copy the compiled module to the Raspberry Pi and run the following commands
as root. If another version of the driver is already loaded, remove it
before loading the new module.

```bash
insmod lcd_driver.ko
dmesg | tail -n 30
ls -l /dev/lcd0
```

Write text to the display:

```bash
echo -n "Hello from Embedded Linux" > /dev/lcd0
echo -n 'Labas, ąčęėįšųū!' > /dev/lcd0
echo -n 'ĄąČčĘęĖė' > /dev/lcd0
echo -n 'Žž' > /dev/lcd0
```

Remove the module when it is no longer in use:

```bash
rmmod lcd_driver
```

## Implementation notes

- Uses the Linux I2C API to communicate with the LCD controller.
- Registers a misc character device that provides the `/dev/lcd0` interface.
- Copies user-space input into a bounded kernel buffer.
- Processes Lithuanian UTF-8 characters without splitting a multibyte
  character across display pages.
- Programs custom glyphs into CGRAM as needed for each page.
- Uses delayed work to switch between pages automatically.
- Synchronizes delayed work with incoming writes to avoid conflicting
  display updates.

## Limitations

- The LCD controller has only eight custom-character slots available at
  a time.
- If a page contains more distinct Lithuanian glyphs than can fit in those
  slots, excess characters fall back to their corresponding base ASCII
  letters.
- Hardware behavior may vary with other LCD modules, I2C configurations
  and kernel versions.
