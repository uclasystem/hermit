#ifndef __RSWAP_SERVER_HPP
#define __RSWAP_SERVER_HPP

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "constants.h"
#include "utils.h"

// #define DEBUG_RDMA_SERVER /* verbose debug messages */

#define CQ_QP_IDLE 0
#define CQ_QP_BUSY 1
#define CQ_QP_DOWN 2

/**
 * This memory server support multiple QP for each CPU.
 */
struct rswap_rdma_queue {

  struct rdma_cm_id *cm_id;

  struct ibv_cq *cq;
  struct ibv_qp *qp;

  uint8_t connected;

  int q_index;
  enum rdma_queue_type type;
  struct context *rdma_session;
};

struct rswap_rdma_dev {
  struct ibv_context *ctx;
  struct ibv_pd *pd;
};

enum server_states { S_WAIT, S_BIND, S_DONE };

enum send_states { SS_INIT, SS_MR_SENT, SS_STOP_SENT, SS_DONE_SENT };

enum recv_states { RS_INIT, RS_STOPPED_RECV, RS_DONE_RECV };

/**
 * RDMA conection context.
 */
struct context {
  struct rswap_rdma_dev *rdma_dev;
  struct rswap_rdma_queue *rdma_queues;

  struct ibv_comp_channel *comp_channel;

  struct message *recv_msg;
  struct ibv_mr *recv_mr;

  struct message *send_msg;
  struct ibv_mr *send_mr;

  struct rdma_mem_pool *mem_pool;
  int connected;

  server_states server_state;
};

/**
 * Describe the memory pool
 */
struct rdma_mem_pool {
  char *heap_start;
  int region_num;

  struct ibv_mr *mr_buffer[MAX_REGION_NUM];
  char *region_list[MAX_REGION_NUM];
  size_t region_mapped_size[MAX_REGION_NUM];
  int cache_status[MAX_REGION_NUM];
};

/**
 * Define tools
 */
void die(const char *reason);

#define ntohll(x)                                                              \
  (((uint64_t)(ntohl((int)((x << 32) >> 32))) << 32) |                         \
   (unsigned int)ntohl(((int)(x >> 32))))

#define TEST_NZ(x)                                                             \
  do {                                                                         \
    if ((x))                                                                   \
      die("error: " #x " failed (returned non-zero).");                        \
  } while (0)
#define TEST_Z(x)                                                              \
  do {                                                                         \
    if (!(x))                                                                  \
      die("error: " #x " failed (returned zero/null).");                       \
  } while (0)

/**
 * Declare functions
 */
int on_cm_event(struct rdma_cm_event *event);
int on_connect_request(struct rdma_cm_id *id);
int rdma_connected(struct rswap_rdma_queue *rdma_queue);
int on_disconnect(struct rswap_rdma_queue *rdma_queue);

void build_connection(struct rswap_rdma_queue *rdma_queue);
void build_params(struct rdma_conn_param *params);
void get_device_info(struct rswap_rdma_queue *rdma_queue);
void build_qp_attr(struct rswap_rdma_queue *rdma_queue,
                   struct ibv_qp_init_attr *qp_attr);
void handle_cqe(struct ibv_wc *wc);

void inform_memory_pool_available(struct rswap_rdma_queue *rdma_queue);
void send_free_mem_size(struct rswap_rdma_queue *rdma_queue);
void send_regions(struct rswap_rdma_queue *rdma_queue);
void send_message(struct rswap_rdma_queue *rdma_queue);

void destroy_connection(struct context *rdma_session);
void *poll_cq(void *ctx);
void post_receives(struct rswap_rdma_queue *rdma_queue);

void init_memory_pool(struct context *rdma_ctx);
void register_rdma_comm_buffer(struct context *rdma_ctx);

enum rdma_queue_type get_qp_type(int idx);
struct rswap_rdma_queue *get_rdma_queue(unsigned int cpu,
                                        enum rdma_queue_type type);

extern struct context *global_rdma_ctx;
extern int rdma_queue_count;

extern int errno;

static inline void print_debug(FILE *file, const char *format, ...)
{
#ifdef DEBUG_RDMA_SERVER
	va_list args;
	va_start(args, format);
	fprintf(file, format, args);
	va_end(args);
#endif
}

#endif // __RSWAP_SERVER_HPP
