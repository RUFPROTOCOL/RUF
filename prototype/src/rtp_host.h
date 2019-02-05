#ifndef RTP_HOST_H
#define RTP_HOST_H

#include <inttypes.h>
#include <stdlib.h>

#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_hash.h>
#include <rte_mempool.h>
#include <rte_malloc.h>
#include <rte_ring.h>
#include <rte_timer.h>

#include "debug.h"
#include "header.h"
#include "rtp_pacer.h"
#include "rtp_flow.h"
#include "pq.h"

struct rtp_flow;

struct gosrc_info {
	uint32_t src_addr;
	struct rtp_flow* current_flow;
    int max_tokens;
    int remain_tokens;
    int round;
    bool has_gosrc;
    bool send_nrts;   
};

struct idle_timeout_params {
	struct rtp_pacer* pacer;
	struct rtp_receiver* receiver;
};

struct send_token_evt_params {
	struct rtp_pacer* pacer;
	struct rtp_receiver* receiver;
};

struct send_listsrc_params {
	struct rtp_pacer *pacer;
	struct rtp_receiver *receiver;
	int nrts_src_addr; 
};


struct src_dst_pair {
	uint32_t src;
	uint32_t dst;
};

struct rtp_sender{
	
	struct rte_mempool *tx_flow_pool;
	struct rte_ring *short_flow_token_q;
	struct rte_ring *long_flow_token_q;
	// struct rte_ring * control_message_q;
	struct rte_hash * tx_flow_table;
	uint32_t finished_flow;
	uint32_t sent_bytes;

};

struct rtp_receiver{
	struct rte_mempool *rx_flow_pool;
	struct rte_hash *rx_flow_table;
	// min large flow
	struct rte_hash *src_minflow_table;
	struct rte_ring *long_flow_token_q;
	struct rte_ring *short_flow_token_q;
	struct rte_ring *temp_pkt_buffer;
	// struct rte_ring * control_message_q;
	struct rte_timer idle_timeout;
	struct idle_timeout_params* idle_timeout_params;
	struct gosrc_info gosrc_info;
	struct rte_timer send_token_evt_timer;
	struct send_token_evt_params* send_token_evt_params;

    struct rte_timer send_listsrc_timer;
    // struct send_listsrc_params* send_listsrc_params;
	uint32_t received_bytes;
	uint64_t start_cycle;
	uint64_t end_cycle;
	uint32_t num_token_sent;
	uint32_t idle_timeout_times;
};

struct rtp_controller{
	// struct rte_mempool* node_pool;
	// struct rte_mempool* element_pool;
	struct rte_hash* sender_state;
	struct rte_hash* receiver_state;
	struct rte_timer handle_rq_timer;
	Node* head;
};

void rtp_new_flow_comes(struct rtp_sender * sender, struct rtp_pacer* pacer, 
	uint32_t flow_id, uint32_t dst_addr, uint32_t flow_size);
// set gosrc
void init_gosrc(struct gosrc_info *gosrc);
void reset_gosrc(struct gosrc_info *gosrc);
// receiver logic
void idle_timeout_handler(__rte_unused struct rte_timer *timer, void* arg);
void send_token_evt_handler(__rte_unused struct rte_timer *timer, void* arg);

void reset_idle_timeout(struct rtp_receiver *receiver, struct rtp_pacer *pacer);
void reset_send_tokens_evt(struct rtp_receiver *receiver, struct rtp_pacer* pacer, int sent_token);

void rtp_rx_packets(struct rtp_receiver* receiver, struct rtp_sender* sender, struct rtp_pacer* pacer,
 struct rte_mbuf* p);
void rtp_receive_rts(struct rtp_receiver* receiver, struct rtp_pacer *pacer, 
	struct ipv4_hdr* ipv4_hdr, struct rtp_rts_hdr* rtp_rts_hdr);
void rtp_receive_gosrc(struct rtp_receiver *receiver, struct rtp_pacer *pacer,
 struct rtp_gosrc_hdr *rtp_gosrc_hdr);
void rtp_receive_data(struct rtp_receiver *receiver, struct rtp_pacer* pacer,
 struct rtp_data_hdr * rtp_data_hdr, struct rte_mbuf *p);

void host_dump(struct rtp_sender* sender, struct rtp_receiver *receiver, struct rtp_pacer* pacer);

void send_listsrc(__rte_unused struct rte_timer *timer, void* arg);
void invoke_send_listsrc(struct rtp_receiver* receiver, struct rtp_pacer *pacer, int nrts_src_addr);

void rtp_flow_finish_at_receiver(struct rtp_receiver *receiver, struct rtp_flow * f);
// sender logic
void rtp_receive_token(struct rtp_sender *sender, struct rtp_token_hdr *rtp_token_hdr, struct rte_mbuf* p);
void rtp_receive_ack(struct rtp_sender *sender, struct rtp_ack_hdr * rtp_ack_hdr);
// void enqueue();
// void dequeue();

void rtp_new_flow(
 struct rtp_pacer* pacer, uint32_t flow_id, uint32_t dst_addr, uint32_t flow_size);

// controller logic 

void rtp_receive_listsrc(struct rtp_controller *controller, struct rte_mbuf *p);
void handle_requests(__rte_unused struct rte_timer *timer, void* arg);

void init_sender(struct rtp_sender *rtp_sender, uint32_t socket_id);
void init_receiver(struct rtp_receiver *rtp_receiver, uint32_t socket_id);
void init_controller(struct rtp_controller* controller, uint32_t socket_id);



// helper function
void send_rts(struct rtp_sender* sender, struct rtp_pacer* pacer, struct rtp_flow* flow);
void iterate_temp_pkt_buf(struct rtp_receiver* receiver, struct rtp_pacer* pacer, uint32_t flow_id);
void get_gosrc_pkt(struct rte_mbuf* p, uint32_t src_addr,
 uint32_t dst_addr, uint32_t token_num);
#endif