import numpy as np
import networkx as nx
import pickle as cp
import random
import ctypes
import os
import re
import copy
import click

def get_solution(G):
    val = 0
    sol = []
    G = copy.deepcopy(G)
    while len(G.nodes) > 0:
        max_degree = 0
        max_node = None
        del_nodes = []
        for n, d in G.degree:
            if d > max_degree:
                max_degree = d
                max_node = int(n)
            elif d == 0:
                del_nodes.append(n)
        if max_node is not None:
            del_nodes.append(max_node)
        G.remove_nodes_from(del_nodes)
        if max_node is not None:
            sol.append(max_node)
        val += len(del_nodes)
    return val, sol

@click.command()
@click.option('--data-dir', help='Data directory')
@click.option('--output-dir', help='Output directory')
@click.option('--isec', type=int, help='Independent set extraction count')
def run(data_dir, output_dir, isec):
    for filename in os.listdir(data_dir):
        if filename.endswith(".export.pp2graph.pkl"):
            filepath = f"{data_dir}/{filename}"
            with open(filepath, 'rb') as f:
                g = cp.load(f)
                g = nx.convert_node_labels_to_integers(g)
                for i in range(isec):
                    frac = 0
                    filename = re.sub('\.pkl$', '', filename)
                    result_file = f'{output_dir}/{filename}.clr.{i}'
                    with open(result_file, 'w') as f_out:
                        print(filename)
                        val, sol = get_solution(g)
                        for n in sol:
                            f_out.write(f'{n} ')

                        mis_nodes = set(g.nodes).difference(set(sol))
                        g.remove_nodes_from(mis_nodes)

                        frac += val
                    print('average size of vc: ', frac)

if __name__ == '__main__':
    run()
