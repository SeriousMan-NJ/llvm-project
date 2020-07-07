#!/bin/bash

for file in *.ll
do
echo "[REGALLOC] $file"
  llc -O3 -filetype=asm -regalloc pp2 -pp2-dummy-dump-graph -pp2-dummy-export-graph -pp2-isec 8 $file &> /dev/null
done
