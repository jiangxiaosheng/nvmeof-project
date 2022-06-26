#!/usr/bin/env bash

# reference list:
# https://linuxhint.com/iscsi_storage_server_ubuntu/

shared_device=/dev/nvme0n1p4
target_name=iqn.2020-03.com.linuxhint:data
initiator_name=iqn.2020-03.com.linuxhint:initiator01
# client_username=linuxhint
# client_passwd=secret
conf_file=/etc/tgt/conf.d/iqn.2020-03.com.linuxhint.www.conf


sudo apt update
sudo apt install -y tgt
# auto-start on boot
sudo systemctl enable tgt

if [[ ! -e $conf_file ]]; then
    sudo touch $conf_file
fi
echo "<target $target_name>" >> $conf_file
echo "backing-store $shared_device" >> $conf_file
echo "initiator-name $initiator_name" >> $conf_file
# echo "incominguser $client_username $client_passwd" >> $conf_file
echo "</target>" >> $conf_file


sudo systemctl restart tgt

