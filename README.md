# Hermit
Hermit is a new swap system designed for low-latency and high-throughput remote memory. Hermit employs *feedback-directed asynchrony* to reduce remote access latency and improve CPU efficiency for swapping. Please find more details in our NSDI'23 [paper](https://www.usenix.org/conference/nsdi23/presentation/qiao).

## Prerequisites
You will need two servers with RDMA connection to use Hermit. One will serve as the host server and another will serve as the memory server (remote memory pool). Hermit is developed and tested under the following settings:

Hardware:
* Infiniband: Mellanox ConnectX-3/4 (40Gbps), or Mellanox ConnectX-6 (100Gbps)
* RoCE: Mellanox ConnectX-5 25GbE, or Mellanox ConnectX-5 Ex 100GbE

Software:
* OS: Ubuntu 18.04/20.04
* gcc 7.5.0/9.4.0
* Mellanox OFED driver: 5.4-3.1.0.0-LTS (for ConnectX-5 and 6), or 4.9-4.1.7.0-LTS (for ConnectX-3).

## Build & Install Hermit
Next we will use Ubuntu 20.04 as an example to show how to build and install the kernel. It is not required but highly recommended to have the same kernel version for both host and memory server.

(1) Change the grub parameters (at least on host server)
```bash
sudo vim /etc/default/grub

# Set the boot kernel version as 5.14-rc5
GRUB_DEFAULT="Advanced options for Ubuntu>Ubuntu, with Linux 5.14-rc5"

# Change the value of GRUB_CMDLINE_LINUX to set transparent hugepage as madvise:
GRUB_CMDLINE_LINUX="transparent_hugepage=madvise"

# Apply the change
sudo update-grub
```

(2) Build && install Hermit kernel
```bash
# Change to the kernel folder:
cd hermit/linux-5.14-rc5

# In case new kernel options are prompted, press enter to use the default options.
cp config .config
sudo ./build_kernel.sh build
sudo ./build_kernel.sh install
# (optional) install built kernel only.
# sudo ./build_kernel.sh replace
sudo reboot
```

### Install remoteswap kernel module
Follow the README in 'hermit/remoteswap' for the instructions.

## Run applications on Hermit
We use cgroup-v1 to limit application's memory usage.

Here is an example to set the cgroup memory limit and run an application:
```bash
mkdir /sys/fs/cgroup/memory/<cgroup name>
echo <limit size, e.g., 2560m> > /sys/fs/cgroup/memory/<cgroup name>/memory.limit_in_bytes
cgexec --sticky -g memory:<cgroup name> taskset -c <cores> /usr/bin/time -v <command>
```

### Customization
We export several configurable parameters and flags via debugfs under `/sys/kernel/debug/hermit`.

Blow is our default configuration and explanation of each parameter. Feel free to enable/disable each of them for performance breakdown.
```bash
echo Y > /sys/kernel/debug/hermit/vaddr_swapout # to reduce the overhead of reverse mapping. We record a PA->VA mapping manually for now
echo Y > /sys/kernel/debug/hermit/batch_swapout # batched paging out dirty pages in the page reclamation
echo Y > /sys/kernel/debug/hermit/swap_thread # enable async page reclamation (swap-out)
echo Y > /sys/kernel/debug/hermit/bypass_swapcache # for pages that are not shared by multiple PTEs, we can skip adding it into the swap cache and map it directly to the faulting PTE
echo Y > /sys/kernel/debug/hermit/speculative_io # speculative RDMA read
echo Y > /sys/kernel/debug/hermit/speculative_lock # speculative mmap_lock. Mostly useful for java applications
echo N > /sys/kernel/debug/hermit/prefetch_thread # disable async prefetching for now

echo 16 > /sys/kernel/debug/hermit/sthd_cnt # number of page reclamation threads. The default cores these threads run on are set in the kernel (https://github.com/ivanium/linux-5.14-rc5/blob/separate-swapout-codepath/mm/hermit.c#L114)
```

### Collect performance stats for monitoring/debugging
To ensure stats are in correct time metrics, please set CPU frequency in kernel first:
* [RMGRID_CPU_FREQ](linux-5.14-rc5/include/linux/adc_macros.h#L9): should be set to the CPU base frequency in MHz

We offer a helper script [`tools/hermit/syscaller.py`](linux-5.14-rc5/tools/hermit/syscaller.py) to collect and reset stats counters.
* `python3 tools/hermit/syscaller.py stats` will show the page fault handling latencies, breakdowns, and other statistics about swap in `dmesg`. It also prints the prefetching contribution and accuracy in the terminal.
* `python3 tools/hermit/syscaller.py reset` will reset the stats and latencies numbers in kernel, which enables recollecting stats for the next run.

## Reference
Please refer to our NSDI'23 paper, [Hermit: Low-Latency, High-Throughput, and Transparent Remote Memory via Feedback-Directed Asynchrony](https://www.usenix.org/conference/nsdi23/presentation/qiao) for more details.
### Bibtex
```txt
@inproceedings {yifan2023hermit,
author = {Yifan Qiao and Chenxi Wang and Zhenyuan Ruan and Adam Belay and Qingda Lu and Yiying Zhang and Miryung Kim and Guoqing Harry Xu},
title = {Hermit: {Low-Latency}, {High-Throughput}, and Transparent Remote Memory via {Feedback-Directed} Asynchrony},
booktitle = {20th USENIX Symposium on Networked Systems Design and Implementation (NSDI 23)},
year = {2023},
isbn = {978-1-939133-33-5},
address = {Boston, MA},
pages = {181--198},
url = {https://www.usenix.org/conference/nsdi23/presentation/qiao},
publisher = {USENIX Association},
month = apr,
}
```

## Contact
Please contact yifanqiao [at] g [dot] ucla [dot] edu for assistance.