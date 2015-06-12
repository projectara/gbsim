Setting up BeagleBone* (BBB/BBW) to hook greybus into hardware UARTs.

BBB1 will be used to denote the beaglebone running greybus and gbsim.
This beagle presents /dev/ttyGBO and /dev/ttyGB1 which are hooked up
via gbsim to /dev/ttyO1 and /dev/ttyO2 respectively.

BBB2 will be used to denote the second bealgebone which simulates the
Module end of the greybus connection. BBB2 and BBB1 are wired together via
/dev/ttyO1 and /dev/ttyO2 respectively i.e BBB1:/dev/ttyO1 <=>
BBB2:/dev/ttyO1 and BBB1:/dev/ttyO2 <=> BBB2:/dev/ttyO2. Any machine with
two serial ports could be used in place of BBB2 provided that machine
supports UART voltage levels (+/- 5v) as opposed to RS232 voltage levels
(+/- 20v).

1. Hardware setup

BBB1/BBB2:
uart1: P9.24 tx, P9.26 rx this UART will be /dev/ttyO1
uart2: P9.21 tx, P9.22 rx this UART will be /dev/ttyO2

Cross wire BBB1 to BBB2:
BBB1:P9.24:tx -> BBB2:P9.26:rx
BBB1:P9.26:rx <- BBB2:P9.24:tx
BBB1:P9.21:tx -> BBB2:P9.22:rx
BBB1:P9.22:rx <- BBB1:P9.21:tx

2. Kernel DTS

Kernel DTS files to enable UART1 and UART2 are provided :
https://github.com/beagleboard/linux.git

In test/uart/test/uart/am335x-boneblack-tty-ara.dts

Copy this file to linux/arch/arm/boot/dts/

You need to turn this .dts file into a .dtb and have your bootloader pass
this to Linux.

Once done correctly you should see the following in your kernel log.

44e09000.serial: ttyO0 at MMIO 0x44e09000
48022000.serial: ttyO1 at MMIO 0x48022000
48024000.serial: ttyO2 at MMIO 0x48024000

With corresponding entries in /dev/ttyO0, /dev/ttyO1 and /dev/ttyO2

3. Running the tests

Start gbsim with the following command line amending the -h as necessary:

bin/gbsim -b -u 1 -U 2 -h /home/deckard/gbsim

-h = Hotplug base directory the directory where 'hotplug-module' is found.
-b = beaglebone backend
-u = UART base port in this case ttyO1
-U = UART device count of two - /dev/ttyO1, /dev/ttyO2

gbsim will monitor and connect /dev/ttyO1 and /dev/ttyO2 to /dev/ttyGB0 and
/dev/ttyGB1 respectively

Copy the UART manifest file into hotplug-module i.e.
/home/deckard/gbsim/hotplug-module

4. Running getty

4.1 Getty on ttyGBO
On the beaglebone acting as the greybus master run the following command.
BBB1: /sbin/getty -L ttyGB0 115200 vt102

On the beaglebone acting as the UART endpoint you can now run your
favourite serial program.

BBB2:BAUD=115200, parity=none, data-bits=8, stop-bits=1.

You should get a Linux login prompt.

4.2 Linux login via ttyGB1
We can also reverse this test by running the getty on BBB2
BBB2: /sbin/getty -L ttyO2 115200 vt102

On the greybus end we then run the same serial application such as minicom
or kermit.
BBB1: BAUD=115200, parity=none, data-bits=8, stop-bits=1

4.3 Simultaneous getty on ttyGBO and Linux login on ttyGB1
Run test 4.1 and 4.2 concurrently running the 'top' command inside of each
shell.
Leave overnight and confirm data is still flowing.

5. Large binary transfer
To verify a transfer of a large amount of binary data we will use the
mipi_UniPro_specification_v1-6.pdf, but any large binary file will suffice.

BBB1:
Configure /dev/ttyGB0

stty -F /dev/ttyGB0 raw
stty -F /dev/ttyGB0 -icrnl -igncr -inlcr -isig -icanon -iexten -echo -echoe -echok -echoctl -echoke
stty -F /dev/ttyGB0 -opost -onlcr -ocrnl
stty -F /dev/ttyGB0 -brkint -imaxbel
stty -F /dev/ttyGB0 115200
stty -F /dev/ttyGB0

Use cat to receive the binary data
cat < /dev/ttyGB0 > mipi.pdf

BBB2:
Configure /dev/ttyO1
stty -F /dev/ttyO1 raw
stty -F /dev/ttyO1 -icrnl -igncr -inlcr -isig -icanon -iexten -echo -echoe -echok -echoctl -echoke
stty -F /dev/ttyO1 -opost -onlcr -ocrnl
stty -F /dev/ttyO1 -brkint -imaxbel
stty -F /dev/ttyO1 115200
stty -F /dev/ttyO1

Use cat to send the binary data
cat mipi_UniPro_specification_v1-6.pdf > /dev/ttyO1

Once cat finishes on BBB2 interrupt cat on BBB1 and run md5sum to verify.

BBB1: md5sum mipi.pdf 94d75720af410f1414f95e79eed5544b
BBB2: md5sum mipi_UniPro_specification_v1-6.pdf 94d75720af410f1414f95e79eed5544b

6. Large concurrent binary data transfer
For this test we will run at a lower BAUD rate so that gbsim won't miss any
data during the transfer.

BBB1 port setup:
Configure ttyGB0
stty -F /dev/ttyGB0 raw
stty -F /dev/ttyGB0 -icrnl -igncr -inlcr -isig -icanon -iexten -echo -echoe -echok -echoctl -echoke
stty -F /dev/ttyGB0 -opost -onlcr -ocrnl
stty -F /dev/ttyGB0 -brkint -imaxbel
stty -F /dev/ttyGB0 115200
stty -F /dev/ttyGB0

Configure ttyGB1
stty -F /dev/ttyGB1 raw
stty -F /dev/ttyGB1 -icrnl -igncr -inlcr -isig -icanon -iexten -echo -echoe -echok -echoctl -echoke
stty -F /dev/ttyGB1 -opost -onlcr -ocrnl
stty -F /dev/ttyGB1 -brkint -imaxbel
stty -F /dev/ttyGB1 38400
stty -F /dev/ttyGB1

BBB2 port setup:
Configure ttyO1
stty -F /dev/ttyO1 raw
stty -F /dev/ttyO1 -icrnl -igncr -inlcr -isig -icanon -iexten -echo -echoe -echok -echoctl -echoke
stty -F /dev/ttyO1 -opost -onlcr -ocrnl
stty -F /dev/ttyO1 -brkint -imaxbel
stty -F /dev/ttyO1 115200
stty -F /dev/ttyO1

Configure ttyO2
stty -F /dev/ttyO2 raw
stty -F /dev/ttyO2 -icrnl -igncr -inlcr -isig -icanon -iexten -echo -echoe -echok -echoctl -echoke
stty -F /dev/ttyO2 -opost -onlcr -ocrnl
stty -F /dev/ttyO2 -brkint -imaxbel
stty -F /dev/ttyO2 38400
stty -F /dev/ttyO2
