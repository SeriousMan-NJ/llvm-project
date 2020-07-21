#!/bin/bash
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/harris --benchmark harris --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/BilateralFiltering --benchmark BilateralFiltering --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/Blur --benchmark Blur --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/Dilate --benchmark Dilate --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/Dither --benchmark Dither --mis-heuristic max-approx
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/Interpolation --benchmark Interpolation --mis-heuristic max-approx
