<!-- This file uses Github Flavored Markdown (GFM) format. -->

# Greybus Simulator (gbsim)

A tool which simulates an AP Bridge, SVC, and an arbitrary set
of Ara modules plugged into Greybus.

Provided under BSD license. See *LICENSE* for details.

## Quick Start

This code depends on header files present in the "greybus" source
directory.  The location of this directory is defined in the *GBDIR*
environment variable.

To just build gbsim, do this:
```
export GBDIR=".../path/to/greybus"
./autogen.sh
./configure
make
```

## Install

The easiest path to get started is to run the kernel greybus support and
gbsim on the same machine. This is accomplished using the *dummy_hcd*
USB host+gadget driver in conjuction with the gadget configfs/functionfs
features.

Set the environment variable *GBDIR* to point at the kernel greybus
(https://github.com/gregkh/greybus) directory as the simulator shares
headers with the kernel code.

`export GBDIR=".../path/to/greybus"`

Build the kernel greybus subsystem, including the host driver and
related protocol drivers.
```
cd "${GBDIR}"
make -C /usr/src/linux-headers-foo M=$PWD
make -C /usr/src/linux-headers-foo M=$PWD modules_install
```

Build it:

gbsim has the following dependencies:

* libusbg (https://github.com/libusbg/libusbg)
* libconfig (http://hyperrealm.com/libconfig/libconfig.html)
* libsoc (https://github.com/jackmitch/libsoc)

It also assumes the *GBDIR* environment variable has been set.
```
cd /path/to/gbsim
./autogen.sh
./configure
make
make install
```

If you would like to cross-compile the simulator, you can optionally
specify the prefix used for compilation tools at configure time.
For example:
```
./configure --host=arm-linux-gnueabi
```

If your build environment only supplies the legacy (pre-V2) USB
descriptor format, you can indicate that when configuring as well:
```
./configure --enable-legacy-descriptors
```
(If you get errors about FUNCTIONFS_DESCRIPTORS_MAGIC_V2 not
being defined, you'll need this.)

## Run

Load up the greybus framework and ES1 USB driver:

```
modprobe greybus
modprobe gb-es1
modprobe gb-phy
```

Now start the simulator:

```
modprobe configfs
mount -t configfs none /sys/kernel/config
modprobe libcomposite
modprobe dummy_hcd
gbsim -h /path/to -v
```

Where */path/to* is the base directory containing the
directory *hotplug-modules*

gbsim supports the following option flags:

* -b: enable the BeagleBone Black hardware backend
* -h: hotplug base directory
* -i: i2c adapter (if BBB hardware backend is enabled)
* -v: enable verbose output

### Using the simulator

After running output should appear as follows:

```
[I] GBSIM: gbsim gadget created
[D] GBSIM: event BIND,0
[D] GBSIM: event ENABLE,2
[D] GBSIM: Start SVC/CPort endpoints
[D] GBSIM: SVC->AP handshake sent
[D] GBSIM: event SETUP,4
[D] GBSIM: AP->AP Bridge setup message:
[D] GBSIM:   bRequestType = 41
[D] GBSIM:   bRequest     = 01
[D] GBSIM:   wValue       = 0000
[D] GBSIM:   wIndex       = 0000
[D] GBSIM:   wLength      = 000b
[D] GBSIM: AP->SVC message:
  00 00 03 00 00 01 01 00 00 00 00 
[I] GBSIM: AP handshake complete
```

This indicates that the simulated AP Bridge device was enumerated by the host,
a handshake message was sent to the AP, and another handshake message received
from the AP indicating that the handshake process has completed.

At this point, it's possible to hot plug/unplug modules by simply copying or
removing a conformant manifest blob file in the /path/to/hotplug-module
directory. Manifest blob files can be created using the Manifesto tool
found at https://github.com/projectara/manifesto. Using the Simple I2C Module as
an example, a module can be inserted as follows:

`cp /foo/bar/simple-i2c-module.mnfb /path/to/hotplug-module/simple-i2c-module.mnfb`

After module insertion, gbsim will report:

```
[I] GBSIM: simple-i2c-module.mnfb module inserted
[D] GBSIM: SVC->AP hotplug event (plug) sent
```
