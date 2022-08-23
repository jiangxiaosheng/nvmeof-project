#!/usr/bin/env bash

# load required Linux modules
sudo modprobe nvme_rdma
sudo modprobe nvmet
sudo modprobe nvmet-rdma

# create a new nvme subsystem called nvmet-test
sudo mkdir /sys/kernel/config/nvmet/subsystems/nvmet-rdma
cd /sys/kernel/config/nvmet/subsystems/nvmet-rdma

# enable the target can be accessed by any other client
echo 1 | sudo tee -a attr_allow_any_host > /dev/null

# create a nvme namespace
sudo mkdir namespaces/1
cd namespaces/1/

# set the path of the nvme device, default is /dev/nvme0n1 (in cloudlab m510 machines)
sudo echo -n /dev/nvme1n1 | sudo tee -a device_path > /dev/null
echo 1 | sudo tee -a enable > /dev/null


sudo mkdir /sys/kernel/config/nvmet/ports/1
cd /sys/kernel/config/nvmet/ports/1

# ip address of the target which is connected to clients
IP_ADDR=10.10.1.1
echo $IP_ADDR | sudo tee -a addr_traddr > /dev/null
echo rdma | sudo tee -a addr_trtype > /dev/null
echo 4420 | sudo tee -a addr_trsvcid > /dev/null
echo ipv4 | sudo tee -a addr_adrfam > /dev/null

sudo ln -s /sys/kernel/config/nvmet/subsystems/nvmet-rdma/ /sys/kernel/config/nvmet/ports/1/subsystems/nvmet-rdma

echo "nvme-rdma configuration ok"
