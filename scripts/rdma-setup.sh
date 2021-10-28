#!/usr/bin/env bash

# supported by cloudlab m510 machines, using ubuntu distribution

# install required packages
sudo apt update
sudo apt-get install libmlx4-1 infiniband-diags ibutils ibverbs-utils rdmacm-utils perftest \
  rdma-core mstflint

# verify the PCIe device is connected to the adapter card
lspci | grep Mellanox
