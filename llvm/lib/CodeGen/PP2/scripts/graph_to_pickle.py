import os
import networkx as nx
import click

@click.command()
@click.option('--path', help='clr files directory path')
def run(path):
    for filename in os.listdir(path):
        filename = os.path.join(path, filename)
        if filename.endswith(".export.pp2graph"):
            G = nx.read_adjlist(filename)
            nx.write_gpickle(G, filename + ".pkl")

if __name__ == '__main__':
    run()
