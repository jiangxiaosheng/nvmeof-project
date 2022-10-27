#!/usr/bin/python

import os
from subprocess import Popen, run
import signal
import re
from time import sleep

benchmark_bin_path = '/users/sjiang/nvmeof-project/src/build/benchmark'
cpu_processes = [
	'benchmark',
	'kworker/.+',
	'io_wqe_worker.+'
]

def parse_log():
	log = open('log.txt', 'r')
	content = log.readlines()
	iters = 0
	i = 0
	skip = True
	total_cpu = 0.0
	while i < len(content):
		line = content[i]
		if line.strip().startswith('PID'):
			# skip the first top as metrics are not stable now
			if skip:
				skip = False
			else:
				iters += 1
				i += 1
				while i < len(content):
					if content[i].strip() == '':
						break
					line = content[i]
					# do some processing
					cols = line.split()
					process = cols[-1]
					cpu = cols[-4]
					for pattern in cpu_processes:
						if re.match(pattern, process) is not None:
							total_cpu += float(cpu)
					i += 1

		i += 1
	return total_cpu / iters


def nvmeof():
	# NVMe/RDMA
	args = [benchmark_bin_path, '-file', '/nvme/test', '-runtime', '30', '-bs', '4', '-cores', '1']
	cmd = ' '.join(args)
	result_file = open('/users/sjiang/nvmeof-project/src/results/rubble/result-nvmeof.txt', 'w+')
	for bs in [4, 16, 64, 512, 1024, 17408]:
		args[-3] = str(bs)
		print('[bs=' + str(bs) + 'kB]')
		for cores in [1, 2, 3]:
			print(' [cores=' + str(cores) + ']')
			args[-1] = str(cores)

			total_thru = 0.0
			total_cpu = 0.0
			for i in range(3):
				out = Popen('top -b > log.txt', shell=True, preexec_fn=os.setsid)
				# start the worker process on the peer
				peer_worker = Popen(f'ssh 10.10.1.2 "{cmd}"', stdout=open('/dev/null'), shell=True)
				benchmark = run(args, capture_output=True, text=True)

				os.killpg(os.getpgid(out.pid), signal.SIGTERM)
				peer_worker.terminate()

				last_record = benchmark.stdout.splitlines()[-1]
				thru = re.search(r'(\d+\.\d+) (MB/s)', last_record).groups()
				total_thru += float(thru[0])
				avg_cpu = parse_log()
				total_cpu += avg_cpu
				# clean the env
				os.system('pkill benchmark')
				os.system('pkill top')
				os.system(f'ssh 10.10.1.2 "pkill benchmark"')

			print(f' thru: {total_thru / 3:.2f}')
			print(f' avg cpu: {total_cpu / 3:.2f}')
			result_file.write(f'bs={bs},cores={cores},thru={total_thru / 3:.2f},cpu={total_cpu / 3:.2f}\n')
			result_file.flush()
		print()


def userspace_rdma():
	args = [benchmark_bin_path, '-user-rdma', '-client', '-event', '-host', '10.10.1.2', '-file', '/mnt/test',
			'-runtime', '30', '-bs', '4', '-cores', '1']
	result_file = open('/users/sjiang/nvmeof-project/src/results/rubble/result-rdma.txt', 'w+')
	peer_cmd = f'{benchmark_bin_path} -user-rdma -cores 3'

	for bs in [4, 16, 64, 512, 1024, 17408]:
		args[-3] = str(bs)
		print('[bs=' + str(bs) + 'kB]')
		for cores in [1, 2, 3]:
			print(' [cores=' + str(cores) + ']')
			args[-1] = str(cores)

			total_thru = 0.0
			total_cpu = 0.0
			for i in range(3):
				out = Popen('top -b > log.txt', shell=True, preexec_fn=os.setsid)
				peer_worker = Popen(f'ssh 10.10.1.2 "{peer_cmd}"', stdout=open('/dev/null'), shell=True)
				sleep(1)
				benchmark = run(args, capture_output=True, text=True)

				os.killpg(os.getpgid(out.pid), signal.SIGTERM)
				peer_worker.terminate()

				last_record = benchmark.stdout.splitlines()[-1]
				thru = re.search(r'(\d+\.\d+) (MB/s)', last_record).groups()
				total_thru += float(thru[0])
				avg_cpu = parse_log()
				total_cpu += avg_cpu

				# clean the env
				os.system('pkill benchmark')
				os.system('pkill top')
				os.system(f'ssh 10.10.1.2 "pkill benchmark"')

			print(f' thru: {total_thru / 3:.2f}')
			print(f' avg cpu: {total_cpu / 3:.2f}')
			result_file.write(f'bs={bs},cores={cores},thru={total_thru / 3:.2f},cpu={total_cpu / 3:.2f}\n')
			result_file.flush()
		print()


if __name__ == '__main__':
	nvmeof()
	# userspace_rdma()
	# peer_cmd = f'{benchmark_bin_path} -user-rdma -cores 3'
	# Popen(f'ssh 10.10.1.2 "{peer_cmd}"', stdout=open('/dev/null'), shell=True)
	# os.system(f'ssh 10.10.1.2 "pkill benchmark"')
