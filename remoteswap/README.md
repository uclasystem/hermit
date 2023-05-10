# Remoteswap
A RDMA-based remote swap system for remote memory and disaggregated clusters. The codebase is larged adopted from Fastswap (https://github.com/clusterfarmem/fastswap) and Canvas (https://github.com/uclasystem/canvas), but is enhanced with our optimizations.

## Prerequisites
Remoteswap is developed and tested under the following settings:

Hardware:
* Infiniband: Mellanox ConnectX-3/4 (40Gbps), or Mellanox ConnectX-6 (100Gbps)
* RoCE: Mellanox ConnectX-5 25GbE, or Mellanox ConnectX-5 Ex 100GbE

Software:
* OS: Ubuntu 18.04/20.04
* gcc 7.5.0/9.4.0
* Mellanox OFED driver: 5.4-3.1.0.0-LTS (for ConnectX-5 and 6), or 4.9-4.1.7.0-LTS (for ConnectX-3).

## Build & Install

### Install MLNX_OFED driver
Here is an example on Ubuntu 20.04:
```bash
# Download the MLNX OFED driver for the Ubuntu 20.04
wget https://content.mellanox.com/ofed/MLNX_OFED-5.4-3.1.0.0/MLNX_OFED_LINUX-5.4-3.1.0.0-ubuntu20.04-x86_64.tgz
tar xzf MLNX_OFED_LINUX-5.4-3.1.0.0-ubuntu20.04-x86_64.tgz
cd MLNX_OFED_LINUX-5.4-3.1.0.0-ubuntu20.04-x86_64

# Install the MLNX OFED driver against linux-5.14-rc5
sudo ./mlnxofedinstall --add-kernel-support --force
# restart openibd service as required
sudo systemctl restart openibd
# reboot
sudo reboot
```

### Client
On the host machine,
```bash
cd remoteswap/client
make
```

There should be a kernel module called `rswap-client.ko` under the client/ directory, which indicates a build success.

### Server
On the memory server,
```bash
cd remotswap/server
make
```

We should get a `rswap-server` after the compilation.

### DRAM client backend

Similar to Fastswap, remoteswap supports debugging using local memory as a fake "remote memory pool". In such case, there is no "remoteswap-server", and the "remoteswap-client" should be built in the following way:

```bash
# (under the remoteswap/client dir)
make BACKEND=DRAM
```

## Usage

1. On the memory server, run `rswap-server` first. This process must be alive all the time so either run it inside `tmux` or `screen`, or run it as a system service.

For now, we have to know the online core number of the **host server** first. You can check `/proc/cpuinfo` on the host server or simply get the number via `top` or `htop`.
A wrong core number will crash the kernel module.

```bash
cd remoteswap/server
./rswap-server <memory server ip> <memory server port> <memory pool size in GB> <number of cores on host server>
# an example: ./rswap-server 10.0.0.4 9400 48 32
```

2. On the host server, edit the parameters in `manage_rswap_client.sh` under `remoteswap/client` directory.

Here is an excerpt of the script:

```bash
# The swap file/partition size should be equal to the whole size of remote memory
SWAP_PARTITION_SIZE="48"

server_ip="10.0.0.2"
server_port="9400"
swap_file="/mnt/swapfile"
```

Make sure that "SWAP_PARTITION_SIZE" equals to the remote memory pool size you set when running `rswap-server`, as well as "server_ip" and "server_port" here.

"swap_file" is the path which the script uses to create a special file as a fake swap partition in the size of `SWAP_PARTITION_SIZE`GB. The script will create the file by itself but the path must be valid. The path can be "/mnt/swapfile" here, or somewhere under your home like "${HOME}/aaa/bbb".

3. On the host server, install the `rswap-client` kernel module, which will establish the connection to the server and finalize the setup.

```bash
./manage_rswap_client.sh install
```

The script should have done everything but in case here is the command to install the kernel module manually:

```bash
sudo insmod ./rswap-client.ko sip=${mem_server_ip} sport=${mem_server_port} rmsize=${SWAP_PARTITION_SIZE_GB}
```

It might take a while to allocate and register all memory in the memory pool, and establish the connection. The system should have been fully set up now.

You can optionally check system log via `dmesg`. A success should look like (1 chunk is 4GB so 12 chunks are essentially 48GB remote memory):
```
rswap_request_for_chunk, Got 12 chunks from memory server.
rdma_session_connect,Exit the main() function with built RDMA conenction rdma_session_context:0xffffffffc08f7460.
frontswap module loaded
```

4. To uninstall the kernel module, one can run the following command on the host server
```bash
cd remoteswap/client
sudo ./manage_rswap_client.sh uninstall
```

## Acknowledgement
The codebase is largely adopted from Fastswap (https://github.com/clusterfarmem/fastswap) and Canvas (https://github.com/uclasystem/canvas).

## Contact
Please contact yifanqiao [at] g [dot] ucla [dot] edu for assistance.
