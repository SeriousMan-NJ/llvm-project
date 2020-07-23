#!/bin/bash

# MicroBenchmarks
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/harris --benchmark harris --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/BilateralFiltering --benchmark BilateralFiltering --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/Blur --benchmark Blur --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/Dilate --benchmark Dilate --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/Dither --benchmark Dither --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/Interpolation --benchmark Interpolation --mis-heuristic max-approx

# Olden
python benchmark.py --path /home/ywshin/test-suite/MultiSource/Benchmarks/Olden/bh --benchmark olden/bh --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MultiSource/Benchmarks/Olden/bisort --benchmark olden/bisort --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MultiSource/Benchmarks/Olden/health --benchmark olden/health --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MultiSource/Benchmarks/Olden/mst --benchmark olden/mst --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MultiSource/Benchmarks/Olden/tsp --benchmark olden/tsp --mis-heuristic max-approx

# McGill
python benchmark.py --path /home/ywshin/test-suite/SingleSource/Benchmarks/McGill --benchmark mcgill --mis-heuristic max-approx
