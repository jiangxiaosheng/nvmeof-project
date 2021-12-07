#!/usr/bin/env bash

# reference list:
# https://blogs.oracle.com/linux/post/nvme-over-tcp
# https://github.com/camelboat/EECS_6897_Distributed_Storage_System_Project_Scripts/tree/master/setup_scripts/NVME_over_Fabrics

# format the device partition we want clients to use
# sudo mkfs.ext4 /dev/nvme0n1p4

# nvme over tcp is fully supported since Linux kernel 5.0
# so typically we should use at least ubuntu 19.04 or cent-os 8
# otherwise the nvme_tcp module cannot be found

# load required Linux modules
sudo modprobe nvme_tcp
sudo modprobe nvmet
sudo modprobe nvmet-tcp

# create a new nvme subsystem called nvmet-test
sudo mkdir /sys/kernel/config/nvmet/subsystems/nvmet-test
cd /sys/kernel/config/nvmet/subsystems/nvmet-test

# enable the target can be accessed by any other client
echo 1 | sudo tee -a attr_allow_any_host > /dev/null

# create a nvme namespace
sudo mkdir namespaces/1
cd namespaces/1/

# set the path of the nvme device, default is /dev/nvme0n1 (in cloudlab m510 machines)
sudo echo -n /dev/nvme0n1 | sudo tee -a device_path > /dev/null
echo 1 | sudo tee -a enable > /dev/null


sudo mkdir /sys/kernel/config/nvmet/ports/1
cd /sys/kernel/config/nvmet/ports/1

# ip address of the target which is connected to clients
IP_ADDR=10.10.1.1
echo $IP_ADDR | sudo tee -a addr_traddr > /dev/null
echo tcp | sudo tee -a addr_trtype > /dev/null
echo 4420 | sudo tee -a addr_trsvcid > /dev/null
echo ipv4 | sudo tee -a addr_adrfam > /dev/null

sudo ln -s /sys/kernel/config/nvmet/subsystems/nvmet-test/ /sys/kernel/config/nvmet/ports/1/subsystems/nvmet-t

echo "nvme-tcp configuration ok"