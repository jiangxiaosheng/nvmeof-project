#!/usr/bin/bash

set -e

output_base_dir="results/fio"
if [ ! -z "$OUTDIR" ]; then
	output_base_dir=$OUTDIR
fi
test_file="/mnt/fiotest"

function append_to_log() {
	local msg="$1"
	local log_file="$2"
	if [ ! -f $log_file ]; then
		if [ ! -d $output_base_dir ]; then
			mkdir -p $output_base_dir
		fi
		touch $log_file
	fi

	echo "$msg" | tee -a $log_file
}

function clear_testfile() {
	if [ -f $test_file ]; then
		sudo rm $test_file
	fi
}


function sequential_write() {
	local base_args="--cpus_allowed_policy=split --name=seq_write --rw=write --direct=1 "
	base_args+="--size=1G --gtod_reduce=0 --thread --group_reporting --time_based --runtime=20 --iodepth=128 "
	base_args+="--cpus_allowed=0-15 --filename=$test_file"
	local result_file

	clear_testfile
	result_file="seq_write_iouring.txt"
	result_file=$output_base_dir/$result_file
	> $result_file
	for thread in {1..6}
	do
		append_to_log "============== sequential write with io_uring =============" "$result_file"
		for bs in "4k" "16k" "64k" "512k" "1m"
		do
			printf "[bs=$bs]\n" | tee -a $result_file
			args="$base_args --ioengine=io_uring --numjobs=$thread --bs=$bs"
			sudo fio $args | tee -a "$result_file"
			printf "\n\n" | tee -a "$result_file"
		done
	done

	clear_testfile
	result_file="seq_write_sync.txt"
	result_file=$output_base_dir/$result_file
	> $result_file
	for thread in {1..6}
	do
		append_to_log "============== sequential write with sync io =============" "$result_file"
		for bs in "4k" "16k" "64k" "512k" "1m"
		do
			printf "[bs=$bs]\n" | tee -a $result_file
			args="$base_args --ioengine=sync --numjobs=$thread --bs=$bs"
			sudo fio $args | tee -a "$result_file"
			printf "\n\n" | tee -a "$result_file"
		done
	done
}


function random_write() {
	local base_args="--cpus_allowed_policy=split --name=rand_write --rw=randwrite --direct=1 "
	base_args+="--size=1G --gtod_reduce=0 --thread --group_reporting --time_based --runtime=20 --iodepth=128 "
	base_args+="--cpus_allowed=0-15 --filename=$test_file"
	local result_file

	clear_testfile
	result_file="rand_write_iouring.txt"
	result_file=$output_base_dir/$result_file
	> $result_file
	for thread in {1..6}
	do
		append_to_log "============== random write with io_uring =============" "$result_file"
		for bs in "4k" "16k" "64k" "512k" "1m"
		do
			printf "[bs=$bs]\n" | tee -a $result_file
			args="$base_args --ioengine=io_uring --numjobs=$thread --bs=$bs"
			sudo fio $args | tee -a "$result_file"
			printf "\n\n" | tee -a "$result_file"
		done
	done

	clear_testfile
	result_file="rand_write_sync.txt"
	result_file=$output_base_dir/$result_file
	> $result_file
	for thread in {1..6}
	do
		append_to_log "============== random write with sync io =============" "$result_file"
		for bs in "4k" "16k" "64k" "512k" "1m"
		do
			printf "[bs=$bs]\n" | tee -a $result_file
			args="$base_args --ioengine=sync --numjobs=$thread --bs=$bs"
			sudo fio $args | tee -a "$result_file"
			printf "\n\n" | tee -a "$result_file"
		done
	done
}

function sequential_read() {
	local base_args="--cpus_allowed_policy=split --name=seq_read --rw=read --direct=1 "
	base_args+="--size=1G --gtod_reduce=0 --thread --group_reporting --time_based --runtime=20 --iodepth=128 "
	base_args+="--cpus_allowed=0-15 --filename=$test_file"
	local result_file

	clear_testfile
	result_file="seq_read_iouring.txt"
	result_file=$output_base_dir/$result_file
	> $result_file
	for thread in {1..6}
	do
		append_to_log "============== sequential read with io_uring =============" "$result_file"
		for bs in "4k" "16k" "64k" "512k" "1m"
		do
			printf "[bs=$bs]\n" | tee -a $result_file
			args="$base_args --ioengine=io_uring --numjobs=$thread --bs=$bs"
			sudo fio $args | tee -a "$result_file"
			printf "\n\n" | tee -a "$result_file"
		done
	done

	clear_testfile
	result_file="seq_read_sync.txt"
	result_file=$output_base_dir/$result_file
	> $result_file
	for thread in {1..6}
	do
		append_to_log "============== sequential read with sync io =============" "$result_file"
		for bs in "4k" "16k" "64k" "512k" "1m"
		do
			printf "[bs=$bs]\n" | tee -a $result_file
			args="$base_args --ioengine=sync --numjobs=$thread --bs=$bs"
			sudo fio $args | tee -a "$result_file"
			printf "\n\n" | tee -a "$result_file"
		done
	done
}

function random_read() {
	local base_args="--cpus_allowed_policy=split --name=rand_read --rw=randread --direct=1 "
	base_args+="--size=1G --gtod_reduce=0 --thread --group_reporting --time_based --runtime=20 --iodepth=128 "
	base_args+="--cpus_allowed=0-15 --filename=$test_file"
	local result_file

	clear_testfile
	result_file="rand_read_iouring.txt"
	result_file=$output_base_dir/$result_file
	> $result_file
	for thread in {1..6}
	do
		append_to_log "============== random read with io_uring =============" "$result_file"
		for bs in "4k" "16k" "64k" "512k" "1m"
		do
			printf "[bs=$bs]\n" | tee -a $result_file
			args="$base_args --ioengine=io_uring --numjobs=$thread --bs=$bs"
			sudo fio $args | tee -a "$result_file"
			printf "\n\n" | tee -a "$result_file"
		done
	done

	clear_testfile
	result_file="rand_read_sync.txt"
	result_file=$output_base_dir/$result_file
	> $result_file
	for thread in {1..6}
	do
		append_to_log "============== random read with sync io =============" "$result_file"
		for bs in "4k" "16k" "64k" "512k" "1m"
		do
			printf "[bs=$bs]\n" | tee -a $result_file
			args="$base_args --ioengine=sync --numjobs=$thread --bs=$bs"
			sudo fio $args | tee -a "$result_file"
			printf "\n\n" | tee -a "$result_file"
		done
	done
}

function sequential_rw() {
	local base_args="--cpus_allowed_policy=split --name=seq_rw --rw=rw --direct=1 "
	base_args+="--size=1G --gtod_reduce=0 --thread --group_reporting --time_based --runtime=20 --iodepth=128 "
	base_args+="--cpus_allowed=0-15 --filename=$test_file"
	local result_file

	clear_testfile
	result_file="seq_rw_iouring.txt"
	result_file=$output_base_dir/$result_file
	> $result_file
	for thread in {1..6}
	do
		append_to_log "============== sequential read & write with io_uring =============" "$result_file"
		for bs in "4k" "16k" "64k" "512k" "1m"
		do
			printf "[bs=$bs]\n" | tee -a $result_file
			args="$base_args --ioengine=io_uring --numjobs=$thread --bs=$bs"
			sudo fio $args | tee -a "$result_file"
			printf "\n\n" | tee -a "$result_file"
		done
	done

	clear_testfile
	result_file="seq_rw_sync.txt"
	result_file=$output_base_dir/$result_file
	> $result_file
	for thread in {1..6}
	do
		append_to_log "============== sequential read & write with sync io =============" "$result_file"
		for bs in "4k" "16k" "64k" "512k" "1m"
		do
			printf "[bs=$bs]\n" | tee -a $result_file
			args="$base_args --ioengine=sync --numjobs=$thread --bs=$bs"
			sudo fio $args | tee -a "$result_file"
			printf "\n\n" | tee -a "$result_file"
		done
	done
}

function random_rw() {
	local base_args="--cpus_allowed_policy=split --name=rand_rw --rw=randrw --direct=1 "
	base_args+="--size=1G --gtod_reduce=0 --thread --group_reporting --time_based --runtime=20 --iodepth=128 "
	base_args+="--cpus_allowed=0-15 --filename=$test_file"
	local result_file

	clear_testfile
	result_file="rand_rw_iouring.txt"
	result_file=$output_base_dir/$result_file
	> $result_file
	for thread in {1..6}
	do
		append_to_log "============== random read & write with io_uring =============" "$result_file"
		for bs in "4k" "16k" "64k" "512k" "1m"
		do
			printf "[bs=$bs]\n" | tee -a $result_file
			args="$base_args --ioengine=io_uring --numjobs=$thread --bs=$bs"
			sudo fio $args | tee -a "$result_file"
			printf "\n\n" | tee -a "$result_file"
		done
	done

	clear_testfile
	result_file="rand_rw_sync.txt"
	result_file=$output_base_dir/$result_file
	> $result_file
	for thread in {1..6}
	do
		append_to_log "============== random read & write with sync io =============" "$result_file"
		for bs in "4k" "16k" "64k" "512k" "1m"
		do
			printf "[bs=$bs]\n" | tee -a $result_file
			args="$base_args --ioengine=sync --numjobs=$thread --bs=$bs"
			sudo fio $args | tee -a "$result_file"
			printf "\n\n" | tee -a "$result_file"
		done
	done
}

function usage() {
	options="[seqw|randw|seqr|randr|seqrw|randrw|all]"
	echo "auto run fio tests for different workloads"
	echo "usage: $(basename $1) $options"
	echo "seqw              sequential write"
	echo "randw             random write"
	echo "seqr              sequential read"
	echo "randr             random read"
	echo "seqrw             sequential read and write"
	echo "randrw            random read and write"
	echo "all               run all kinds of tests"
	echo "OUTDIR            env: specify the output dir, default is ./results/fio"
}

if [ $# -ne 1 ]; then
	usage $0
	exit 0
fi

case $1 in
	"seqw") sequential_write ;;
	"randw") random_write ;;
	"seqr") sequential_read ;;
	"randr") random_read ;;
	"seqrw") sequential_rw ;;
	"randrw") random_rw ;;
	"all") 
		sequential_write
		random_write
		sequential_read
		random_read
		sequential_rw
		random_rw
		;;
esac

