#ifndef __RSWAP_CONSTANTS_H
#define __RSWAP_CONSTANTS_H

#ifndef ONE_MB
#define ONE_MB ((size_t)1024 * 1024)
#endif
#ifndef ONE_GB
#define ONE_GB ((size_t)1024 * 1024 * 1024)
#endif

// RDMA manage granularity, not heap region.
#ifndef REGION_SIZE_GB
#define REGION_SIZE_GB ((size_t)8)
#endif

#ifdef MAX_REGION_NUM
#undef MAX_REGION_NUM
#endif
#define MAX_REGION_NUM 16 // up to 128GB remote memory

// number of segments, get from ibv_query_device.
// Check the hardware information and chang the number here.
// For zion-# servers with ConnectX-3 IB, the hardware supports 32, but it can
// only support up to 30. For the cloudlab CX5 machines, though the hardware
// reports 30, it can only support up to 19.
#define MAX_REQUEST_SGL 1

// number of write queues
#define NR_WRITE_QUEUE 48

#endif // __RSWAP_CONSTANTS_H
