import subprocess as sp
import click
import json
import os
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator
import numpy as np

def get_files(record, working_dir):
    if len(record['files']) == 0:
        files = set()
        for filename in os.listdir(working_dir):
            if len(filename.split(".")) >= 3 and filename.endswith(".s"):
                files.insert(filename.split(".")[0])
        return list(files)
    else:
        return record['files']

def get_loc(filepath):
    process = sp.Popen(f'cat {filepath} | wc -l',
        shell=True,
        executable='/bin/bash',
        stdout=sp.PIPE,
        stderr=sp.PIPE)
    stdout, stderr = process.communicate()
    assert process.returncode == 0, f"Cannot get LoC of {filepath}"
    return int(stdout)

def get_spills(filepath):
    process = sp.Popen(f'cat {filepath} | rg "\-byte Spill" | wc -l',
        shell=True,
        executable='/bin/bash',
        stdout=sp.PIPE,
        stderr=sp.PIPE)
    stdout, stderr = process.communicate()
    assert process.returncode == 0, f"Cannot get Spills of {filepath}"
    return int(stdout)

def get_reloads(filepath):
    reloads = 0
    process = sp.Popen(f'cat {filepath} | rg "\-byte Reload" | wc -l',
        shell=True,
        executable='/bin/bash',
        stdout=sp.PIPE,
        stderr=sp.PIPE)
    stdout, stderr = process.communicate()
    assert process.returncode == 0, f"Cannot get Reloads of {filepath}"
    reloads += int(stdout)

    process = sp.Popen(f'cat {filepath} | rg "\-byte Folded Reload" | wc -l',
        shell=True,
        executable='/bin/bash',
        stdout=sp.PIPE,
        stderr=sp.PIPE)
    stdout, stderr = process.communicate()
    assert process.returncode == 0, f"Cannot get Folded reloads of {filepath}"
    reloads += int(stdout)
    return reloads

def get_nodes(dir, file, i):
    if i == 0:
        return 0

    num_nodes = 0
    i -= 1
    for filename in os.listdir(dir):
        filepath = f'{dir}/{filename}'
        if filename.startswith(file) and filename.endswith(".export.pp2graph"):
            process = sp.Popen(f'cat {filepath} | wc -l',
            shell=True,
            executable='/bin/bash',
            stdout=sp.PIPE,
            stderr=sp.PIPE)
            stdout, stderr = process.communicate()
            assert process.returncode == 0
            num_nodes += int(stdout)
        if filename.startswith(file) and filename.endswith(f".clr.{i}"):
            with open(filepath, 'r') as f:
                nodes = f.read()
                num_nodes -= len(nodes.split())

    return num_nodes

def plot(isec, labels, data, y_label, filename, preprocess_func, data_type=None):
    ISEC = isec + 1

    x = np.arange(len(labels))  # the label locations
    width = 0.8  # the width of the bars

    fig, ax = plt.subplots()
    rects_list = []
    for i in range(ISEC):
        ax.bar(x - (8 - i)*width/ISEC, preprocess_func(np.transpose(data), data_type=data_type, isec=isec)[i], width/ISEC, label=i, lw=0.1)
        rects_list.append(ax)

    # Add some text for labels, title and custom x-axis tick labels, etc.
    ax.set_ylabel(y_label)
    if preprocess_func == preprocess_ratio:
        if data_type == "loc":
            ax.set_ylim([0.95,1.05])
            ax.set_yticks(np.arange(0.95, 1.05, 0.01))
        else:
            pass
    else:
        ax.yaxis.set_major_locator(MaxNLocator(integer=True))
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.grid(True, axis='y', alpha=1, lw=0.2)
    ax.legend(loc='best', fontsize='xx-small', ncol=2)
    plt.setp(ax.get_xticklabels(), rotation=60, horizontalalignment='right')

    fig.tight_layout()

    plt.savefig(filename, dpi=300)

def preprocess_diff(data, **kwargs):
    return [d - data[0] for d in data]

def preprocess_ratio(data, data_type=None, isec=None):
    if data_type == 'nodes':
        return [d / data[isec + 1] for d in data]
    else:
        return [d / data[0] for d in data]

def preprocess_step_diff(data, **kwargs):
    result = [0]
    for i in range(1, len(data)):
        result.append(data[i] - data[i - 1])
    return result

@click.command()
@click.option('--sub-dir', default="./", help='Sub directory where files exist')
@click.option('--isec', type=int, default=16, help='Independent set extraction count')
def run(sub_dir, isec):
    labels = []
    locs = []
    num_spills = []
    num_reloads = []
    num_nodes = []
    with open('benchmarks.json') as f:
        data = json.load(f)
        for record in data:
            working_dir = os.path.join(record['path'], sub_dir)
            files = get_files(record, working_dir)

            for file in files:
                labels.append(file)
                locs.append([])
                num_spills.append([])
                num_reloads.append([])
                num_nodes.append([])
                for i in range(isec + 1):
                    filepath = f'{working_dir}/{file}.{i}.s'
                    locs[-1].append(get_loc(filepath))
                    num_spills[-1].append(get_spills(filepath))
                    num_reloads[-1].append(get_reloads(filepath))
                    num_nodes[-1].append(get_nodes(working_dir, file, i))
                num_nodes[-1].append(get_nodes(working_dir, file, -1))

    plot(isec, labels, locs, "LoC", f"{sub_dir}_loc_diff.png", preprocess_diff)
    plot(isec, labels, locs, "LoC", f"{sub_dir}_loc_ratio.png", preprocess_ratio, data_type="loc")
    plot(isec, labels, num_spills, "# of Spills", f"{sub_dir}_spills_diff.png", preprocess_diff)
    plot(isec, labels, num_spills, "# of Spills", f"{sub_dir}_spills_ratio.png", preprocess_ratio)
    plot(isec, labels, num_reloads, "# of Reloads", f"{sub_dir}_reloads_diff.png", preprocess_diff)
    plot(isec, labels, num_reloads, "# of Reloads", f"{sub_dir}_reloads_ratio.png", preprocess_ratio)
    plot(isec, labels, num_nodes, "# of MIS nodes", f"{sub_dir}_nodes_diff.png", preprocess_step_diff)
    plot(isec, labels, num_nodes, "# of MIS nodes", f"{sub_dir}_nodes_ratio.png", preprocess_ratio, data_type="nodes")

if __name__ == '__main__':
    run()
