#!/usr/bin/env bash

# this script runs in the target side

device=nvme0n1

# default time window is 60s
sudo blktrace -d /dev/$device -w 60

blkparse -i $device -d $device.blktrace.bin -O

btt -i $device.blktrace.bin -o $device
