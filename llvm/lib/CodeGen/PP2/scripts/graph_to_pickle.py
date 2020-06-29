import os
dir_path = os.path.dirname(os.path.realpath(__file__))

import networkx as nx

for filename in os.listdir(dir_path):
    if filename.endswith(".export.pp2graph"):
        G = nx.read_adjlist(filename)
        nx.write_gpickle(G, filename + ".pkl")
