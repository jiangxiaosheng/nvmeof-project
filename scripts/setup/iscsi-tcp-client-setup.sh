#!/usr/bin/env bash

# reference list:
# https://linuxhint.com/iscsi_storage_server_ubuntu/

initiator_name=iqn.2020-03.com.linuxhint:initiator01
target_ip=10.10.1.3

sudo apt update
sudo apt install -y open-iscsi
# auto-start on boot
sudo systemctl enable iscsid
# Manual:
# 1. open /etc/iscsi/initiatorname.iscsi
#    set initiatorName to $initiator_name
#    close file`
# 2. open /etc/iscsi/iscsid.conf
#    uncomment node.startup = automatic
#    comment node.startup = manual

echo "scan the iSCSI server:"
sudo iscsiadm -m discovery -t sendtargets -p $target_ip
echo "(server is listed above)"
echo "login..."
sudo iscsiadm -m node -p $target_ip --login
# Manual:
# sudo mkfs.ext4 /dev/sda
# sudo mkdir /iscsi
# sudo mount /dev/sda /iscsi