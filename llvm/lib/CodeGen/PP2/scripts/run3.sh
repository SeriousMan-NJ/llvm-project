for file in *.ll
do
echo "[REGALLOC] $file"
  llc -O3 -filetype=obj -regalloc pp2 -pp2-dummy-dump-graph -pp2-dummy-export-graph -pp2-skip $file
done

