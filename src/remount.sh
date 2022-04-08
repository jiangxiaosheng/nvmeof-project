#!/usr/bin/bash

pushd ~/

sudo umount /mnt
sudo mount /dev/nvme2n1 /mnt

popd
