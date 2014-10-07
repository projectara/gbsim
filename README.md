# Greybus Simulator (gbsim)

A tool which simulates an AP Bridge, SVC, and an arbitrary set
of Ara modules plugged into Greybus.

Provided under BSD license. See *LICENSE* for details.

## Install

The easiest path to get started is to run the kernel greybus support and
gbsim on the same machine. This is accomplished using the *dummy_hcd*
USB host+gadget driver in conjuction with the gadget configfs/functionfs
features.

Build the kernel greybus subsystem and ES1 USB driver:

```
cd /path/to/greybus
make -C /usr/src/linux-headers-foo M=$PWD
make -C /usr/src/linux-headers-foo M=$PWD modules_install
```

Modify the gsim Makefile *GBDIR* to point at the kernel greybus
(https://github.com/gregkh/greybus) directory as the simulator shares
headers with the kernel code.

`GBDIR = /path/to/greybus`

Optionally uncomment *CROSS_COMPILE* and set the variable appropriately
if you are cross compiling the simulator.

`CROSS_COMPILE = arm-linux-gnueabi-`

Build it:

gbsim depends on libusbg (https://github.com/libusbg/libusbg)
and libconfig (http://hyperrealm.com/libconfig/libconfig.html)

```
cd /path/to/gbsim
make
make install
```

## Run

Load up the greybus framework and ES1 USB driver:

```
modprobe greybus
modprobe es1-ap-usb
```

Now start the simulator:

```
modprobe configfs
mount -t configfs none /sys/kernel/config
modprobe libcomposite
modprobe dummy_hcd
gbsim /path/to
```

Where */path/to* is the base directory containing the
directory *hotplug-modules

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
found at https://github.com/ohporter/manifesto. Using the Simple I2C Module as
an example, a module can be inserted as follows:

`cp /foo/bar/simple-i2c-module.mnfb /path/to/hotplug-module/MID1-simple-i2c-module.mnfb`

The *MIDn* prefix is required to specify the Module ID that the module occupies,
indicating its location in the endoskeleton where *n* is a decimal integer greater
than 0 indicating the Module ID. 

After module insertion, gbsim will report:

```
[I] GBSIM: MID1-simple-i2c-module.mnfb module inserted
[D] GBSIM: SVC->AP hotplug event (plug) sent
```
