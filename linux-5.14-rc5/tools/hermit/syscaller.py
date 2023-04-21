import ctypes
import sys
import os
from collections import defaultdict

CGROUP_PATH = '/sys/fs/cgroup/memory'

SYS_RESET_SWAP_STAT = 451
SYS_GET_SWAP_STATS = 452

DISABLE_SYSCALL = False

# syscalls
def reset_swap_stats():
    if DISABLE_SYSCALL:
        return
    libc = ctypes.CDLL(None)
    syscall = libc.syscall
    syscall.restype = ctypes.c_int

    return syscall(SYS_RESET_SWAP_STAT)


def get_swap_stats():
    if DISABLE_SYSCALL:
        return 0,0,0
    libc = ctypes.CDLL(None)
    syscall = libc.syscall
    syscall.restype = ctypes.c_int
    syscall.argtypes = ctypes.c_long, ctypes.POINTER(
        ctypes.c_int), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)

    c_ondemand_swap_num = ctypes.c_int()
    c_prefetch_swap_num = ctypes.c_int()
    c_hiton_swap_cache_num = ctypes.c_int()
    syscall(SYS_GET_SWAP_STATS, ctypes.byref(c_ondemand_swap_num), ctypes.byref(
        c_prefetch_swap_num), ctypes.byref(c_hiton_swap_cache_num))

    ondemand_swap_num = c_ondemand_swap_num.value
    prefetch_swap_num = c_prefetch_swap_num.value
    hiton_swap_cache_num = c_hiton_swap_cache_num.value
    print(ondemand_swap_num, prefetch_swap_num, hiton_swap_cache_num)
    return ondemand_swap_num, prefetch_swap_num, hiton_swap_cache_num


# util functions
def calc_percentage():
    ondemand, prefetch, hiton_swap = get_swap_stats()
    if (ondemand + prefetch) == 0 or prefetch == 0:
        prefetch_percentage = 0
        prefetch_accuracy = 0
    else:
        prefetch_percentage = prefetch / (ondemand + prefetch)
        prefetch_accuracy = hiton_swap / prefetch

    print("#(On-demand Swapin):", ondemand)
    print("#(Prefetch Swapin) :", prefetch)
    print("#(Hiton Swap Cache):", hiton_swap)
    print("Prefetch Precentage: {:.2f}".format(prefetch_percentage))
    print("Prefetch Accuracy  : {:.2f}".format(prefetch_accuracy))

    return prefetch_percentage, prefetch_accuracy


def get_containers():
    return [name for name in os.listdir(CGROUP_PATH) if os.path.isdir(os.path.join(CGROUP_PATH, name))]


def get_container_stats(containers=None, keys=None, tty=True):
    if not containers:
        containers = get_containers()
    if not keys:
        keys = ['ondemand_swapin ',
                'prefetch_swapin ',
                'hiton_swap_cache']

    global_stats = defaultdict(int)
    container_stats = {}
    for container in containers:
        container_stats[container] = {}
        with open(os.path.join(CGROUP_PATH, container, 'memory.stat')) as f:
            lines = f.readlines()
            for line in lines:
                for key in keys:
                    if line[:len(key)] == key:
                        container_stats[container][key] = int(line.split()[-1])
                        global_stats[key] += int(line.split()[-1])

    if tty:
        for c, stats in container_stats.items():
            print(c, ':')
            for k, v in stats.items():
                print('\t' + k, v)
        for k, v in global_stats.items():
            print('total_' + k, v)

    return container_stats


def reset_container_stats(container):
    # return
    with open(os.path.join(CGROUP_PATH, container, 'memory.stat'), 'w') as f:
        f.write('0')


def main():
    if len(sys.argv) == 1:
        calc_percentage()
    elif len(sys.argv) > 1:
        if sys.argv[1] == 'reset':
            reset_swap_stats()
        elif sys.argv[1] == 'stats':
            calc_percentage()
            get_container_stats()
        elif sys.argv[1] == 'sthd_cores':
            set_sthd_cores()
        else:
             print("Unrecognized operation:", sys.argv[1])


if __name__ == '__main__':
    main()
