## Running TMPFS experiments

```shell
# Need to give yourself CAP_SYS_ADMIN
vim /etc/security/capability.conf
# ...

# This allows perf to see kernel functions 
sudo sysctl -w kernel.kptr_restrict=0

perf record -g <some command> # outputs perf.data
perf script -i perf.data | ../FlameGraph/stackcollapse-perf.pl > out.perf-folded
../FlameGraph/flamegraph.pl tmpfs.perf-folded > tmpfs.svg
```
