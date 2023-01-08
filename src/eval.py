#!/usr/bin/python

import os
from subprocess import Popen, run, DEVNULL
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
        if line.strip().startswith('PID'):
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
                    # do some processing
                    # '%Cpu114:  0.0 us,  0.0 sy,  0.0 ni,100.0 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st'
                    cols = line.split()
                    process = cols[-1]
                    cpu = cols[-4]
                    if process == 'top':
                        continue
                    total_cpu += float(cpu)

        i += 1
    return total_cpu / iters
        

def nvmeof():
    # NVMe/RDMA
    args = [benchmark_bin_path, '-file', '/nvme/test', '-runtime', '30', '-bs', '4', '-cores', '1']
    result_file = open('/users/sjiang/nvmeof-project/src/results/rubble/nvmeof-tcp.txt', 'w+')
    logdir = '/users/sjiang/nvmeof-project/src/build/'
    worker1_out_file = 'worker1.out'
    worker2_out_file = 'worker2.out'
    for bs in [4, 16, 64, 512, 1024, 4096, 17408]:
        os.system('ssh 10.10.1.2 "rm /nvme/test*"')
        os.system('ssh 10.10.1.3 "rm /nvme/test*"')
     
        args[-3] = str(bs)
        print('[bs=' + str(bs) + 'kB]')
        for cores in [1]:
            print(' [cores=' + str(cores) + ']')
            # args[-1] = str(cores)
        
            worker1_total_thru = 0.0
            worker1_total_cpu = 0.0
            worker2_total_thru = 0.0
            worker2_total_cpu = 0.0
   
            # warm up
            args[-5] = '5'
            cmd = ' '.join(args)
            worker1 = Popen(f'ssh 10.10.1.2 "{cmd}"', shell=True, stdout=DEVNULL)
            worker2 = Popen(f'ssh 10.10.1.3 "{cmd}"', shell=True, stdout=DEVNULL)

            worker1.wait()
            worker2.wait()
            os.system(f'ssh 10.10.1.2 "pkill -9 benchmark"')
            os.system(f'ssh 10.10.1.3 "pkill -9 benchmark"')
            sleep(2)
   
            args[-5] = '30'
            cmd = ' '.join(args)
            for i in range(3):
                logfile = os.path.join(logdir, f'logs/nvmeof-bs-{bs}-cores-{cores}-{i}.txt')
                out1 = Popen(f'ssh 10.10.1.2 "top -b -H -1 > {logfile}"', shell=True, stderr=DEVNULL)
                worker1 = Popen(f'ssh 10.10.1.2 "{cmd}"', shell=True, stdout=open(worker1_out_file, 'w'))
                out2 = Popen(f'ssh 10.10.1.3 "top -b -H -1 > {logfile}"', shell=True, stderr=DEVNULL)
                worker2 = Popen(f'ssh 10.10.1.3 "{cmd}"', shell=True, stdout=open(worker2_out_file, 'w'))

                worker1.wait()
                worker2.wait()
                out1.terminate()
                out2.terminate()

                os.system('ssh 10.10.1.2 "pkill -9 top"')
                os.system('ssh 10.10.1.2 "pkill -9 benchmark"')
                os.system('ssh 10.10.1.3 "pkill -9 top"')
                os.system('ssh 10.10.1.3 "pkill -9 benchmark"')
                sleep(1)
                

                worker1_logfile = f'logs/nvmeof-bs-{bs}-cores-{cores}-{i}-worker1.txt'
                worker1_top = Popen(f'ssh 10.10.1.2 "cat {logfile}"', shell=True, 
                        stdout=open(worker1_logfile, 'w'))
                worker1_top.wait()
                worker1_avg_cpu = parse_log(worker1_logfile)
                worker1_total_cpu += worker1_avg_cpu
                worker1_last_record = open(worker1_out_file, 'r').readlines()[-1]
                worker1_thru = float(re.search(r'(\d+\.\d+) (MB/s)', worker1_last_record).groups()[0])
                worker1_total_thru += worker1_thru
                print('worker1', 'thru:', worker1_thru, 'cpu:', worker1_avg_cpu)

                worker2_logfile = f'logs/nvmeof-bs-{bs}-cores-{cores}-{i}-worker2.txt'
                worker2_top = Popen(f'ssh 10.10.1.3 "cat {logfile}"', shell=True,
                        stdout=open(worker2_logfile, 'w'))
                worker2_top.wait()
                worker2_avg_cpu = parse_log(worker2_logfile)
                worker2_total_cpu += worker2_avg_cpu
                worker2_last_record = open(worker2_out_file, 'r').readlines()[-1]
                worker2_thru = float(re.search(r'(\d+\.\d+) (MB/s)', worker2_last_record).groups()[0])
                worker2_total_thru += worker2_thru
                print('worker2', 'thru:', worker2_thru, 'cpu:', worker2_avg_cpu)	


            total_cpu = worker1_total_cpu + worker2_total_cpu
            total_thru = worker1_total_thru + worker2_total_thru
            print(f' thru: {total_thru / 3:.2f}')
            print(f' avg cpu: {total_cpu / 3:.2f}')
            result_file.write(f"bs={bs},cores={cores},thru={total_thru / 3:.2f}"
                       f"(worker1:{worker1_total_thru / 3}, worker2: {worker2_total_thru / 3}),"
                    f"cpu={total_cpu / 3:.2f}"
                    f"(worker1:{worker1_total_cpu / 3}, worker2: {worker2_total_cpu / 3})\n")
            result_file.flush()
        print()

  
def test_grpc():
    worker1_args = [benchmark_bin_path, '-grpc', '-client', '-host', '10.10.1.3', '-runtime', '30', '-bs', '4', '-cores', '1']
    worker2_args = [benchmark_bin_path, '-grpc', '-client', '-host', '10.10.1.2', '-runtime', '30', '-bs', '4', '-cores', '1']
    result_file = open('/users/sjiang/nvmeof-project/src/results/rubble/grpc.txt', 'w+')
    worker1_listener_cmd = f'{benchmark_bin_path} -host 10.10.1.2 -grpc'
    worker2_listener_cmd = f'{benchmark_bin_path} -host 10.10.1.3 -grpc'
    logdir = '/users/sjiang/nvmeof-project/src/build/'
    worker1_out_file = 'worker1.out'
    worker2_out_file = 'worker2.out'
 
    for bs in [4, 16, 64, 512, 1024, 4096, 17408]:
        os.system('ssh 10.10.1.2 "rm /mnt/test*"')
        os.system('ssh 10.10.1.3 "rm /mnt/test*"')
        
        worker1_args[-3] = str(bs)
        worker2_args[-3] = str(bs)
        print('[bs=' + str(bs) + 'kB]')
        for cores in [1]:
            print(' [cores=' + str(cores) + ']')
            worker1_args[-1] = str(cores)
            worker2_args[-1] = str(cores)

            worker1_total_thru = 0.0
            worker1_total_cpu = 0.0
            worker2_total_thru = 0.0
            worker2_total_cpu = 0.0
            
            # warm up
            worker1_listener = Popen(f'ssh 10.10.1.2 "{worker1_listener_cmd}"', stdout=DEVNULL, shell=True)
            worker2_listener = Popen(f'ssh 10.10.1.3 "{worker2_listener_cmd}"', stdout=DEVNULL, shell=True)
            sleep(1)
            worker1_args[-5] = '5'
            worker2_args[-5] = '5'
            worker1_benchmark = Popen(f'ssh 10.10.1.2 "{" ".join(worker1_args)}"', shell=True, stdout=DEVNULL)
            worker2_benchmark = Popen(f'ssh 10.10.1.3 "{" ".join(worker2_args)}"', shell=True, stdout=DEVNULL)

            
            worker1_benchmark.wait()
            worker2_benchmark.wait()
            worker1_listener.terminate()
            worker2_listener.terminate()
   
            os.system('ssh 10.10.1.2 "pkill -9 benchmark"')
            os.system('ssh 10.10.1.2 "pkill -9 top"')
            os.system('ssh 10.10.1.3 "pkill -9 benchmark"')
            os.system('ssh 10.10.1.3 "pkill -9 top"')
            sleep(3)

            worker1_args[-5] = '30'
            worker2_args[-5] = '30'
            for i in range(3):
                worker1_listener = Popen(f'ssh 10.10.1.2 "{worker1_listener_cmd}"', stdout=DEVNULL, shell=True)
                worker2_listener = Popen(f'ssh 10.10.1.3 "{worker2_listener_cmd}"', stdout=DEVNULL, shell=True)
                sleep(1)

                worker1_cmd = ' '.join(worker1_args)
                worker2_cmd = ' '.join(worker2_args)
    
                logfile = os.path.join(logdir, f'logs/nvmeof-bs-{bs}-cores-{cores}-{i}.txt')
                out1 = Popen(f'ssh 10.10.1.2 "top -b -H -1 > {logfile}"', shell=True, stderr=DEVNULL)
                worker1 = Popen(f'ssh 10.10.1.2 "{worker1_cmd}"', shell=True, stdout=open(worker1_out_file, 'w'),
                    stderr=DEVNULL)
                out2 = Popen(f'ssh 10.10.1.3 "top -b -H -1 > {logfile}"', shell=True, stderr=DEVNULL)
                worker2 = Popen(f'ssh 10.10.1.3 "{worker2_cmd}"', shell=True, stdout=open(worker2_out_file, 'w'),
                    stderr=DEVNULL)

                worker1.wait()
                worker2.wait()
                out1.terminate()
                out2.terminate()
                sleep(1)
    
                os.system('ssh 10.10.1.2 "pkill -9 top"')
                os.system('ssh 10.10.1.2 "pkill -9 benchmark"')
                os.system('ssh 10.10.1.3 "pkill -9 top"')
                os.system('ssh 10.10.1.3 "pkill -9 benchmark"')

                worker1_logfile = f'logs/rdma-bs-{bs}-cores-{cores}-{i}-worker1.txt'
                worker1_top = Popen(f'ssh 10.10.1.2 "cat {logfile}"', shell=True, 
                        stdout=open(worker1_logfile, 'w'))
                worker1_top.wait()
                worker1_avg_cpu = parse_log(worker1_logfile)
                worker1_total_cpu += worker1_avg_cpu
                worker1_last_record = open(worker1_out_file, 'r').readlines()[-1]
                worker1_thru = float(re.search(r'(\d+\.\d+) (MB/s)', worker1_last_record).groups()[0])
                worker1_total_thru += worker1_thru
                print('worker1', 'thru:', worker1_thru, 'cpu:', worker1_avg_cpu)

                worker2_logfile = f'logs/nvmeof-bs-{bs}-cores-{cores}-{i}-worker2.txt'
                worker2_top = Popen(f'ssh 10.10.1.3 "cat {logfile}"', shell=True,
                        stdout=open(worker2_logfile, 'w'))
                worker2_top.wait()
                worker2_avg_cpu = parse_log(worker2_logfile)
                worker2_total_cpu += worker2_avg_cpu
                worker2_last_record = open(worker2_out_file, 'r').readlines()[-1]
                worker2_thru = float(re.search(r'(\d+\.\d+) (MB/s)', worker2_last_record).groups()[0])
                worker2_total_thru += worker2_thru
                print('worker2', 'thru:', worker2_thru, 'cpu:', worker2_avg_cpu)


            total_cpu = worker1_total_cpu + worker2_total_cpu
            total_thru = worker1_total_thru + worker2_total_thru
            print(f' thru: {total_thru / 3:.2f}')
            print(f' avg cpu: {total_cpu / 3:.2f}')
            result_file.write(f"bs={bs},cores={cores},thru={total_thru / 3:.2f}"
                       f"(worker1:{worker1_total_thru / 3}, worker2: {worker2_total_thru / 3}),"
                    f"cpu={total_cpu / 3:.2f}"
                    f"(worker1:{worker1_total_cpu / 3}, worker2: {worker2_total_cpu / 3})\n")
            result_file.flush()
        print()
  
  

def userspace_rdma():
    worker1_args = [benchmark_bin_path, '-user-rdma', '-client', '-event', '-nt', '-host', '10.10.1.3', '-file', '/mnt/test',
            '-runtime', '30', '-bs', '4', '-cores', '1']
    worker2_args = [benchmark_bin_path, '-user-rdma', '-client', '-event', '-nt', '-host', '10.10.1.2', '-file', '/mnt/test',
            '-runtime', '30', '-bs', '4', '-cores', '1']
    result_file = open('/users/sjiang/nvmeof-project/src/results/rubble/result-rdma-2.txt', 'w+')
    worker1_listener_cmd = f'{benchmark_bin_path} -host 10.10.1.2 -user-rdma -event -nt -cores 6'
    worker2_listener_cmd = f'{benchmark_bin_path} -host 10.10.1.3 -user-rdma -event -nt -cores 6'
    logdir = '/users/sjiang/nvmeof-project/src/build/'
    worker1_out_file = 'worker1.out'
    worker2_out_file = 'worker2.out'
 
    for bs in [4, 16, 64, 512, 1024, 4096, 17408]:
        os.system('ssh 10.10.1.2 "rm /mnt/test*"')
        os.system('ssh 10.10.1.3 "rm /mnt/test*"')
  
        worker1_args[-3] = str(bs)
        worker2_args[-3] = str(bs)
        print('[bs=' + str(bs) + 'kB]')
        for cores in [1]:
            print(' [cores=' + str(cores) + ']')
            worker1_args[-1] = str(cores)
            worker2_args[-1] = str(cores)

            worker1_total_thru = 0.0
            worker1_total_cpu = 0.0
            worker2_total_thru = 0.0
            worker2_total_cpu = 0.0

            # warm up
            worker1_listener = Popen(f'ssh 10.10.1.2 "{worker1_listener_cmd}"', stdout=DEVNULL, shell=True)
            worker2_listener = Popen(f'ssh 10.10.1.3 "{worker2_listener_cmd}"', stdout=DEVNULL, shell=True)
            sleep(1)
            worker1_args[-5] = '5'
            worker2_args[-5] = '5'
            worker1_benchmark = Popen(f'ssh 10.10.1.2 "{" ".join(worker1_args)}"', shell=True, stdout=DEVNULL)
            worker2_benchmark = Popen(f'ssh 10.10.1.3 "{" ".join(worker2_args)}"', shell=True, stdout=DEVNULL)

            
            worker1_benchmark.wait()
            worker2_benchmark.wait()
            worker1_listener.terminate()
            worker2_listener.terminate()
   
            os.system('ssh 10.10.1.2 "pkill -9 benchmark"')
            os.system('ssh 10.10.1.2 "pkill -9 top"')
            os.system('ssh 10.10.1.3 "pkill -9 benchmark"')
            os.system('ssh 10.10.1.3 "pkill -9 top"')
            sleep(3)

            worker1_args[-5] = '30'
            worker2_args[-5] = '30'
            for i in range(3):
                worker1_listener = Popen(f'ssh 10.10.1.2 "{worker1_listener_cmd}"', stdout=DEVNULL, shell=True)
                worker2_listener = Popen(f'ssh 10.10.1.3 "{worker2_listener_cmd}"', stdout=DEVNULL, shell=True)
                sleep(1)

                worker1_cmd = ' '.join(worker1_args)
                worker2_cmd = ' '.join(worker2_args)
    
                logfile = os.path.join(logdir, f'logs/nvmeof-bs-{bs}-cores-{cores}-{i}.txt')
                out1 = Popen(f'ssh 10.10.1.2 "top -b -H -1 > {logfile}"', shell=True, stderr=DEVNULL)
                worker1 = Popen(f'ssh 10.10.1.2 "{worker1_cmd}"', shell=True, stdout=open(worker1_out_file, 'w'),
                    stderr=DEVNULL)
                out2 = Popen(f'ssh 10.10.1.3 "top -b -H -1 > {logfile}"', shell=True, stderr=DEVNULL)
                worker2 = Popen(f'ssh 10.10.1.3 "{worker2_cmd}"', shell=True, stdout=open(worker2_out_file, 'w'),
                    stderr=DEVNULL)

                worker1.wait()
                worker2.wait()
                out1.terminate()
                out2.terminate()
                sleep(1)
    
                os.system('ssh 10.10.1.2 "pkill -9 top"')
                os.system('ssh 10.10.1.2 "pkill -9 benchmark"')
                os.system('ssh 10.10.1.3 "pkill -9 top"')
                os.system('ssh 10.10.1.3 "pkill -9 benchmark"')

                worker1_logfile = f'logs/rdma-bs-{bs}-cores-{cores}-{i}-worker1.txt'
                worker1_top = Popen(f'ssh 10.10.1.2 "cat {logfile}"', shell=True, 
                        stdout=open(worker1_logfile, 'w'))
                worker1_top.wait()
                worker1_avg_cpu = parse_log(worker1_logfile)
                worker1_total_cpu += worker1_avg_cpu
                worker1_last_record = open(worker1_out_file, 'r').readlines()[-1]
                worker1_thru = float(re.search(r'(\d+\.\d+) (MB/s)', worker1_last_record).groups()[0])
                worker1_total_thru += worker1_thru
                print('worker1', 'thru:', worker1_thru, 'cpu:', worker1_avg_cpu)

                worker2_logfile = f'logs/nvmeof-bs-{bs}-cores-{cores}-{i}-worker2.txt'
                worker2_top = Popen(f'ssh 10.10.1.3 "cat {logfile}"', shell=True,
                        stdout=open(worker2_logfile, 'w'))
                worker2_top.wait()
                worker2_avg_cpu = parse_log(worker2_logfile)
                worker2_total_cpu += worker2_avg_cpu
                worker2_last_record = open(worker2_out_file, 'r').readlines()[-1]
                worker2_thru = float(re.search(r'(\d+\.\d+) (MB/s)', worker2_last_record).groups()[0])
                worker2_total_thru += worker2_thru
                print('worker2', 'thru:', worker2_thru, 'cpu:', worker2_avg_cpu)


            total_cpu = worker1_total_cpu + worker2_total_cpu
            total_thru = worker1_total_thru + worker2_total_thru
            print(f' thru: {total_thru / 3:.2f}')
            print(f' avg cpu: {total_cpu / 3:.2f}')
            result_file.write(f"bs={bs},cores={cores},thru={total_thru / 3:.2f}"
                       f"(worker1:{worker1_total_thru / 3}, worker2: {worker2_total_thru / 3}),"
                    f"cpu={total_cpu / 3:.2f}"
                    f"(worker1:{worker1_total_cpu / 3}, worker2: {worker2_total_cpu / 3})\n")
            result_file.flush()
        print()


if __name__ == '__main__':
    if not os.path.exists('logs/'):
        os.mkdir('logs/')
    
    # r = Popen(f'ssh 10.10.1.2 "{benchmark_bin_path} -file /nvme/test -bs 4 -runtime 5"', shell=True, stdout=open('tmp', 'w'))
    # r.wait()
    # print(r.stdout)
    # nvmeof()
    test_grpc()
    # userspace_rdma()
    # peer_cmd = f'{benchmark_bin_path} -user-rdma -cores 3'
    # Popen(f'ssh 10.10.1.3 "{peer_cmd}"', stdout=open('/dev/null'), shell=True)
    # os.system(f'ssh 10.10.1.3 "pkill benchmark"')
