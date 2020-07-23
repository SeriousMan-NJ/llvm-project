import numpy as np
import networkx as nx
import pickle as cp
import random
import ctypes
import os
import re
import copy
import click
import json

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
    return sol

def get_solution_nw(G, nw):
    val = 0
    sol = []
    G = copy.deepcopy(G)
    while len(G.nodes) > 0:
        max_value = -float("inf")
        max_node = None
        del_nodes = []
        for n, d in G.degree:
            if d == 0:
                del_nodes.append(n)
            elif d*1/nw[n] > max_value:
                max_value = d*1/nw[n]
                max_node = int(n)
        if max_node is not None:
            del_nodes.append(max_node)
        G.remove_nodes_from(del_nodes)
        if max_node is not None:
            sol.append(max_node)
    return sol

@click.command()
@click.option('--data-dir', help='Data directory')
@click.option('--output-dir', help='Output directory')
@click.option('--isec', type=int, help='Independent set extraction count')
@click.option('--use-node-weights', type=bool, default=False, help='Use node weights')
def run(data_dir, output_dir, isec, use_node_weights):
    for filename in os.listdir(data_dir):
        if filename.endswith(".export.pp2graph.pkl"):
            filepath = os.path.join(data_dir, filename)
            nodeweightfilepath = re.sub('\.pp2graph.pkl$', '.nw.json', filepath)
            with open(filepath, 'rb') as f, open(nodeweightfilepath, 'r') as g:
                G = nx.read_gpickle(f)
                mapping = {n: int(n) for n in G.nodes}
                G = nx.relabel_nodes(G, mapping)
                nw = json.load(g)
                nw = {int(k): float(v) for k, v in nw.items()}
                for i in range(isec):
                    filename = re.sub('\.pkl$', '', filename)
                    result_file = f'{output_dir}/{filename}.clr.{i}'
                    with open(result_file, 'w') as f_out:
                        print(filename)
                        if use_node_weights:
                            sol = get_solution_nw(G, nw)
                        else:
                            sol = get_solution(G)
                        for n in sol:
                            f_out.write(f'{n} ')

                        mis_nodes = set(G.nodes).difference(set(sol))
                        G.remove_nodes_from(mis_nodes)

                    print('average size of vc: ', len(sol))

if __name__ == '__main__':
    run()
