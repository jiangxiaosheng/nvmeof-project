#!/usr/bin/env bash

max_jobs=8

echo "performing random read fio benchmark"

results_dir=results_randread_$(date +%Y%m%d_%H%M%S)
mkdir "$results_dir"

for jobs in $(seq 1 $max_jobs)
do
  echo "================== start $jobs jobs ======================="

  fio --cpus_allowed_policy=split --rw=randread --direct=1 --ioengine=libaio \
  --size=1G --gtod_reduce=0 --thread --group_reporting --time_based --runtime=60 --bs=4k --iodepth=128 \
  --cpus_allowed=0-15 --numjobs="$jobs" --name=random_read --filename=/dev/nvme0n1 --output "$results_dir"/randread."$jobs".txt

  echo "==========================================================="
done

