#! /bin/bash


### Macros ###
mem_server_ip="10.0.0.2"
mem_server_port="9400"

if [ -z "${HOME}" ]; then
	echo "set home_dir first."
	exit 1
else
	home_dir=${HOME}
fi

swap_file="${home_dir}/swapfile"
# The swap file/partition size should be equal to the whole size of remote memory
SWAP_PARTITION_SIZE_GB="48"

echo " !! Warning, check the parameters below : "
echo " Assigned memory server IP ${mem_server_ip} Port ${mem_server_port}"
echo " swapfile ${swap_file}, size ${SWAP_PARTITION_SIZE_GB} GB"
echo " "
echo " "

### Action ###
action=$1
if [[ -z "${action}" ]]; then
	echo "This shellscipt for Infiniswap pre-configuration."
	echo "Run it with sudo or root"
	echo ""
	echo "Please select what to do: [install | replace | uninstall]"

	read action
fi

function close_swap_partition() {
	# For ubuntu, usually a file is used as swap space
	swap_bd=$(swapon -s | grep "swap.img" | cut -d " " -f 1)
	# the name can also be swapfile
	if [ -z "${swap_bd}" ]; then
		swap_bd=$(swapon -s | grep "swapfile" | cut -d " " -f 1)
	fi

	if [[ -z "${swap_bd}" ]]; then
		echo "Nothing to close."
	else
		echo "Swap Partition to close :${swap_bd} "
		sudo swapoff "${swap_bd}"
	fi

	# Check
	echo "Current swap partition:"
	swapon -s
}

function create_swap_file() {
	if [[ -e ${swap_file} ]]; then
		echo "Please confirm the size of swapfile match the expected ${SWAP_PARTITION_SIZE_GB}G"
		cur_size=$(du -sh ${swap_file} | awk '{print $1;}' | tr -cd '[[:digit:]]')
		if [[ ${cur_size} -ne "${SWAP_PARTITION_SIZE_GB}" ]]; then
			echo "Current ${swap_file}: ${cur_size}G NOT equal to expected ${SWAP_PARTITION_SIZE_GB}G"
			echo "Delete it"
			sudo rm ${swap_file}

			echo "Create a file, ~/swapfile, with size ${SWAP_PARTITION_SIZE_GB}G as swap device."
			sudo fallocate -l ${SWAP_PARTITION_SIZE_GB}G ${swap_file}
			sudo chmod 600 ${swap_file}
		else
			echo "Existing swapfile ${swap_file} , ${cur_size}GB is euqnal or larger than we want, ${SWAP_PARTITION_SIZE_GB}GB. Reuse it."
		fi
	else
		# does not exist, create a swapfile
		echo "Create a file, ~/swapfile, with size ${SWAP_PARTITION_SIZE_GB}G as swap device."
		sudo fallocate -l ${SWAP_PARTITION_SIZE_GB}G ${swap_file}
		sudo chmod 600 ${swap_file}
		du -sh ${swap_file}
	fi

	sleep 1
	echo "Mount the ${swap_file} as swap device"
	sudo mkswap ${swap_file}
	sudo swapon ${swap_file}

	# Check
	swapon -s
}

if [[ "${action}" = "install" ]]; then
	echo "Close current swap partition && Create swap file"
	close_swap_partition

	create_swap_file

	echo "insmod ./rswap-client.ko sip=${mem_server_ip} sport=${mem_server_port} rmsize=${SWAP_PARTITION_SIZE_GB}"
	sudo insmod ./rswap-client.ko sip=${mem_server_ip} sport=${mem_server_port} rmsize=${SWAP_PARTITION_SIZE_GB}

elif [[ "${action}" = "replace" ]]; then
	echo "rmmod rswap-client"
	sudo rmmod rswap-client
	echo "Please restart rswap-server on mem server. Press <Enter> to continue..."

	read
	echo "insmod ./rswap-client.ko sip=${mem_server_ip} sport=${mem_server_port} rmsize=${SWAP_PARTITION_SIZE_GB}"
	sudo insmod ./rswap-client.ko sip=${mem_server_ip} sport=${mem_server_port} rmsize=${SWAP_PARTITION_SIZE_GB}

elif [[ "${action}" = "uninstall" ]]; then
	echo "Close current swap partition"
	close_swap_partition

	echo "rmmod rswap-client"
	sudo rmmod rswap-client

elif [[ "${action}" = "create_swap" ]]; then
	echo "Check the existing swapfile"
	close_swap_partition

	echo "Create swapfile"
	create_swap_file

else
	echo "!! Wrong choice : ${action}"
fi
