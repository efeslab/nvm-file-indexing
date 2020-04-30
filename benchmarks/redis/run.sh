YCSB_PATH=/home/takh/git-repos/YCSB
output_dir=/home/takh/git-repos/nvm-file-indexing/benchmarks/redis
flame_path=/home/takh/git-repos/nvm-file-indexing/benchmarks/FlameGraph
for i in a b c d e f;
do
  rm -rf /mnt/pmem-fsdax0/takh/redis/*
  src/redis-server redis.conf --daemonize yes #&>/dev/null & disown; #&
  server_pid=$(pgrep redis) #$!
  dirs -c
  pushd .
  cd $YCSB_PATH
  ./bin/ycsb load redis -s -P workloads/workload$i -p "redis.host=127.0.0.1" -p "redis.port=6379"
  perf record -o /tmp/perf.data -g -p $server_pid -- ./bin/ycsb run redis -s -P workloads/workload$i -p "redis.host=127.0.0.1" -p "redis.port=6379"
  popd
  kill -SIGINT $server_pid
  # kill -9 $server_pid
  sleep 2
  wait
  perf script -i /tmp/perf.data |$flame_path/stackcollapse-perf.pl|$flame_path/flamegraph.pl>$output_dir/$i.svg
  rm -rf /tmp/perf.data
  rm -rf /mnt/pmem-fsdax0/takh/redis/*
done
