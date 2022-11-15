#perf record -g -- ./db_bench --db=/mnt/pmem-fsdax0/takh/rocksdb --benchmarks="fillseq,readrandom,readseq"
#perf script | ~/git-repos/nvm-file-indexing/benchmarks/FlameGraph/stackcollapse-perf.pl|~/git-repos/nvm-file-indexing/benchmarks/FlameGraph/flamegraph.pl > /tmp/test.svg
output_dir=/home/takh/git-repos/nvm-file-indexing/benchmarks/rocksdb
db_dir=/mnt/pmem-fsdax0/takh/rocksdb
flame_path=/home/takh/git-repos/nvm-file-indexing/benchmarks/FlameGraph
rm -rf tmp;
mkdir tmp;
rm -rf $output_dir/*.svg
#cat benchmarks.txt|
while read i;do
  echo $i;
  rm -rf $db_dir/*
  rm -rf perf.data
  if [[ "$i" =~ ^fill.* ]] || [[ "$i" =~ ^randomreplacekeys.* ]] || [[ "$i" =~ ^timeseries.* ]] ;
  then
    echo "Not doing an initial fill load";
    perf record -g -- ./db_bench --db=$db_dir --benchmarks="$i" &> tmp/$i.txt;
  else
    echo "Doing an initial fill load";
    ./db_bench --db=$db_dir --benchmarks="fillseq" &> tmp/initial_load.txt;
    perf record -g -- ./db_bench --db=$db_dir --use_existing_db --benchmarks="$i" &> tmp/$i.txt;
  fi
  perf script|$flame_path/stackcollapse-perf.pl|$flame_path/flamegraph.pl>$output_dir/$i.svg
  rm -rf perf.data
  rm -rf $db_dir/*
done <benchmarks.txt
