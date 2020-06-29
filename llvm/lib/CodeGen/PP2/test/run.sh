#!/bin/bash
WORKING_DIR=$PWD


for file in *.c
do
  echo "[COMPILE] $file"
  clang -S -emit-llvm $file
done


for file in *.ll
do
  echo "[PROCESS] $file"
  llc -pp2-dummy-dump-graph -pp2-dummy-export-graph -pp2-skip $file &> /dev/null
done


conda activate py27
python graph_to_pickle.py


cd $HOME/graph_comb_opt/code/s2v_mvc
. run_eval.sh


cd $WORKING_DIR
for file in *.ll
do
  echo "[REGALLOC] $file"
  llc -filetype=obj -pp2-dummy-dump-graph -pp2-dummy-export-graph $file &> /dev/null
done
