#ifndef RTP_FLOW_H
#define RTP_FLOW_H

#include <stdbool.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_timer.h>
#include <rte_hash.h>

#include "debug.h"
#include "rtp_host.h"
#include "flow.h"

typedef struct //for extendability
{
    double timeout;
    int seq_num;
    int data_seq_num;
    int round;
} rtp_token;

struct rtp_flow{
	struct flow _f;
    struct rte_mbuf* buf;
    bool rts_received;
    bool finished_at_receiver;
    int last_token_data_seq_num_sent;
    int received_until;
    int token_count;
    int token_packet_sent_count;
    int token_waste_count;
    int token_goal;
    int remaining_pkts_at_sender;
    int largest_token_seq_received;
    int largest_token_data_seq_received;
    double latest_token_sent_time;
    double latest_data_pkt_sent_time;
    
    struct rte_timer rd_ctrl_timeout;
    int rd_ctrl_timeout_times;
    struct rd_ctrl_timeout_params* rd_ctrl_timeout_params;
    // need to wait some time before cleaning states
    struct rte_timer finish_timeout;
    struct finish_timeout_params* finish_timeout_params;
};

struct rd_ctrl_timeout_params {
    struct rtp_receiver* receiver;
    struct rtp_flow* flow;
};

struct finish_timeout_params {
    struct rte_hash* hash;
    uint32_t flow_id;
};

void rtp_flow_dump(struct rtp_flow* f);
struct rtp_flow* rtp_flow_new(struct rte_mempool* pool);
void init_rtp_flow(struct rtp_flow* rtp_f, uint32_t id, uint32_t size, uint32_t src_addr, uint32_t dst_addr, double start_time, int receiver_side);

// void rtp_flow_free(struct rte_mempool* pool);

bool rtp_flow_compare(struct rtp_flow *a, struct rtp_flow* b);
// rtp_flow* rtp_flow_free(rtp_flow* rtp_f);
int rtp_init_token_size(struct rtp_flow* rtp_f);

void rd_ctrl_timeout_handler(__rte_unused struct rte_timer *timer, void* arg);
void finish_timeout_handler(__rte_unused struct rte_timer *timer, void* arg);

void reset_rd_ctrl_timeout(struct rtp_receiver* receiver, struct rtp_flow* flow, double time);

// double rtp_calc_oct_time_ratio();
// // send control signals
// void rtp_sending_rts(rtp_flow* rtp_f);
// void rtp_sending_nrts(rtp_flow* rtp_f, int round);
// void rtp_sending_nrts_to_arbiter(rtp_flow* rtp_f, uint32_t src_id, uint32_t dst_id);
// void rtp_sending_gosrc(rtp_flow* rtp_f, uint32_t src_id);
// void rtp_sending_ack(rtp_flow* rtp_f, int round);
// // sender side
// void rtp_clear_token(rtp_flow* rtp_f);
// rtp_token* rtp_use_token(rtp_flow* rtp_f);
// bool rtp_has_token(rtp_flow* rtp_f);
// struct rte_mbuf* rtp_send(rtp_flow* rtp_f, uint32_t seq, int token_seq, int data_seq, int priority, int ranking_round);
// void rtp_assign_init_token(rtp_flow* rtp_f);
// // receiver side
int rtp_remaining_pkts(struct rtp_flow* rtp_f);
int rtp_token_gap(struct rtp_flow* rtp_f);
// void rtp_relax_token_gap(rtp_flow* rtp_f);
int rtp_get_next_token_seq_num(struct rtp_flow* rtp_f);
void rtp_get_token_pkt(struct rtp_flow* rtp_f, struct rte_mbuf* p, uint32_t round, int data_seq);
void rtp_get_ack_pkt(struct rte_mbuf* p, struct rtp_flow* flow);
// void rtp_receive_short_flow(rtp_flow* rtp_f);
struct rtp_flow* get_src_smallest_unfinished_flow(uint32_t src_addr, struct rte_hash* table);



#endif