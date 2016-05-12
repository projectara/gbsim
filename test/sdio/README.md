# Greybus Simulator Testing for sdio

This document describes how to setup gbsim and the kernel to execute the
mmc_test test facility provided by the kernel.

This assumes that you follow the guidance in the README file in the top
directory of the gbsim tool and gbsim is running.

## Preparing for test

Copy an sdio example manifest file to the hotplug-modules directory, after this
a new sd card should appear in the system with the label "GBSIM":

```
# greybus endo0:1:1:1:1: card inserted now event
mmc0: mmc_rescan_try_freq: trying to init card at 400000 Hz
greybus endo0:1:1:1:1: no support for card's volts
mmc0: error -22 whilst initialising SDIO card
mmc0: new SD card at address 0001
blk_queue_max_segment_size: set to minimum 4096
mmcblk0: mmc0:0001 GBSIM 4.00 MiB
#
```

At this point you should have a new mmc device entry in sysfs in which
you can verify the sysfs provided values 'name' and 'oemid' of the
simulated card.

```
/sys/bus/mmc/devices/mmc0\:0001/
```

After that we need to mount debugfs if it is not already mounted and
unbind the device from the block layer so that the mmc_test driver can
bind to it

```
"mount debugfs"
mount -t debugfs debugfs /mnt

"unbinding from mmc block"
echo mmc0:0001 > /sys/bus/mmc/drivers/mmcblk/unbind

"binding to mmc test"
echo mmc0:0001 > /sys/bus/mmc/drivers/mmc_test/bind

The following should appear:
mmc_test mmc0:0001: Card claimed for testing.
```

After this a new entry will appear in debugfs path, in this case:
/mnt/mmc0/mmc0\:0001/, with the following entries:

```
state     status    test      testlist
```

You can then see the tests available reading from the testlist entry, and run
tests by writing to the test entry:

to run test 1 issue the following command:
```
echo 1 > test
```
To run all tests in sequence from 1 to 45 issue the following commands
and this will run in sequence all the tests specified in the mmc_test
driver:

```
for x in $(seq 45); do echo $x > test; done
```
This is the output of a full test over gbsim to sdio greybus:
hotplug_basedir /tmp/gbsim
file system registered
[I] GBSIM: USB gadget created
read descriptors
read strings
usb 1-1: new high-speed USB device number 2 using dummy_hcd
configfs-gadget gadget: high-speed config #1: config
[I] GBSIM: AP handshake complete
[I] GBSIM: simple-sdio-module.mnfb Interface inserted
# greybus endo0:1:1:1:1: card inserted now event
mmc0: mmc_rescan_try_freq: trying to init card at 400000 Hz
greybus endo0:1:1:1:1: no support for card's volts
mmc0: error -22 whilst initialising SDIO card
mmc0: new SD card at address 0001
blk_queue_max_segment_size: set to minimum 4096
mmcblk0: mmc0:0001 GBSIM 4.00 MiB
# mmc_test
mount debugfs
unbinding from mmc block
binding to mmc test
mmc_test mmc0:0001: Card claimed for testing.
# cd /mnt/mmc0/mmc0\:0001/
# for x in $(seq 45); do echo $x > test; done
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 1. Basic write (no data verification)...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 2. Basic read (no data verification)...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 3. Basic write (with data verification)...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 4. Basic read (with data verification)...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 5. Multi-block write...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 6. Multi-block read...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 7. Power of two block writes...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 8. Power of two block reads...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 9. Weird sized block writes...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 10. Weird sized block reads...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 11. Badly aligned write...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 12. Badly aligned read...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 13. Badly aligned multi-block write...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 14. Badly aligned multi-block read...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 15. Correct xfer_size at write (start failure)...
greybus endo0:1:1:1:1: send: wrong size received
mmc0: Result: UNSUPPORTED (by host)
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 16. Correct xfer_size at read (start failure)...
greybus endo0:1:1:1:1: recv: wrong size received
mmc0: Result: UNSUPPORTED (by host)
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 17. Correct xfer_size at write (midway failure)...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 18. Correct xfer_size at read (midway failure)...
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 19. Highmem write...
mmc0: Highmem not configured - test skipped
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 20. Highmem read...
mmc0: Highmem not configured - test skipped
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 21. Multi-block highmem write...
mmc0: Highmem not configured - test skipped
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 22. Multi-block highmem read...
mmc0: Highmem not configured - test skipped
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 23. Best-case read performance...
mmc0: Transfer of 1 x 1024 sectors (1 x 512 KiB) took 2.759678421 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 24. Best-case write performance...
mmc0: Transfer of 1 x 1024 sectors (1 x 512 KiB) took 2.759750673 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 25. Best-case read performance into scattered pages...
mmc0: Transfer of 1 x 1024 sectors (1 x 512 KiB) took 2.759010952 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 26. Best-case write performance from scattered pages...
mmc0: Transfer of 1 x 1024 sectors (1 x 512 KiB) took 2.759476770 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 27. Single read performance by transfer size...
mmc0: Transfer of 1 x 1 sectors (1 x 0.5 KiB) took 0.023976164 seconds (21 kB/s, 20 KiB/s, 41.70 IOPS, sg_len 1)
mmc0: Transfer of 1 x 2 sectors (1 x 1 KiB) took 0.031139524 seconds (32 kB/s, 32 KiB/s, 32.11 IOPS, sg_len 1)
mmc0: Transfer of 1 x 4 sectors (1 x 2 KiB) took 0.039754621 seconds (51 kB/s, 50 KiB/s, 25.15 IOPS, sg_len 2)
mmc0: Transfer of 1 x 8 sectors (1 x 4 KiB) took 0.047321284 seconds (86 kB/s, 84 KiB/s, 21.13 IOPS, sg_len 4)
mmc0: Transfer of 1 x 16 sectors (1 x 8 KiB) took 0.071100700 seconds (115 kB/s, 112 KiB/s, 14.06 IOPS, sg_len 8)
mmc0: Transfer of 1 x 32 sectors (1 x 16 KiB) took 0.111705469 seconds (146 kB/s, 143 KiB/s, 8.95 IOPS, sg_len 16)
mmc0: Transfer of 1 x 64 sectors (1 x 32 KiB) took 0.199884666 seconds (163 kB/s, 160 KiB/s, 5.00 IOPS, sg_len 32)
mmc0: Transfer of 1 x 128 sectors (1 x 64 KiB) took 0.367132394 seconds (178 kB/s, 174 KiB/s, 2.72 IOPS, sg_len 64)
mmc0: Transfer of 1 x 256 sectors (1 x 128 KiB) took 0.711821645 seconds (184 kB/s, 179 KiB/s, 1.40 IOPS, sg_len 128)
mmc0: Transfer of 1 x 512 sectors (1 x 256 KiB) took 1.391117819 seconds (188 kB/s, 184 KiB/s, 0.71 IOPS, sg_len 256)
mmc0: Transfer of 1 x 1024 sectors (1 x 512 KiB) took 2.759065076 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 28. Single write performance by transfer size...
mmc0: Transfer of 1 x 1 sectors (1 x 0.5 KiB) took 0.023916415 seconds (21 kB/s, 20 KiB/s, 41.81 IOPS, sg_len 1)
mmc0: Transfer of 1 x 2 sectors (1 x 1 KiB) took 0.031577823 seconds (32 kB/s, 31 KiB/s, 31.66 IOPS, sg_len 1)
mmc0: Transfer of 1 x 4 sectors (1 x 2 KiB) took 0.039731730 seconds (51 kB/s, 50 KiB/s, 25.16 IOPS, sg_len 2)
mmc0: Transfer of 1 x 8 sectors (1 x 4 KiB) took 0.047691379 seconds (85 kB/s, 83 KiB/s, 20.96 IOPS, sg_len 4)
mmc0: Transfer of 1 x 16 sectors (1 x 8 KiB) took 0.071736644 seconds (114 kB/s, 111 KiB/s, 13.93 IOPS, sg_len 8)
mmc0: Transfer of 1 x 32 sectors (1 x 16 KiB) took 0.111578106 seconds (146 kB/s, 143 KiB/s, 8.96 IOPS, sg_len 16)
mmc0: Transfer of 1 x 64 sectors (1 x 32 KiB) took 0.199694427 seconds (164 kB/s, 160 KiB/s, 5.00 IOPS, sg_len 32)
mmc0: Transfer of 1 x 128 sectors (1 x 64 KiB) took 0.367651756 seconds (178 kB/s, 174 KiB/s, 2.71 IOPS, sg_len 64)
mmc0: Transfer of 1 x 256 sectors (1 x 128 KiB) took 0.711714863 seconds (184 kB/s, 179 KiB/s, 1.40 IOPS, sg_len 128)
mmc0: Transfer of 1 x 512 sectors (1 x 256 KiB) took 1.391535108 seconds (188 kB/s, 183 KiB/s, 0.71 IOPS, sg_len 256)
mmc0: Transfer of 1 x 1024 sectors (1 x 512 KiB) took 2.758620144 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 29. Single trim performance by transfer size...
mmc0: Result: UNSUPPORTED (by card)
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 30. Consecutive read performance by transfer size...
mmc0: Transfer of 8192 x 1 sectors (8192 x 0.5 KiB) took 196.590034229 seconds (21 kB/s, 20 KiB/s, 41.67 IOPS, sg_len 1)
mmc0: Transfer of 4096 x 2 sectors (4096 x 1 KiB) took 131.059370176 seconds (32 kB/s, 31 KiB/s, 31.25 IOPS, sg_len 1)
mmc0: Transfer of 2048 x 4 sectors (2048 x 2 KiB) took 81.912058888 seconds (51 kB/s, 50 KiB/s, 25.00 IOPS, sg_len 2)
mmc0: Transfer of 1024 x 8 sectors (1024 x 4 KiB) took 49.146825891 seconds (85 kB/s, 83 KiB/s, 20.83 IOPS, sg_len 4)
mmc0: Transfer of 512 x 16 sectors (512 x 8 KiB) took 36.859679397 seconds (113 kB/s, 111 KiB/s, 13.89 IOPS, sg_len 8)
mmc0: Transfer of 256 x 32 sectors (256 x 16 KiB) took 28.668691996 seconds (146 kB/s, 142 KiB/s, 8.92 IOPS, sg_len 16)
mmc0: Transfer of 128 x 64 sectors (128 x 32 KiB) took 25.597472477 seconds (163 kB/s, 160 KiB/s, 5.00 IOPS, sg_len 32)
mmc0: Transfer of 64 x 128 sectors (64 x 64 KiB) took 23.549210438 seconds (178 kB/s, 173 KiB/s, 2.71 IOPS, sg_len 64)
mmc0: Transfer of 32 x 256 sectors (32 x 128 KiB) took 22.781347647 seconds (184 kB/s, 179 KiB/s, 1.40 IOPS, sg_len 128)
mmc0: Transfer of 16 x 512 sectors (16 x 256 KiB) took 22.269323272 seconds (188 kB/s, 183 KiB/s, 0.71 IOPS, sg_len 256)
mmc0: Transfer of 8 x 1024 sectors (8 x 512 KiB) took 22.077383457 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 31. Consecutive write performance by transfer size...
mmc0: Transfer of 8192 x 1 sectors (8192 x 0.5 KiB) took 196.589590762 seconds (21 kB/s, 20 KiB/s, 41.67 IOPS, sg_len 1)
mmc0: Transfer of 4096 x 2 sectors (4096 x 1 KiB) took 131.058792934 seconds (32 kB/s, 31 KiB/s, 31.25 IOPS, sg_len 1)
mmc0: Transfer of 2048 x 4 sectors (2048 x 2 KiB) took 81.912278029 seconds (51 kB/s, 50 KiB/s, 25.00 IOPS, sg_len 2)
mmc0: Transfer of 1024 x 8 sectors (1024 x 4 KiB) took 49.146183076 seconds (85 kB/s, 83 KiB/s, 20.83 IOPS, sg_len 4)
mmc0: Transfer of 512 x 16 sectors (512 x 8 KiB) took 36.860467978 seconds (113 kB/s, 111 KiB/s, 13.89 IOPS, sg_len 8)
mmc0: Transfer of 256 x 32 sectors (256 x 16 KiB) took 28.668683510 seconds (146 kB/s, 142 KiB/s, 8.92 IOPS, sg_len 16)
mmc0: Transfer of 128 x 64 sectors (128 x 32 KiB) took 25.597170917 seconds (163 kB/s, 160 KiB/s, 5.00 IOPS, sg_len 32)
mmc0: Transfer of 64 x 128 sectors (64 x 64 KiB) took 23.549595147 seconds (178 kB/s, 173 KiB/s, 2.71 IOPS, sg_len 64)
mmc0: Transfer of 32 x 256 sectors (32 x 128 KiB) took 22.781615940 seconds (184 kB/s, 179 KiB/s, 1.40 IOPS, sg_len 128)
mmc0: Transfer of 16 x 512 sectors (16 x 256 KiB) took 22.268914300 seconds (188 kB/s, 183 KiB/s, 0.71 IOPS, sg_len 256)
mmc0: Transfer of 8 x 1024 sectors (8 x 512 KiB) took 22.077776520 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 32. Consecutive trim performance by transfer size...
mmc0: Result: UNSUPPORTED (by card)
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 33. Random read performance by transfer size...
mmc0: Transfer of 417 x 1 sectors (417 x 0.5 KiB) took 10.007254293 seconds (21 kB/s, 20 KiB/s, 41.66 IOPS, sg_len 1)
mmc0: Transfer of 313 x 2 sectors (313 x 1 KiB) took 10.014359470 seconds (32 kB/s, 31 KiB/s, 31.25 IOPS, sg_len 1)
mmc0: Transfer of 251 x 4 sectors (251 x 2 KiB) took 10.038417620 seconds (51 kB/s, 50 KiB/s, 25.00 IOPS, sg_len 2)
mmc0: Transfer of 209 x 8 sectors (209 x 4 KiB) took 10.030409922 seconds (85 kB/s, 83 KiB/s, 20.83 IOPS, sg_len 4)
mmc0: Transfer of 139 x 16 sectors (139 x 8 KiB) took 10.006272748 seconds (113 kB/s, 111 KiB/s, 13.89 IOPS, sg_len 8)
mmc0: Transfer of 90 x 32 sectors (90 x 16 KiB) took 10.078525003 seconds (146 kB/s, 142 KiB/s, 8.92 IOPS, sg_len 16)
mmc0: Transfer of 51 x 64 sectors (51 x 32 KiB) took 10.198443691 seconds (163 kB/s, 160 KiB/s, 5.00 IOPS, sg_len 32)
mmc0: Transfer of 28 x 128 sectors (28 x 64 KiB) took 10.302212225 seconds (178 kB/s, 173 KiB/s, 2.71 IOPS, sg_len 64)
mmc0: Transfer of 15 x 256 sectors (15 x 128 KiB) took 10.678645348 seconds (184 kB/s, 179 KiB/s, 1.40 IOPS, sg_len 128)
mmc0: Transfer of 8 x 512 sectors (8 x 256 KiB) took 11.134898518 seconds (188 kB/s, 183 KiB/s, 0.71 IOPS, sg_len 256)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.037981749 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 34. Random write performance by transfer size...
mmc0: Transfer of 417 x 1 sectors (417 x 0.5 KiB) took 10.007067975 seconds (21 kB/s, 20 KiB/s, 41.67 IOPS, sg_len 1)
mmc0: Transfer of 313 x 2 sectors (313 x 1 KiB) took 10.015376068 seconds (32 kB/s, 31 KiB/s, 31.25 IOPS, sg_len 1)
mmc0: Transfer of 251 x 4 sectors (251 x 2 KiB) took 10.039151434 seconds (51 kB/s, 50 KiB/s, 25.00 IOPS, sg_len 2)
mmc0: Transfer of 209 x 8 sectors (209 x 4 KiB) took 10.031033084 seconds (85 kB/s, 83 KiB/s, 20.83 IOPS, sg_len 4)
mmc0: Transfer of 139 x 16 sectors (139 x 8 KiB) took 10.006949362 seconds (113 kB/s, 111 KiB/s, 13.89 IOPS, sg_len 8)
mmc0: Transfer of 90 x 32 sectors (90 x 16 KiB) took 10.079124067 seconds (146 kB/s, 142 KiB/s, 8.92 IOPS, sg_len 16)
mmc0: Transfer of 51 x 64 sectors (51 x 32 KiB) took 10.199213884 seconds (163 kB/s, 160 KiB/s, 5.00 IOPS, sg_len 32)
mmc0: Transfer of 28 x 128 sectors (28 x 64 KiB) took 10.302915476 seconds (178 kB/s, 173 KiB/s, 2.71 IOPS, sg_len 64)
mmc0: Transfer of 15 x 256 sectors (15 x 128 KiB) took 10.679150736 seconds (184 kB/s, 179 KiB/s, 1.40 IOPS, sg_len 128)
mmc0: Transfer of 8 x 512 sectors (8 x 256 KiB) took 11.134628141 seconds (188 kB/s, 183 KiB/s, 0.71 IOPS, sg_len 256)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.039019862 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 35. Large sequential read into scattered pages...
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519206194 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519202730 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518713374 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519087085 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519242306 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519041100 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519181807 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519060787 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518674438 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519209562 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518713812 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519151262 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519110981 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518883040 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518914856 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518631506 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518638569 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519226891 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 36. Large sequential write from scattered pages...
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519602864 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518918513 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518928209 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518980556 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518760220 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518984376 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518873262 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518880344 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519110987 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519166058 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518435654 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.519661824 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518814454 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518875691 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518971621 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518744006 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518645647 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 2 x 1024 sectors (2 x 512 KiB) took 5.518469151 seconds (190 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 37. Write performance with blocking req 4k to 4MB...
mmc0: Transfer of 512 x 8 sectors (512 x 4 KiB) took 24.573820630 seconds (85 kB/s, 83 KiB/s, 20.83 IOPS, sg_len 4)
mmc0: Transfer of 256 x 16 sectors (256 x 8 KiB) took 18.429735456 seconds (113 kB/s, 111 KiB/s, 13.89 IOPS, sg_len 8)
mmc0: Transfer of 128 x 32 sectors (128 x 16 KiB) took 14.333942566 seconds (146 kB/s, 142 KiB/s, 8.92 IOPS, sg_len 16)
mmc0: Transfer of 64 x 64 sectors (64 x 32 KiB) took 12.798314117 seconds (163 kB/s, 160 KiB/s, 5.00 IOPS, sg_len 32)
mmc0: Transfer of 32 x 128 sectors (32 x 64 KiB) took 11.774289979 seconds (178 kB/s, 173 KiB/s, 2.71 IOPS, sg_len 64)
mmc0: Transfer of 16 x 256 sectors (16 x 128 KiB) took 11.390025676 seconds (184 kB/s, 179 KiB/s, 1.40 IOPS, sg_len 128)
mmc0: Transfer of 8 x 512 sectors (8 x 256 KiB) took 11.134365455 seconds (188 kB/s, 183 KiB/s, 0.71 IOPS, sg_len 256)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038523163 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038255703 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038367192 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 38. Write performance with non-blocking req 4k to 4MB...
mmc0: Transfer of 512 x 8 sectors (512 x 4 KiB) took 24.573758524 seconds (85 kB/s, 83 KiB/s, 20.83 IOPS, sg_len 4)
mmc0: Transfer of 256 x 16 sectors (256 x 8 KiB) took 18.429507687 seconds (113 kB/s, 111 KiB/s, 13.89 IOPS, sg_len 8)
mmc0: Transfer of 128 x 32 sectors (128 x 16 KiB) took 14.334247547 seconds (146 kB/s, 142 KiB/s, 8.92 IOPS, sg_len 16)
mmc0: Transfer of 64 x 64 sectors (64 x 32 KiB) took 12.798108792 seconds (163 kB/s, 160 KiB/s, 5.00 IOPS, sg_len 32)
mmc0: Transfer of 32 x 128 sectors (32 x 64 KiB) took 11.774403759 seconds (178 kB/s, 173 KiB/s, 2.71 IOPS, sg_len 64)
mmc0: Transfer of 16 x 256 sectors (16 x 128 KiB) took 11.390341125 seconds (184 kB/s, 179 KiB/s, 1.40 IOPS, sg_len 128)
mmc0: Transfer of 8 x 512 sectors (8 x 256 KiB) took 11.134310204 seconds (188 kB/s, 183 KiB/s, 0.71 IOPS, sg_len 256)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038192484 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038339085 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038518659 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 39. Read performance with blocking req 4k to 4MB...
mmc0: Transfer of 512 x 8 sectors (512 x 4 KiB) took 24.573442695 seconds (85 kB/s, 83 KiB/s, 20.83 IOPS, sg_len 4)
mmc0: Transfer of 256 x 16 sectors (256 x 8 KiB) took 18.430345159 seconds (113 kB/s, 111 KiB/s, 13.89 IOPS, sg_len 8)
mmc0: Transfer of 128 x 32 sectors (128 x 16 KiB) took 14.334052601 seconds (146 kB/s, 142 KiB/s, 8.92 IOPS, sg_len 16)
mmc0: Transfer of 64 x 64 sectors (64 x 32 KiB) took 12.798078084 seconds (163 kB/s, 160 KiB/s, 5.00 IOPS, sg_len 32)
mmc0: Transfer of 32 x 128 sectors (32 x 64 KiB) took 11.774206006 seconds (178 kB/s, 173 KiB/s, 2.71 IOPS, sg_len 64)
mmc0: Transfer of 16 x 256 sectors (16 x 128 KiB) took 11.390498927 seconds (184 kB/s, 179 KiB/s, 1.40 IOPS, sg_len 128)
mmc0: Transfer of 8 x 512 sectors (8 x 256 KiB) took 11.134333943 seconds (188 kB/s, 183 KiB/s, 0.71 IOPS, sg_len 256)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038267946 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038216446 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038342836 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 40. Read performance with non-blocking req 4k to 4MB...
mmc0: Transfer of 512 x 8 sectors (512 x 4 KiB) took 24.573559521 seconds (85 kB/s, 83 KiB/s, 20.83 IOPS, sg_len 4)
mmc0: Transfer of 256 x 16 sectors (256 x 8 KiB) took 18.429783225 seconds (113 kB/s, 111 KiB/s, 13.89 IOPS, sg_len 8)
mmc0: Transfer of 128 x 32 sectors (128 x 16 KiB) took 14.334093356 seconds (146 kB/s, 142 KiB/s, 8.92 IOPS, sg_len 16)
mmc0: Transfer of 64 x 64 sectors (64 x 32 KiB) took 12.798174783 seconds (163 kB/s, 160 KiB/s, 5.00 IOPS, sg_len 32)
mmc0: Transfer of 32 x 128 sectors (32 x 64 KiB) took 11.774170852 seconds (178 kB/s, 173 KiB/s, 2.71 IOPS, sg_len 64)
mmc0: Transfer of 16 x 256 sectors (16 x 128 KiB) took 11.390380675 seconds (184 kB/s, 179 KiB/s, 1.40 IOPS, sg_len 128)
mmc0: Transfer of 8 x 512 sectors (8 x 256 KiB) took 11.134484214 seconds (188 kB/s, 183 KiB/s, 0.71 IOPS, sg_len 256)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038566722 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038356248 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038279477 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 41. Write performance blocking req 1 to 512 sg elems...
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038959047 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038028708 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038927674 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038213687 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038406038 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038064981 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038958729 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038178968 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 42. Write performance non-blocking req 1 to 512 sg elems...
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038836408 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038552883 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038938308 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038463466 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038203314 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038137155 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038176102 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.037781528 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 43. Read performance blocking req 1 to 512 sg elems...
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038850096 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038404461 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038100681 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038799616 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038254885 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038208717 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038162378 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038322947 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 44. Read performance non-blocking req 1 to 512 sg elems...
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038564004 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038451826 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038294688 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038322338 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038000377 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038720818 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038313350 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Transfer of 4 x 1024 sectors (4 x 512 KiB) took 11.038932451 seconds (189 kB/s, 185 KiB/s, 0.36 IOPS, sg_len 512)
mmc0: Result: OK
mmc0: Tests completed.
mmc0: Starting tests of card mmc0:0001...
mmc0: Test case 45. eMMC hardware reset...
mmc0: Result: UNSUPPORTED (by card)
mmc0: Tests completed.
#
