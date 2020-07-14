import os
import subprocess as sp
from pathlib import Path
import networkx as nx
import shutil
import click
import csv
import json

class BenchmarkBase():
    def __init__(self, path):
        self.CURRENT_DIR = os.path.dirname(os.path.realpath(__file__))

        # default options
        self.HOME = str(Path.home())
        self.WORKING_DIR = path

        self.CC = 'clang'
        self.CXX = 'clang++'
        self.LINKER = 'clang++'

        self.COMMON_FLAGS = '-O3 -S -emit-llvm -fno-vectorize -fno-slp-vectorize'

        self.CC_FLAGS = ''
        self.CXX_FLAGS = ''

        self.BACKEND_REGALLOC = 'greedy'

        self.LLC = 'llc'
        self.LLC_PROCESS_FLAGS = '-O3 -regalloc pp2 -pp2-dummy-dump-graph -pp2-dummy-export-graph -pp2-skip'
        self.LLC_RUN_FLAGS = f'-O3 -filetype=asm -regalloc pp2 -pp2-regalloc {self.BACKEND_REGALLOC} -stats -stats-json-file'

        self.CONDA_S2V_DQN_ENV = 'py27'

    def setup(self):
        os.system(f'rm -rf {self.WORKING_DIR}/tmp')

    def compile(self):
        raise Exception("Not implemented")

    def gen_if_graph(self):
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err:
            for filename in os.listdir(self.WORKING_DIR):
                filename = os.path.join(self.WORKING_DIR, filename)
                if filename.endswith(".ll"):
                    print(f"[PROCESS] {filename}")
                    process = sp.Popen(f'cd {self.WORKING_DIR} && {self.LLC} {self.LLC_PROCESS_FLAGS} {filename}',
                        shell=True,
                        stdout=sp.PIPE,
                        stderr=sp.PIPE)
                    stdout, stderr = process.communicate()
                    if process.returncode != 0:
                        log_err.write(stderr.decode('utf-8'))
                        log_err.write(f"Failed to process: {filename}")
                        exit(301)

    def graph_to_pickle(self):
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err:
            process = sp.Popen(f'source ~/anaconda3/etc/profile.d/conda.sh && conda activate {self.CONDA_S2V_DQN_ENV} && python {self.CURRENT_DIR}/graph_to_pickle.py --path {self.WORKING_DIR}',
                shell=True,
                stdout=sp.PIPE,
                stderr=sp.PIPE)
            stdout, stderr = process.communicate()
            if process.returncode != 0:
                log_err.write(stderr.decode('utf-8'))
                log_err.write('Failed to convert IF graphs to pickle')
                exit(401)

    def gen_coloring(self):
        print("[COLORING] start coloring")
        g_type='barabasi_albert'
        data_test='dummy'
        data_dir=self.WORKING_DIR
        output_dir=self.WORKING_DIR
        isec=16
        result_root=f'results/dqn-{g_type}'
        max_bp_iter=5
        embed_dim=64
        dev_id=3
        batch_size=64
        net_type='QNet'
        reg_hidden=64
        learning_rate=0.0001
        w_scale=0.01
        n_step=5
        min_n=15
        max_n=20
        num_env=10
        mem_size=500000
        max_iter=100000
        save_dir=f'{result_root}/embed-{embed_dim}-nbp-{max_bp_iter}-rh-{reg_hidden}'
        evaluate=f"""python evaluate.py \
            -n_step {n_step} \
            -dev_id {dev_id} \
            -data_test {data_test} \
            -data_dir {data_dir} \
            -output_dir {output_dir} \
            -isec {isec} \
            -min_n {min_n} \
            -max_n {max_n} \
            -num_env {num_env} \
            -max_iter {max_iter} \
            -mem_size {mem_size} \
            -g_type {g_type} \
            -learning_rate {learning_rate} \
            -max_bp_iter {max_bp_iter} \
            -net_type {net_type} \
            -max_iter {max_iter} \
            -save_dir {save_dir} \
            -embed_dim {embed_dim} \
            -batch_size {batch_size} \
            -reg_hidden {reg_hidden} \
            -momentum 0.9 \
            -l2 0.00 \
            -w_scale {w_scale}"""
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err:
            env = os.environ.copy()
            env['WORKING_DIR'] = self.WORKING_DIR
            process = sp.Popen(f'source ~/anaconda3/etc/profile.d/conda.sh && conda activate {self.CONDA_S2V_DQN_ENV} && cd {self.HOME}/graph_comb_opt/code/s2v_mvc && {evaluate}',
                shell=True,
                stdout=sp.PIPE,
                stderr=sp.PIPE,
                env=env)
            stdout, stderr = process.communicate()
            if process.returncode != 0:
                log_err.write(stderr.decode('utf-8'))
                log_err.write('Failed to color IF graphs')
                exit(501)

    def benchmark(self):
        raise Exception("Not implemented")

    def gen_statistics(self):
        raise Exception("Not implemented")

    def cleanup(self):
        print("[CLEANUP] start cleanup")
        os.makedirs(f'{self.WORKING_DIR}/tmp', exist_ok=True)
        for filename in os.listdir(self.WORKING_DIR):
            if len(filename.split('.')) >= 3 and (filename.split('.')[1] == 'll' or filename.split('.')[-1] == 's' or filename.split('.')[-1] == 'json'):
                shutil.move(f'{self.WORKING_DIR}/{filename}', f'{self.WORKING_DIR}/tmp/{filename}')

    def run(self):
        self.setup()
        self.compile()
        self.gen_if_graph()
        self.graph_to_pickle()
        self.gen_coloring()
        self.benchmark()
        self.gen_statistics()
        self.cleanup()
        print("Finished successfully!")

class BenchmarkHarris(BenchmarkBase):
    def __init__(self, path):
        BenchmarkBase.__init__(self, path)
        self.CXX_FLAGS = f'{self.COMMON_FLAGS} -I{self.HOME}/include -std=c++11 -ffast-math'

    def compile(self):
        with open(f'{self.WORKING_DIR}/log.txt', 'w') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'w') as log_err:
            for filename in os.listdir(self.WORKING_DIR):
                filename = os.path.join(self.WORKING_DIR, filename)
                if filename.endswith(".cpp"):
                    print(f"[COMPILE] {filename}")
                    process = sp.Popen(f'cd {self.WORKING_DIR} && {self.CXX} {self.CXX_FLAGS} {filename}',
                        shell=True,
                        stdout=sp.PIPE,
                        stderr=sp.PIPE)
                    stdout, stderr = process.communicate()
                    if process.returncode != 0:
                        log_err.write(stderr.decode('utf-8'))
                        log_err.write(f"Failed to compile: {filename}")
                        exit(201)

    def benchmark(self):
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err:
            for i in range(17):
                print(f'ISEC #{i}:')
                for filename in os.listdir(self.WORKING_DIR):
                    filename = os.path.join(self.WORKING_DIR, filename)
                    if filename.endswith(".ll"):
                        print(f'[REGALLOC] {filename}')
                        process = sp.Popen(f'cd {self.WORKING_DIR} && {self.LLC} {self.LLC_RUN_FLAGS} -pp2-isec {i} {filename}',
                            shell=True,
                            stdout=sp.PIPE,
                            stderr=sp.PIPE)
                        stdout, stderr = process.communicate()
                        if process.returncode != 0:
                            log_err.write(stderr.decode('utf-8'))
                            log_err.write(f"Failed to regalloc: {filename}")
                            exit(601)

                print(f'[LINKING] start linking')
                process = sp.Popen(f'cd {self.WORKING_DIR} && {self.LINKER} harrisKernel.s main.s -L{self.HOME}/lib64 -lbenchmark -o harris',
                    shell=True,
                    stdout=sp.PIPE,
                    stderr=sp.PIPE)
                stdout, stderr = process.communicate()
                if process.returncode != 0:
                    log_err.write(stderr.decode('utf-8'))
                    log_err.write(f"Failed to linking")
                    exit(602)

                print(f'[BENCHMARK] start benchmarking')
                process = sp.Popen(f'cd {self.WORKING_DIR} && ./harris --benchmark_out="{self.WORKING_DIR}/result.{i}.json" --benchmark_out_format=json --benchmark_repetitions=2',
                    shell=True,
                    stdout=sp.PIPE,
                    stderr=sp.PIPE)
                stdout, stderr = process.communicate()
                if process.returncode != 0:
                    log_err.write(stderr.decode('utf-8'))
                    log_err.write(f"Failed to benchmark")
                    exit(603)

                print(f'[PREPARE STATISTICS] prepare statistics')
                process = sp.Popen(f'mv {self.WORKING_DIR}/stats.json {self.WORKING_DIR}/stats.{i}.json',
                    shell=True,
                    stdout=sp.PIPE,
                    stderr=sp.PIPE)
                stdout, stderr = process.communicate()
                if process.returncode != 0:
                    log_err.write(stderr.decode('utf-8'))
                    log_err.write(f"Failed to prepare statistics")
                    exit(604)

                for filename in os.listdir(self.WORKING_DIR):
                    filename = os.path.join(self.WORKING_DIR, filename)
                    if filename.endswith(".ll"):
                        process = sp.Popen(f'mv {os.path.splitext(filename)[0]}.s {os.path.splitext(filename)[0]}.{i}.s',
                            shell=True,
                            stdout=sp.PIPE,
                            stderr=sp.PIPE)
                        stdout, stderr = process.communicate()
                        if process.returncode != 0:
                            log_err.write(stderr.decode('utf-8'))
                            log_err.write(f"Failed to prepare statistics")
                            exit(605)

    def gen_statistics(self):
        print(f'[STATISTICS] generate statistics')
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err, open(f'{self.WORKING_DIR}/runtime.csv', 'w') as runtime_csv, open(f'{self.WORKING_DIR}/ise.csv', 'w') as ise_csv:
            # runtime statistics
            runtime_csv_writer = csv.writer(runtime_csv, delimiter=',', quotechar='"', quoting=csv.QUOTE_MINIMAL)
            mean_list = []
            stddev_list = []
            for i in range(17):
                with open(f'{self.WORKING_DIR}/result.{i}.json') as f:
                    data = json.load(f)
                    for record in data['benchmarks']:
                        if record['name'] == 'BENCHMARK_HARRIS/2048/2048_mean':
                            mean_list.append(record['real_time']/10**6)
                        elif record['name'] == 'BENCHMARK_HARRIS/2048/2048_stddev':
                            stddev_list.append(record['real_time']/10**6)
                        else:
                            continue
            runtime_csv_writer.writerow(mean_list + stddev_list)

            # ise statistics
            ise_csv_writer = csv.writer(ise_csv, delimiter=',', quotechar='"', quoting=csv.QUOTE_MINIMAL)
            for i in range(17):
                csv_row = []
                with open(f'{self.WORKING_DIR}/stats.{i}.json') as f:
                    data = json.load(f)
                    csv_row.append(data.get("regalloc.NumMISNodes", None))
                    csv_row.append(data.get("regalloc.NumSkippedNodes",None))
                    csv_row.append(data.get("regalloc.NumSplittedOrEvictedVRegsAllocatedByPP2", None))
                for filename in os.listdir(self.WORKING_DIR):
                    filename = os.path.join(self.WORKING_DIR, filename)
                    if filename.endswith(".ll"):
                        process = sp.Popen(f'cat {os.path.splitext(filename)[0]}.{i}.s | wc -l',
                            shell=True,
                            stdout=sp.PIPE,
                            stderr=sp.PIPE)
                        stdout, stderr = process.communicate()
                        if process.returncode != 0:
                            log_err.write(stderr.decode('utf-8'))
                            log_err.write(f"Failed to benchmark")
                            exit(701)
                        csv_row.append(int(stdout.decode('utf-8')))

                        process = sp.Popen(f'cat {os.path.splitext(filename)[0]}.{i}.s | rg "\-byte Spill" | wc -l',
                            shell=True,
                            stdout=sp.PIPE,
                            stderr=sp.PIPE)
                        stdout, stderr = process.communicate()
                        if process.returncode != 0:
                            log_err.write(stderr.decode('utf-8'))
                            log_err.write(f"Failed to benchmark")
                            exit(702)
                        csv_row.append(int(stdout.decode('utf-8')))
                ise_csv_writer.writerow(csv_row)

@click.command()
@click.option('--path', help='benchmark path')
@click.option('--benchmark', help='benchmark type')
def run(path, benchmark):
    if benchmark == 'harris':
        BenchmarkHarris(path).run()
    else:
        pass

if __name__ == "__main__":
    run()
