#ifndef __RSWAP_UTILS_H
#define __RSWAP_UTILS_H

#include "constants.h"

enum rdma_queue_type { QP_STORE, QP_LOAD_SYNC, QP_LOAD_ASYNC, NUM_QP_TYPE };

/* 2-sided RDMA message type */
enum message_type {
  DONE = 1,
  GOT_CHUNKS,
  GOT_SINGLE_CHUNK,
  FREE_SIZE,
  EVICT,

  ACTIVITY,
  STOP,

  REQUEST_CHUNKS,
  REQUEST_SINGLE_CHUNK,
  QUERY,

  AVAILABLE_TO_QUERY
};

/**
 * RDMA command line attached to each RDMA message.
 *
 */
struct message {
  uint64_t buf[MAX_REGION_NUM];
  uint64_t mapped_size[MAX_REGION_NUM];
  uint32_t rkey[MAX_REGION_NUM];
  int mapped_chunk;

  enum message_type type;
};

#endif // __RSWAP_UTILS_H
