#!/bin/env bash

# reference list:
# https://blogs.oracle.com/linux/post/nvme-over-tcp
# https://github.com/camelboat/EECS_6897_Distributed_Storage_System_Project_Scripts/tree/master/setup_scripts/NVME_over_Fabrics

# load required Linux modules
sudo modprobe nvme
sudo modprobe nvme-tcp

# install the nvme-cli tool
sudo apt update & sudo apt install nvme-cli libaio-dev -y
nvme gen-hostnqn | sudo tee /etc/nvme/hostnqn > /dev/null

# ip address of the nvme target machine
TARGET_IP_ADDR=10.10.1.1

# discover the remote nvme target and connect to it
sudo nvme discover -t tcp -a $TARGET_IP_ADDR -s 4420
sudo nvme connect -t tcp -n nvmet-tcp -a $TARGET_IP_ADDR -s 4420

echo "nvme-tcp client setup ok"