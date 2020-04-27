perf record -g -- bash run.sh
perf script -i perf.data| ../FlameGraph/stackcollapse-perf.pl > tmp.txt
../FlameGraph/flamegraph.pl tmp.txt > rsync-dax.svg
rm -rf perf.data tmp.txt
