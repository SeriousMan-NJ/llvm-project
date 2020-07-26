import os
import subprocess as sp
from pathlib import Path
import networkx as nx
import shutil
import click
import csv
import json
import time

class BenchmarkBase():
    def __init__(self, path):
        self.CURRENT_DIR = os.path.dirname(os.path.realpath(__file__))

        # default options
        self.HOME = str(Path.home())
        self.WORKING_DIR = path

        self.CC = 'clang'
        self.CXX = 'clang++'
        self.LINKER = 'clang++'
        self.linking_files = ''

        self.COMMON_FLAGS = '-O3 -S -emit-llvm -fno-vectorize -fno-slp-vectorize'

        self.CC_FLAGS = ''
        self.CXX_FLAGS = ''
        self.LINKER_FLAGS = ''

        self.BINARY = 'a.out'
        self.BINARY_OPTIONS = ''

        self.BACKEND_REGALLOC = 'greedy'

        self.LLC = 'llc'
        self.LLC_PROCESS_FLAGS = '-O3 -regalloc pp2 -pp2-dummy-dump-graph -pp2-dummy-export-graph -pp2-skip'
        self.LLC_RUN_FLAGS = f'-O3 -filetype=asm -regalloc pp2 -pp2-regalloc {self.BACKEND_REGALLOC} -stats -stats-json-file'

        self.CONDA_S2V_DQN_ENV = 'py27'
        self.CONDA_MAX_APPROX_ENV = 'py37'

        self.ISEC = 16
        self.BENCHMARK_REPETITION = 10

    def setup(self):
        os.system(f'rm -rf {self.WORKING_DIR}/tmp')
        pass

    def compile(self):
        with open(f'{self.WORKING_DIR}/log.txt', 'w') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'w') as log_err:
            for filename in os.listdir(self.WORKING_DIR):
                filename = os.path.join(self.WORKING_DIR, filename)
                if filename.endswith(".c"):
                    print(f"[COMPILE] {filename}")
                    process = sp.Popen(f'cd {self.WORKING_DIR} && {self.CC} {self.CC_FLAGS} {filename}',
                        shell=True,
                        executable='/bin/bash',
                        stdout=sp.PIPE,
                        stderr=sp.PIPE)
                    stdout, stderr = process.communicate()
                    if process.returncode != 0:
                        log_err.write(stderr.decode('utf-8'))
                        log_err.write(f"Failed to compile: {filename}")
                        exit(201)
                if filename.endswith(".cpp"):
                    print(f"[COMPILE] {filename}")
                    process = sp.Popen(f'cd {self.WORKING_DIR} && {self.CXX} {self.CXX_FLAGS} {filename}',
                        shell=True,
                        executable='/bin/bash',
                        stdout=sp.PIPE,
                        stderr=sp.PIPE)
                    stdout, stderr = process.communicate()
                    if process.returncode != 0:
                        log_err.write(stderr.decode('utf-8'))
                        log_err.write(f"Failed to compile: {filename}")
                        exit(202)

    def gen_if_graph(self):
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err:
            for filename in os.listdir(self.WORKING_DIR):
                filename = os.path.join(self.WORKING_DIR, filename)
                if filename.endswith(".ll"):
                    print(f"[PROCESS] {filename}")
                    process = sp.Popen(f'cd {self.WORKING_DIR} && {self.LLC} {self.LLC_PROCESS_FLAGS} {filename}',
                        shell=True,
                        executable='/bin/bash',
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
                executable='/bin/bash',
                stdout=sp.PIPE,
                stderr=sp.PIPE)
            stdout, stderr = process.communicate()
            if process.returncode != 0:
                log_err.write(stderr.decode('utf-8'))
                log_err.write('Failed to convert IF graphs to pickle')
                exit(401)

    def gen_coloring_s2v_dqn(self):
        print("[COLORING: S2V-DQN] start coloring")
        g_type='erdos_renyi'
        # g_type='barabasi_albert'
        data_test='dummy'
        data_dir=self.WORKING_DIR
        output_dir=self.WORKING_DIR
        isec=self.ISEC
        result_root=f'results/dqn-{g_type}'
        max_bp_iter=5
        embed_dim=64
        dev_id=3
        batch_size=128
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
            process = sp.Popen(f'source {self.HOME}/.bash_profile && source {self.HOME}/anaconda3/etc/profile.d/conda.sh && conda activate {self.CONDA_S2V_DQN_ENV} && cd {self.HOME}/graph_comb_opt/code/s2v_mvc && {evaluate}',
                shell=True,
                executable='/bin/bash',
                stdout=sp.PIPE,
                stderr=sp.PIPE,
                env=env)
            stdout, stderr = process.communicate()
            if process.returncode != 0:
                print('[RECOLORING] retry...')
                self.gen_coloring_s2v_dqn()
                # log_err.write(stderr.decode('utf-8'))
                # log_err.write('Failed to color IF graphs')
                # exit(501)

    def gen_coloring_max_approx(self, cost_model):
        print(f"[COLORING: MAX-APPROX-{cost_model}] start coloring")
        data_dir=self.WORKING_DIR
        output_dir=self.WORKING_DIR
        isec=self.ISEC
        evaluate=f"""python mvc_approx_greedy.py \
            --data-dir {data_dir} \
            --output-dir {output_dir} \
            --isec {isec} \
            --cost-model {cost_model}"""
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err:
            process = sp.Popen(f'source {self.HOME}/.bash_profile && source {self.HOME}/anaconda3/etc/profile.d/conda.sh && conda activate {self.CONDA_MAX_APPROX_ENV} && cd {self.HOME}/llvm-project/llvm/lib/CodeGen/PP2/scripts && {evaluate}',
                shell=True,
                executable='/bin/bash',
                stdout=sp.PIPE,
                stderr=sp.PIPE)
            stdout, stderr = process.communicate()
            if process.returncode != 0:
                print('[RECOLORING] retry...')
                self.gen_coloring_max_approx()
                # log_err.write(stderr.decode('utf-8'))
                # log_err.write('Failed to color IF graphs')
                # exit(501)

    def gen_coloring(self, mis_heuristic):
        if mis_heuristic == 's2v-dqn':
            self.gen_coloring_s2v_dqn()
        elif mis_heuristic == 'max-approx':
            self.gen_coloring_max_approx(1)
        elif mis_heuristic == 'max-approx-2':
            self.gen_coloring_max_approx(2)
        elif mis_heuristic == 'max-approx-3':
            self.gen_coloring_max_approx(3)

    def regalloc(self, i):
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err:
            for filename in os.listdir(self.WORKING_DIR):
                filename = os.path.join(self.WORKING_DIR, filename)
                if filename.endswith(".ll"):
                    print(f'[REGALLOC] {filename}')
                    process = sp.Popen(f'cd {self.WORKING_DIR} && {self.LLC} {self.LLC_RUN_FLAGS} -pp2-isec {i} {filename}',
                        shell=True,
                        executable='/bin/bash',
                        stdout=sp.PIPE,
                        stderr=sp.PIPE)
                    stdout, stderr = process.communicate()
                    if process.returncode != 0:
                        log_err.write(stderr.decode('utf-8'))
                        log_err.write(f"Failed to regalloc: {filename}")
                        exit(601)

    def linking(self):
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err:
            print(f'[LINKING: {self.BINARY}] start linking')
            process = sp.Popen(f'cd {self.WORKING_DIR} && {self.LINKER} {self.linking_files} {self.LINKER_FLAGS} -o {self.BINARY}',
                shell=True,
                executable='/bin/bash',
                stdout=sp.PIPE,
                stderr=sp.PIPE)
            stdout, stderr = process.communicate()
            if process.returncode != 0:
                log_err.write(stderr.decode('utf-8'))
                log_err.write(f"Failed to linking")
                exit(602)

    def benchmark(self, i):
        raise Exception("Not implemented")

    def prepare_statistics(self, i):
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err:
            print(f'[PREPARE STATISTICS] prepare statistics')
            for filename in os.listdir(self.WORKING_DIR):
                filename = os.path.join(self.WORKING_DIR, filename)
                if filename.endswith(".ll"):
                    process = sp.Popen(f'mv {os.path.splitext(filename)[0]}.s {os.path.splitext(filename)[0]}.{i}.s',
                        shell=True,
                        executable='/bin/bash',
                        stdout=sp.PIPE,
                        stderr=sp.PIPE)
                    stdout, stderr = process.communicate()
                    if process.returncode != 0:
                        log_err.write(stderr.decode('utf-8'))
                        log_err.write(f"Failed to prepare statistics")
                        exit(605)

    def gen_statistics(self):
        self.runtime_statistics()
        self.ise_statistics()

    def runtime_statistics(self):
        raise Exception("Not implemented")

    def ise_statistics(self):
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err, open(f'{self.WORKING_DIR}/ise_{self.BINARY}.csv', 'w') as ise_csv:
            ise_csv_writer = csv.writer(ise_csv, delimiter=',', quotechar='"', quoting=csv.QUOTE_MINIMAL)
            for i in range(self.ISEC + 1):
                csv_row = []
                total_loc = 0
                total_spills = 0
                for filename in os.listdir(self.WORKING_DIR):
                    filename = os.path.join(self.WORKING_DIR, filename)
                    if filename.endswith(".ll"):
                        process = sp.Popen(f'cat {os.path.splitext(filename)[0]}.{i}.s | wc -l',
                            shell=True,
                            executable='/bin/bash',
                            stdout=sp.PIPE,
                            stderr=sp.PIPE)
                        stdout, stderr = process.communicate()
                        if process.returncode != 0:
                            log_err.write(stderr.decode('utf-8'))
                            log_err.write(f"Failed to benchmark")
                            exit(701)
                        total_loc += int(stdout.decode('utf-8'))

                        process = sp.Popen(f'cat {os.path.splitext(filename)[0]}.{i}.s | rg "\-byte Spill" | wc -l',
                            shell=True,
                            executable='/bin/bash',
                            stdout=sp.PIPE,
                            stderr=sp.PIPE)
                        stdout, stderr = process.communicate()
                        if process.returncode != 0:
                            log_err.write(stderr.decode('utf-8'))
                            log_err.write(f"Failed to benchmark")
                            exit(702)
                        total_spills += int(stdout.decode('utf-8'))
                csv_row.append(total_loc)
                csv_row.append(total_spills)
                ise_csv_writer.writerow(csv_row)

    def cleanup(self):
        print("[CLEANUP] start cleanup")
        os.makedirs(f'{self.WORKING_DIR}/tmp', exist_ok=True)
        for filename in os.listdir(self.WORKING_DIR):
            if len(filename.split('.')) >= 3 and (filename.split('.')[1] == 'll' or filename.split('.')[-1] == 's' or filename.split('.')[-1] == 'json') or len(filename.split('.')) >= 3 and filename.split('.')[-2] == 'bench':
                shutil.move(f'{self.WORKING_DIR}/{filename}', f'{self.WORKING_DIR}/tmp/{filename}')

    def run(self, mis_heuristic):
        self.setup()
        self.compile()
        self.gen_if_graph()
        self.graph_to_pickle()
        self.gen_coloring(mis_heuristic)
        for i in range(self.ISEC + 1):
            print(f'ISEC #{i}:')
            self.regalloc(i)
            self.linking()
            self.benchmark(i)
            self.prepare_statistics(i)
        self.gen_statistics()
        self.cleanup()
        print("Finished successfully!")

class GoogleBenchmarkMixin():
    def benchmark(self, i):
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err:
            print(f'[BENCHMARK: {self.BINARY}] start benchmarking')
            process = sp.Popen(f'cd {self.WORKING_DIR} && ./{self.BINARY} --benchmark_out="{self.WORKING_DIR}/result.{i}.json" --benchmark_out_format=json --benchmark_repetitions={self.BENCHMARK_REPETITION}',
                shell=True,
                executable='/bin/bash',
                stdout=sp.PIPE,
                stderr=sp.PIPE)
            stdout, stderr = process.communicate()
            if process.returncode != 0:
                log_err.write(stderr.decode('utf-8'))
                log_err.write(f"Failed to benchmark")
                exit(603)

    def runtime_statistics(self):
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err:
            for benchmark in self.google_benchmarks:
                with open(f'{self.WORKING_DIR}/runtime_{benchmark["name"]}.csv', 'w') as runtime_csv:
                    runtime_csv_writer = csv.writer(runtime_csv, delimiter=',', quotechar='"', quoting=csv.QUOTE_MINIMAL)
                    mean_list = []
                    stddev_list = []
                    for i in range(self.ISEC + 1):
                        with open(f'{self.WORKING_DIR}/result.{i}.json') as f:
                            data = json.load(f)
                            for record in data['benchmarks']:
                                if record['name'] == f"{benchmark['run']}_mean":
                                    mean_list.append(record['real_time']/10**6)
                                elif record['name'] == f"{benchmark['run']}_stddev":
                                    stddev_list.append(record['real_time']/10**6)
                                else:
                                    continue
                    runtime_csv_writer.writerow(mean_list + stddev_list)

class PerfBencharkMixin():
    def benchmark(self, i):
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err:
            print(f'[BENCHMARK: {self.BINARY}] start benchmarking')
            process = sp.Popen(f'cd {self.WORKING_DIR} && perf stat -B -e cache-references,cache-misses,cycles,instructions,branches,faults,migrations -r {self.BENCHMARK_REPETITION} -o {self.BINARY}.bench.{i} ./{self.BINARY} {self.BINARY_OPTIONS} > /dev/null',
                shell=True,
                executable='/bin/bash',
                stdout=sp.PIPE,
                stderr=sp.PIPE)
            stdout, stderr = process.communicate()
            if process.returncode != 0:
                log_err.write(stderr.decode('utf-8'))
                log_err.write(f"Failed to benchmark")
                exit(603)

    def runtime_statistics(self):
        with open(f'{self.WORKING_DIR}/log.txt', 'a') as log_out, open(f'{self.WORKING_DIR}/err.txt', 'a') as log_err, open(f'{self.WORKING_DIR}/runtime_{self.BINARY}.csv', 'w') as runtime_csv:
            runtime_csv_writer = csv.writer(runtime_csv, delimiter=',', quotechar='"', quoting=csv.QUOTE_MINIMAL)
            mean_list = []
            stddev_list = []
            for i in range(self.ISEC + 1):
                with open(f'{self.WORKING_DIR}/{self.BINARY}.bench.{i}', 'r') as f:
                    for line in f:
                        if "time elapsed" in line:
                            line = line.strip().split()
                            mean_list.append(line[0])
                            stddev_list.append(line[2])
            runtime_csv_writer.writerow(mean_list + stddev_list)

class SingleSourceBenchmarkMixin(PerfBencharkMixin):
    def benchmark(self, i):
        for BINARY, BINARY_OPTIONS in self.BINARIES:
            self.BINARY = BINARY
            self.BINARY_OPTIONS = BINARY_OPTIONS
            super().benchmark(i)

    def linking(self):
        for i, linking_files in enumerate(self.linking_files_list):
            self.BINARY, self.BINARY_OPTIONS = self.BINARIES[i]
            self.linking_files = linking_files
            super().linking()

    def runtime_statistics(self):
        for BINARY, BINARY_OPTIONS in self.BINARIES:
            self.BINARY = BINARY
            self.BINARY_OPTIONS = BINARY_OPTIONS
            super().runtime_statistics()

    def ise_statistics(self):
        for BINARY, BINARY_OPTIONS in self.BINARIES:
            self.BINARY = BINARY
            self.BINARY_OPTIONS = BINARY_OPTIONS
            super().ise_statistics()

class BenchmarkHarris(GoogleBenchmarkMixin, BenchmarkBase):
    def __init__(self, path):
        BenchmarkBase.__init__(self, path)
        self.CXX_FLAGS = f'{self.COMMON_FLAGS} -I{self.HOME}/include -std=c++11 -ffast-math'
        self.LINKER_FLAGS = f'-L{self.HOME}/lib64 -lbenchmark'
        self.BINARY = 'harris'
        self.linking_files = 'harrisKernel.s main.s'
        self.google_benchmarks = [{
            'name': 'harris',
            'run': 'BENCHMARK_HARRIS/2048/2048',
        }]

class BenchmarkBilateralFiltering(GoogleBenchmarkMixin, BenchmarkBase):
    def __init__(self, path):
        BenchmarkBase.__init__(self, path)
        self.CC_FLAGS = f'{self.COMMON_FLAGS} -I{self.HOME}/include -I../utils'
        self.CXX_FLAGS = f'{self.COMMON_FLAGS} -I{self.HOME}/include -I../utils'
        shutil.copy2(f'{self.WORKING_DIR}/../utils/glibc_compat_rand.o', f'{self.WORKING_DIR}')
        shutil.copy2(f'{self.WORKING_DIR}/../utils/ImageHelper.o', f'{self.WORKING_DIR}')
        self.LINKER_FLAGS = f'-lm -L{self.HOME}/lib64 -lbenchmark'

        self.BINARY = 'BilateralFilter'
        self.linking_files = 'bilateralFilterKernel.s ImageHelper.o glibc_compat_rand.o main.s'
        self.google_benchmarks = [{
            'name': 'bilateral',
            'run': 'BENCHMARK_BILATERAL_FILTER/64/4'
        }]

class BenchmarkBlur(GoogleBenchmarkMixin, BenchmarkBase):
    def __init__(self, path):
        BenchmarkBase.__init__(self, path)
        self.CC_FLAGS = f'{self.COMMON_FLAGS} -I{self.HOME}/include -I../utils'
        self.CXX_FLAGS = f'{self.COMMON_FLAGS} -I{self.HOME}/include -I../utils'
        shutil.copy2(f'{self.WORKING_DIR}/../utils/glibc_compat_rand.o', f'{self.WORKING_DIR}')
        shutil.copy2(f'{self.WORKING_DIR}/../utils/ImageHelper.o', f'{self.WORKING_DIR}')
        self.LINKER_FLAGS = f'-lm -L{self.HOME}/lib64 -lbenchmark'

        self.BINARY = 'Blur'
        self.linking_files = 'boxBlurKernel.s gaussianBlurKernel.s ImageHelper.o glibc_compat_rand.o main.s'
        self.google_benchmarks = [{
            'name': 'box',
            'run': 'BENCHMARK_boxBlurKernel/1024'
        }, {
            'name': 'gaussian',
            'run': 'BENCHMARK_GAUSSIAN_BLUR/1024'
        }]

class BenchmarkDilate(GoogleBenchmarkMixin, BenchmarkBase):
    def __init__(self, path):
        BenchmarkBase.__init__(self, path)
        self.CC_FLAGS = f'{self.COMMON_FLAGS} -I{self.HOME}/include -I../utils'
        self.CXX_FLAGS = f'{self.COMMON_FLAGS} -I{self.HOME}/include -I../utils'
        shutil.copy2(f'{self.WORKING_DIR}/../utils/glibc_compat_rand.o', f'{self.WORKING_DIR}')
        shutil.copy2(f'{self.WORKING_DIR}/../utils/ImageHelper.o', f'{self.WORKING_DIR}')
        self.LINKER_FLAGS = f'-lm -L{self.HOME}/lib64 -lbenchmark'

        self.BINARY = 'Dilate'
        self.linking_files = 'dilateKernel.s ImageHelper.o glibc_compat_rand.o main.s'
        self.google_benchmarks = [{
            'name': 'dilate',
            'run': 'BENCHMARK_DILATE/1024'
        }]

class BenchmarkDither(GoogleBenchmarkMixin, BenchmarkBase):
    def __init__(self, path):
        BenchmarkBase.__init__(self, path)
        self.CC_FLAGS = f'{self.COMMON_FLAGS} -I{self.HOME}/include -I../utils'
        self.CXX_FLAGS = f'{self.COMMON_FLAGS} -I{self.HOME}/include -I../utils'
        shutil.copy2(f'{self.WORKING_DIR}/../utils/glibc_compat_rand.o', f'{self.WORKING_DIR}')
        shutil.copy2(f'{self.WORKING_DIR}/../utils/ImageHelper.o', f'{self.WORKING_DIR}')
        self.LINKER_FLAGS = f'-lm -L{self.HOME}/lib64 -lbenchmark'

        self.BINARY = 'Dither'
        self.linking_files = 'floydDitherKernel.s orderedDitherKernel.s ImageHelper.o glibc_compat_rand.o main.s'
        self.google_benchmarks = [{
            'name': 'floyd',
            'run': 'BENCHMARK_FLOYD_DITHER/512'
        }, {
            'name': 'ordered',
            'run': 'BENCHMARK_ORDERED_DITHER/512/8'
        }]

class BenchmarkInterpolation(GoogleBenchmarkMixin, BenchmarkBase):
    def __init__(self, path):
        BenchmarkBase.__init__(self, path)
        self.CC_FLAGS = f'{self.COMMON_FLAGS} -I{self.HOME}/include -I../utils'
        self.CXX_FLAGS = f'{self.COMMON_FLAGS} -I{self.HOME}/include -I../utils'
        shutil.copy2(f'{self.WORKING_DIR}/../utils/glibc_compat_rand.o', f'{self.WORKING_DIR}')
        shutil.copy2(f'{self.WORKING_DIR}/../utils/ImageHelper.o', f'{self.WORKING_DIR}')
        self.LINKER_FLAGS = f'-lm -L{self.HOME}/lib64 -lbenchmark'

        self.BINARY = 'Interpolation'
        self.linking_files = 'bicubicKernel.s bilinearKernel.s ImageHelper.o glibc_compat_rand.o main.s'
        self.google_benchmarks = [{
            'name': 'bicubic',
            'run': 'BENCHMARK_BICUBIC_INTERPOLATION/256'
        }, {
            'name': 'bilinear',
            'run': 'BENCHMARK_BILINEAR_INTERPOLATION/256'
        }]

class BenchmarkOldenBh(PerfBencharkMixin, BenchmarkBase):
    def __init__(self, path):
        BenchmarkBase.__init__(self, path)
        self.CC_FLAGS = f'{self.COMMON_FLAGS} -fcommon -DTORONTO'
        self.LINKER = 'clang'
        self.LINKER_FLAGS = '-lm'
        self.BINARY = 'bh'
        self.BINARY_OPTIONS = '40000 30'
        self.linking_files = 'args.s newbh.s util.s walksub.s'

class BenchmarkOldenBisort(PerfBencharkMixin, BenchmarkBase):
    def __init__(self, path):
        BenchmarkBase.__init__(self, path)
        self.CC_FLAGS = f'{self.COMMON_FLAGS} -DTORONTO'
        self.LINKER = 'clang'
        self.LINKER_FLAGS = '-lm'
        self.BINARY = 'bisort'
        self.BINARY_OPTIONS = '3000000'
        self.linking_files = 'args.s bitonic.s'

class BenchmarkOldenHealth(PerfBencharkMixin, BenchmarkBase):
    def __init__(self, path):
        BenchmarkBase.__init__(self, path)
        self.CC_FLAGS = f'{self.COMMON_FLAGS} -DTORONTO'
        self.LINKER = 'clang'
        self.LINKER_FLAGS = '-lm'
        self.BINARY = 'health'
        self.BINARY_OPTIONS = '10 40 1'
        self.linking_files = 'args.s health.s list.s poisson.s'

class BenchmarkOldenMst(PerfBencharkMixin, BenchmarkBase):
    def __init__(self, path):
        BenchmarkBase.__init__(self, path)
        self.CC_FLAGS = f'{self.COMMON_FLAGS} -DTORONTO'
        self.LINKER = 'clang'
        self.LINKER_FLAGS = ''
        self.BINARY = 'mst'
        self.BINARY_OPTIONS = '4000'
        self.linking_files = 'args.s hash.s main.s makegraph.s'

class BenchmarkOldenTsp(PerfBencharkMixin, BenchmarkBase):
    def __init__(self, path):
        BenchmarkBase.__init__(self, path)
        self.CC_FLAGS = f'{self.COMMON_FLAGS} -DTORONTO'
        self.LINKER = 'clang'
        self.LINKER_FLAGS = '-lm'
        self.BINARY = 'tsp'
        self.BINARY_OPTIONS = '2048000'
        self.linking_files = 'args.s build.s main.s tsp.s'

class BenchmarkMcGill(SingleSourceBenchmarkMixin, BenchmarkBase):
    def __init__(self, path):
        BenchmarkBase.__init__(self, path)
        self.CC_FLAGS = f'{self.COMMON_FLAGS}'
        self.LINKER = 'clang'
        self.LINKER_FLAGS = '-lm'
        self.BINARIES = [
            ('chomp', ''),
            ('queens', ''),
            ('misr', '')
        ]
        self.linking_files_list = ['chomp.s', 'queens.s', 'misr.s']

@click.command()
@click.option('--path', help='benchmark path')
@click.option('--benchmark', help='benchmark type')
@click.option('--mis-heuristic', default='s2v-dqn', help='MIS heuristic')
def run(path, benchmark, mis_heuristic):
    if benchmark == 'harris':
        BenchmarkHarris(path).run(mis_heuristic)
    elif benchmark == 'BilateralFiltering':
        BenchmarkBilateralFiltering(path).run(mis_heuristic)
    elif benchmark == 'Blur':
        BenchmarkBlur(path).run(mis_heuristic)
    elif benchmark == 'Dilate':
        BenchmarkDilate(path).run(mis_heuristic)
    elif benchmark == 'Dither':
        BenchmarkDither(path).run(mis_heuristic)
    elif benchmark == 'Interpolation':
        BenchmarkInterpolation(path).run(mis_heuristic)
    elif benchmark == 'olden/bh':
        BenchmarkOldenBh(path).run(mis_heuristic)
    elif benchmark == 'olden/bisort':
        BenchmarkOldenBisort(path).run(mis_heuristic)
    elif benchmark == 'olden/health':
        BenchmarkOldenHealth(path).run(mis_heuristic)
    elif benchmark == 'olden/mst':
        BenchmarkOldenMst(path).run(mis_heuristic)
    elif benchmark == 'olden/tsp':
        BenchmarkOldenTsp(path).run(mis_heuristic)
    elif benchmark == 'mcgill':
        BenchmarkMcGill(path).run(mis_heuristic)
    else:
        pass

if __name__ == "__main__":
    run()
