cur_dir=`readlink -f ./`
rsync_dir=$cur_dir/src
workload_dir=$cur_dir/workload
pmem_dir=/mnt/pmem-fsdax0/takh

ulimit -c unlimited

rm -rf $pmem_dir/*


cp -r $workload_dir/src $pmem_dir && sync
cd $pmem_dir
mkdir dest
( time cp -r ./src dest/ ) 2>&1 | tee $cur_dir/results_time.txt

rm -rf $pmem_dir/*

cd $cur_dir
