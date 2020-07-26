import matplotlib.pyplot as plt
import networkx as nx
import json
import os
import numpy as np
from scipy import stats

NUM_MIN_NODES = 0

def run():
    ne_pairs = []
    with open('benchmarks.json') as f:
        data = json.load(f)
        for record in data:
            for filename in os.listdir(record['path']):
                if filename.endswith(".export.pp2graph"):
                    filepath = os.path.join(record['path'], filename)
                    G = nx.read_adjlist(filepath)

                    # remove trivial nodes
                    del_nodes = []
                    for n, d in G.degree:
                        if d == 0:
                            del_nodes.append(n)
                    G.remove_nodes_from(del_nodes)

                    C = nx.connected_components(G)
                    for S in C:
                        if len(S) > NUM_MIN_NODES:
                            num_neighbors = 0
                            for n in S:
                                num_neighbors += len(list(nx.neighbors(G, n)))
                            (num_neighbors / 2).is_integer()
                            ne_pairs.append((len(S), num_neighbors // 2))

    nodes = []
    probs = []
    for n, e in ne_pairs:
        nodes.append(n)
        probs.append(2 * e / n / (n - 1))
    # print(probs)

    plt.hist(probs, bins=np.arange(0, 1, 0.05))
    plt.xticks(np.arange(0, 1, 0.1))
    plt.tight_layout()
    plt.savefig(f'graph_statistics.p.{NUM_MIN_NODES}.png', dpi=300)
    print(f'AMEAN ({NUM_MIN_NODES}; probs): {np.mean(probs)}')
    print(f'GMEAN ({NUM_MIN_NODES}; probs): {stats.gmean(probs)}')
    plt.clf()

    plt.hist(nodes)
    plt.tight_layout()
    plt.savefig(f'graph_statistics.n.{NUM_MIN_NODES}.png', dpi=300)
    print(f'MEAN ({NUM_MIN_NODES}; nodes): {np.mean(nodes)}')
    plt.clf()

if __name__ == "__main__":
    run()
