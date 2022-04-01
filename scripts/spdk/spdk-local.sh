#!/usr/bin/env bash

max_jobs=6

echo "performing SPDK random read fio benchmark"

results_dir=results_spdk_randread_$(date +%Y%m%d_%H%M%S)
mkdir "$results_dir"

spdk_ioengine=/home/sheng/spdk/build/fio/spdk_bdev
spdk_conf=bdev.json
nvme_controller=Nvme0n1

for jobs in $(seq 1 $max_jobs)
do
  echo "================== start $jobs jobs ======================="

  fio --ioengine=$spdk_ioengine --thread=1 --direct=1 --group_reporting=1 \
  --bs=4k --rw=randread --time_based=1 --runtime=60 --norandommap=1 --numjobs=1 --iodepth=128 --cpus_allowed=0-15 \
  --spdk_json_conf=$spdk_conf --name=spdk_local --filename=$nvme_controller --output "$results_dir"/randread."$jobs".txt

  echo "==========================================================="
done

