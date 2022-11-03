#!/usr/bin/python

import os
from subprocess import Popen, run
import signal
import re
from time import sleep

benchmark_bin_path = '/users/sjiang/nvmeof-project/src/build/benchmark'
# cpu_processes = [
# 	'benchmark',
# 	'kworker/.+',
# 	'io_wqe_worker.+'
# ]

def parse_log(logfile):
	log = open(logfile, 'r')
	content = log.readlines()
	iters = 0
	i = 0
	skip = 0
	total_cpu = 0.0
	while i < len(content):
		line = content[i]
		if line.strip().startswith('Tasks'):
			# skip the first two top as metrics are not stable now
			if skip < 2:
				skip += 1
			else:
				iters += 1
				i += 1
				while i < len(content):
					if content[i].strip() == '':
						break
					line = content[i]
					i += 1
					if not line.startswith('%Cpu'):
						continue
					# do some processing
					# '%Cpu114:  0.0 us,  0.0 sy,  0.0 ni,100.0 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st'
					cols = line.split(',')
					cpu = 0.0
					for idx in range(len(cols)):
						if idx == 3 or idx == 4:
							continue
						cpu += float(cols[idx].strip().split()[-2])
					total_cpu += float(cpu)

		i += 1
	return total_cpu / iters
		

def nvmeof():
	# NVMe/RDMA
	args = [benchmark_bin_path, '-file', '/nvme/test', '-runtime', '30', '-bs', '4', '-cores', '1']
	result_file = open('/users/sjiang/nvmeof-project/src/results/rubble/result-nvmeof-offloading-tmp.txt', 'w+')
	for bs in [16]:
	# for bs in [17408]:
		os.system('rm /nvme/test*')
		os.system('ssh 10.10.1.2 "rm /nvme/test*"')
     
		args[-3] = str(bs)
		print('[bs=' + str(bs) + 'kB]')
		for cores in [2, 3, 4, 5, 6]:
			print(' [cores=' + str(cores) + ']')
			args[-1] = str(cores)
		
			total_thru = 0.0
			total_cpu = 0.0
   
			# warm up
			args[-5] = '10'
			peer_cmd = ' '.join(args)
			peer_worker = Popen(f'ssh 10.10.1.2 "{peer_cmd}"', shell=True)
			benchmark = run(args, capture_output=True, text=True)

			peer_worker.terminate()
			os.system('pkill -9 benchmark')
			os.system(f'ssh 10.10.1.2 "pkill -9 benchmark"')
			sleep(3)
   
			args[-5] = '30'
			peer_cmd = ' '.join(args)
			for i in range(3):
				logfile = f'logs/nvmeof-bs-{bs}-cores-{cores}-{i}.txt'
				out = Popen(f'top -b -1 > {logfile}', shell=True, preexec_fn=os.setsid)
				# start the worker process on the peer
				peer_worker = Popen(f'ssh 10.10.1.2 "{peer_cmd}"', shell=True)
				benchmark = run(args, capture_output=True, text=True)

				os.killpg(os.getpgid(out.pid), signal.SIGTERM)
				peer_worker.terminate()

				last_record = benchmark.stdout.splitlines()[-1]
				thru = re.search(r'(\d+\.\d+) (MB/s)', last_record).groups()
				total_thru += float(thru[0])
				avg_cpu = parse_log(logfile)
				total_cpu += avg_cpu
				# clean the env
				os.system('pkill -9 benchmark')
				os.system('pkill -9 top')
				os.system(f'ssh 10.10.1.2 "pkill -9 benchmark"')
				sleep(3)

			print(f' thru: {total_thru / 3:.2f}')
			print(f' avg cpu: {total_cpu / 3:.2f}')
			result_file.write(f'bs={bs},cores={cores},thru={total_thru / 3:.2f},cpu={total_cpu / 3:.2f}\n')
			result_file.flush()
		print()


def userspace_rdma():
	args = [benchmark_bin_path, '-user-rdma', '-client', '-event', '-host', '10.10.1.2', '-file', '/mnt/test',
			'-runtime', '30', '-bs', '4', '-cores', '1']
	result_file = open('/users/sjiang/nvmeof-project/src/results/rubble/result-rdma.txt', 'w+')
	listener_cmd = f'{benchmark_bin_path} -host 10.10.1.1 -user-rdma -cores 6'
	peer_listener_cmd = f'{benchmark_bin_path} -host 10.10.1.2 -user-rdma -cores 6'
 
	for bs in [4, 16, 64, 512, 1024, 17408]:
		os.system('rm /mnt/test*')
		os.system('ssh 10.10.1.2 "rm /mnt/test*"')
  
		args[-3] = str(bs)
		print('[bs=' + str(bs) + 'kB]')
		for cores in [1, 2, 3, 4, 5, 6]:
			print(' [cores=' + str(cores) + ']')
			args[-1] = str(cores)

			total_thru = 0.0
			total_cpu = 0.0

			# warm up
			peer_listener = Popen(f'ssh 10.10.1.2 "{peer_listener_cmd}"', stdout=open('/dev/null'), shell=True)
			my_listener = Popen(listener_cmd, stdout=open('/dev/null'), shell=True)
			sleep(1)
			args[-5] = '10'
			peer_args = args.copy()
			peer_args[5] = "10.10.1.1"
			peer_benchmark = Popen(f'ssh 10.10.1.2 "{" ".join(peer_args)}"', shell=True, preexec_fn=os.setsid)
			my_benchmark = run(args, capture_output=True, text=True)

			os.killpg(os.getpgid(peer_benchmark.pid), signal.SIGTERM)
			peer_listener.terminate()
			my_listener.terminate()
			os.system('pkill -9 benchmark')
			os.system('pkill -9 top')
			os.system(f'ssh 10.10.1.2 "pkill -9 benchmark"')
			sleep(3)

			args[-5] = '30'
			for i in range(3):
				peer_listener = Popen(f'ssh 10.10.1.2 "{peer_listener_cmd}"', stdout=open('/dev/null'), shell=True)
				my_listener = Popen(listener_cmd, stdout=open('/dev/null'), shell=True)
				sleep(1)
				peer_args = args.copy()
				peer_args[5] = "10.10.1.1"
    
				logfile = f'logs/rdma-bs-{bs}-cores-{cores}-{i}.txt'
				out = Popen(f'top -b -1 > {logfile}', shell=True, preexec_fn=os.setsid)
				peer_benchmark = Popen(f'ssh 10.10.1.2 "{" ".join(peer_args)}"', shell=True, preexec_fn=os.setsid)
				my_benchmark = run(args, capture_output=True, text=True)

				os.killpg(os.getpgid(out.pid), signal.SIGTERM)
				os.killpg(os.getpgid(peer_benchmark.pid), signal.SIGTERM)
				peer_listener.terminate()
				my_listener.terminate()

				last_record = my_benchmark.stdout.splitlines()[-1]
				thru = re.search(r'(\d+\.\d+) (MB/s)', last_record).groups()
				total_thru += float(thru[0])
				avg_cpu = parse_log(logfile)
				total_cpu += avg_cpu

				# clean the env
				os.system('pkill -9 benchmark')
				os.system('pkill -9 top')
				os.system(f'ssh 10.10.1.2 "pkill -9 benchmark"')
				sleep(3)

			print(f' thru: {total_thru / 3:.2f}')
			print(f' avg cpu: {total_cpu / 3:.2f}')
			result_file.write(f'bs={bs},cores={cores},thru={total_thru / 3:.2f},cpu={total_cpu / 3:.2f}\n')
			result_file.flush()
		print()


if __name__ == '__main__':
	if not os.path.exists('logs/'):
		os.mkdir('logs/')
	
	nvmeof()
	# userspace_rdma()
	# peer_cmd = f'{benchmark_bin_path} -user-rdma -cores 3'
	# Popen(f'ssh 10.10.1.2 "{peer_cmd}"', stdout=open('/dev/null'), shell=True)
	# os.system(f'ssh 10.10.1.2 "pkill benchmark"')
