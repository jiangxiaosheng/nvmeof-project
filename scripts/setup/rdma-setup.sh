#!/usr/bin/env bash

# supported by cloudlab m510 machines, using ubuntu distribution

# install required packages
sudo apt update
sudo apt-get install -y libmlx4-1 infiniband-diags ibutils ibverbs-utils rdmacm-utils perftest \
  rdma-core mstflint libibverbs-dev librdmacm-dev

# install the OFED driver
# first clean the unnecessary drivers from various providers
#sudo rm -rf /etc/libibverbs.d/*
# then download the package and install
#cd /mnt || exit   # recommend to mount a large-size device here
#wget mellanox.com/downloads/ofed/MLNX_OFED-4.9-4.0.8.0/MLNX_OFED_LINUX-4.9-4.0.8.0-ubuntu18.04-x86_64.tgz
#tar -xzvf MLNX_OFED_LINUX-4.9-4.0.8.0-ubuntu18.04-x86_64.tgz
#cd MLNX_OFED_LINUX-4.9-4.0.8.0-ubuntu18.04-x86_64/ || exit
#sudo ./mlnxofedinstall --without-fw-update --with-nvmf
# remove modules
#sudo rmmod rpcrdma
#sudo rmmod rdma_cm
# then restart service
#sudo /etc/init.d/openibd restart


# verify the rdma device
ibv_devinfo

# at last, it's suggested to reboot the machine
# sudo reboot
# sudo mount /dev/nvme0n1p4 /mnt