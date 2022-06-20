#!/usr/bin/env bash

max_jobs=16

echo "performing random read fio benchmark"
name=randwrite-iouring

results_dir=results_"$name"_$(date +%Y%m%d_%H%M%S)
mkdir "$results_dir"


for jobs in $(seq 1 $max_jobs)
do
  echo "================== start $jobs jobs ======================="

  sudo fio --name=$name --cpus_allowed_policy=split --rw=randwrite --direct=1 --ioengine=io_uring \
  --size=1G --gtod_reduce=0 --thread --group_reporting --time_based --runtime=60 --bs=4k --iodepth=128 \
  --cpus_allowed=0-15 --numjobs="$jobs" --directory=/mnt --output="$results_dir"/"$name"."$jobs".txt

  echo "==========================================================="
done

