import matplotlib.pyplot as plt
import numpy as np
import click
import csv
import json
from scipy import stats

@click.command()
@click.option('--path', help='benchmark path')
@click.option('--benchmark', help='benchmark type')
@click.option('--csv-name', help='csv file name')
def run(path, benchmark, csv_name):
    labels = []
    means_normalized = []
    stddevs_normalized = []

    with open('benchmarks.json') as f:
        data = json.load(f)

        for record in data:
            labels.append(record['name'])

            path = record['path']
            csv_name = record['csv_name']

            with open(f'{path}/{csv_name}', 'r') as file:
                reader = csv.reader(file)
                for row in reader:
                    means = row[:17]
                    stddevs = row[17:]
                    means_normalized.append([float(row[0]) / float(m) for m in means])
                    stddevs_normalized.append([float(s) / float(row[0]) for s in stddevs])

    x = np.arange(len(labels))  # the label locations
    width = 0.8  # the width of the bars

    fig, ax = plt.subplots()
    rects_list = []
    for i in range(17):
        # errorbar(np.transpose(stddevs_normalized)[i]
        ax.bar(x - (8 - i)*width/17, np.transpose(means_normalized)[i], width/17, label=i, lw=0.1, yerr=np.transpose(stddevs_normalized)[i], error_kw=dict(ecolor='gray', lw=0.15, capsize=0.75, capthick=0.15))
        rects_list.append(ax)

    # Add some text for labels, title and custom x-axis tick labels, etc.
    ax.set_ylabel('speedup')
    ax.set_ylim([0.97,1.07])
    ax.set_yticks(np.arange(0.97, 1.07, 0.01))
    # ax.set_title('Scores by group and gender')
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.grid(True, axis='y', alpha=1, lw=0.2)
    # ax.legend(ncol=2)
    plt.setp(ax.get_xticklabels(), rotation=60, horizontalalignment='right')

    fig.tight_layout()

    for i in range(len(labels)):
        for j, m in enumerate(means_normalized[i]):
            if m < 0.97:
                ax.annotate(round(m, 3), xy=(i - (8 - j)*width/17, 0.97), xytext=(i - (9 - j)*width/17, 0.9635), xycoords='data', annotation_clip=False, fontsize=5, arrowprops=dict(facecolor='gray', arrowstyle='->'))
            elif m > 1.07:
                ax.annotate(round(m, 3), xy=(i - (8 - j)*width/17, 1.07), xytext=(i - (9 - j)*width/17, 1.0635), xycoords='data', annotation_clip=False, fontsize=5, arrowprops=dict(facecolor='gray', arrowstyle='->'))

    plt.savefig('statistics.png', dpi=300)

    max_means = [max(m[1:]) for m in means_normalized]
    avg_means = [np.mean(m[1:]) for m in means_normalized]
    min_means = [min(m[1:]) for m in means_normalized]
    print(f"Max: {stats.gmean(max_means)}")
    print(f"Avg: {stats.hmean(avg_means)}")
    print(f"Min: {stats.gmean(min_means)}")

if __name__ == "__main__":
    run()
