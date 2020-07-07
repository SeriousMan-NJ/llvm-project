#!/bin/bash
WORKING_DIR=$PWD


for file in *.c
do
  if [ ! -f $file ]; then
    break
  fi
  echo "[COMPILE] $file"
  clang -O3 -S -emit-llvm $file
done


for file in *.cpp
do
  if [ ! -f $file ]; then
    break
  fi
  echo "[COMPILE] $file"
  clang++ -O3 -S -emit-llvm $file
done


for file in *.ll
do
echo "[PROCESS] $file"
  llc -O3 -regalloc pp2 -pp2-dummy-dump-graph -pp2-dummy-export-graph -pp2-skip $file &> /dev/null
done


conda activate py27
python graph_to_pickle.py


cd $HOME/graph_comb_opt/code/s2v_mvc
. run_eval.sh
while [ $? -ne 0 ]
do
. run_eval.sh
done


cd $WORKING_DIR
