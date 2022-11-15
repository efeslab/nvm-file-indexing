run_filebench()
{
  filename=$1
  rm -rf /mnt/pmem-fsdax0/takh/*
  perf record -g -- filebench -f $filename.f 
  perf script -i perf.data| ../FlameGraph/stackcollapse-perf.pl > tmp.txt
  ../FlameGraph/flamegraph.pl tmp.txt > filebench-$filename.svg
  rm -rf perf.data tmp.txt
  rm -rf /mnt/pmem-fsdax0/takh/*
}
bash disable-aslr.sh
for i in fileserver  varmail  webproxy  webserver
do
  run_filebench $i
done
bash enable-aslr.sh
