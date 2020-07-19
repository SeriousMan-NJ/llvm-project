#!/bin/bash
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/harris --benchmark harris
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/BilateralFiltering --benchmark BilateralFiltering
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/Blur --benchmark Blur
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/Dilate --benchmark Dilate
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/Dither --benchmark Dither
python benchmark.py --path /home/ywshin/test-suite/MicroBenchmarks/ImageProcessing/Interpolation --benchmark Interpolation

