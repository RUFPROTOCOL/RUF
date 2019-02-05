#include <math.h>

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_memcpy.h>
#include <rte_hash.h>
#include "config.h"
#include "ds.h"
#include "header.h"
#include "rtp_flow.h"

struct rtp_flow ZERO_FLOW = {0};


void rtp_flow_dump(struct rtp_flow* f) {
    flow_dump(&f->_f);
    printf("%d", f->rd_ctrl_timeout_times);
    printf("\n");
    // printf("flow mbuf address: %u\n", f->buf);
    // printf("flow rts_received: %d\n", f->rts_received);
    // printf("flow finished_at_receiver: %d\n", f->finished_at_receiver);
    // printf("flow token_goal: %d\n", f->token_goal);
    // printf("flow remaining_pkts_at_sender: %d\n", f->remaining_pkts_at_sender);
    // printf("flow largest_token_seq_received: %d\n", f->largest_token_seq_received);
    // printf("flow last_token_data_seq_num_sent: %d\n", f->last_token_data_seq_num_sent);

}
struct rtp_flow* rtp_flow_new(struct rte_mempool* pool) {
	struct rte_mbuf* buf = rte_pktmbuf_alloc(pool);
	if (buf == NULL) {
        printf("%d: allocate flow fails\n", __LINE__);
        rte_exit(EXIT_FAILURE, "fail");
	}
    rte_pktmbuf_append(buf, sizeof(struct rtp_flow));
	struct rtp_flow* flow = rte_pktmbuf_mtod(buf, struct rtp_flow*);
    rte_memcpy(flow, &ZERO_FLOW, sizeof(struct rtp_flow));
	// *flow = ZERO_FLOW;
	flow->buf = buf;
	return flow;
}
void init_rtp_flow(struct rtp_flow* rtp_f, uint32_t id, uint32_t size, uint32_t src_addr, uint32_t dst_addr, double start_time, int receiver_side) {
	init_flow(&(rtp_f->_f), id, size, src_addr, dst_addr, start_time, receiver_side);

    rtp_f->token_goal = (int)(ceil(rtp_f->_f.size_in_pkt * 1.00));
    rtp_f->remaining_pkts_at_sender = rtp_f->_f.size_in_pkt;
    rtp_f->largest_token_seq_received = -1;
    rtp_f->largest_token_data_seq_received = -1;
    rtp_f->latest_token_sent_time = -1;
    rtp_f->latest_data_pkt_sent_time = -1;
	rtp_f->last_token_data_seq_num_sent = -1;
    rtp_f->rd_ctrl_timeout_times = 0;
	rte_timer_init(&rtp_f->rd_ctrl_timeout);
    rte_timer_init(&rtp_f->finish_timeout);
    rtp_f->rd_ctrl_timeout_params = NULL;
    rtp_f->finish_timeout_params = NULL;
}

// void rtp_flow_free(struct rte_mempool* pool){}

int rtp_init_token_size(struct rtp_flow* rtp_f) {
    return rtp_f->_f.size_in_pkt <= params.small_flow_thre? rtp_f->_f.size_in_pkt : 0;
}


bool rtp_flow_compare(struct rtp_flow *a, struct rtp_flow* b) {
    if(a == NULL)
        return true;
    if(b == NULL)
        return false;

    if(rtp_remaining_pkts(a) - rtp_token_gap(a) 
        > rtp_remaining_pkts(b) - rtp_token_gap(b))
        return true;
    else if(a->_f.start_time > b->_f.start_time)
        return true;
    else
        return false;
}

// // receiver side
int rtp_remaining_pkts(struct rtp_flow* f) {
    return 0 > ((int)f->_f.size_in_pkt - (int)f->_f.received_count)? 0 : (f->_f.size_in_pkt - f->_f.received_count);
}
int rtp_token_gap(struct rtp_flow* f) {
    if(f->token_count - f->largest_token_seq_received < 0) {
        rte_exit(EXIT_FAILURE ,"token gap less than 0");
    }
    return f->token_count - f->largest_token_seq_received - 1;
}
// void rtp_relax_token_gap(rtp_flow* f) {

// }
int rtp_get_next_token_seq_num(struct rtp_flow* f) {
    uint32_t count = 0;
    int data_seq = (f->last_token_data_seq_num_sent + 1) % f->_f.size_in_pkt;
    struct rte_bitmap* bmp = f->_f.bmp;
    while(count < f->_f.size_in_pkt)
    {
        if(rte_bitmap_get(bmp, (uint32_t)data_seq) == 0)
        {
            if(data_seq >= 0 && data_seq < f->_f.size_in_pkt) {
                return data_seq;
            } else {
                rte_exit(EXIT_FAILURE, "data seq is in wrong range");
            }
        }
        else
        {
            data_seq++;
            if(data_seq >= f->_f.size_in_pkt)
            {
                data_seq = f->received_until;
            }

        }
        count++;
    }
    rte_exit(EXIT_FAILURE, "get next token should never reaches here");
}
void rtp_get_token_pkt(struct rtp_flow* rtp_f, struct rte_mbuf* p, uint32_t round, int data_seq) {
    add_ether_hdr(p);

    struct ipv4_hdr ipv4_hdr;
    uint16_t size;
    size = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + 
        sizeof(struct rtp_hdr) + sizeof(struct rtp_token_hdr);
    ipv4_hdr.src_addr = rte_cpu_to_be_32(rtp_f->_f.dst_addr);
    ipv4_hdr.dst_addr = rte_cpu_to_be_32(rtp_f->_f.src_addr);
    ipv4_hdr.total_length = rte_cpu_to_be_16(size);

    add_ip_hdr(p, &ipv4_hdr);
    
    struct rtp_hdr rtp_hdr;
    rtp_hdr.type = RTP_TOKEN;
    add_rtp_hdr(p, &rtp_hdr);
    int data_seq_num = data_seq;
    rtp_f->last_token_data_seq_num_sent = data_seq_num;
    struct rtp_token_hdr rtp_token_hdr;
    rtp_token_hdr.priority = rtp_f->_f.priority;
    rtp_token_hdr.flow_id = rtp_f->_f.id;
    rtp_token_hdr.data_seq = data_seq_num;
    rtp_token_hdr.seq_num = rtp_f->token_count;
    rtp_token_hdr.remaining_size = rtp_remaining_pkts(rtp_f);

    if(rtp_f->_f.size_in_pkt > params.small_flow_thre) {
        rtp_token_hdr.round = round;
    } else {
        rtp_token_hdr.round = 0;
    }
    add_rtp_token_hdr(p, &rtp_token_hdr);

    rtp_f->token_count++;
    rtp_f->token_packet_sent_count++;
}

void rtp_get_ack_pkt(struct rte_mbuf* p, struct rtp_flow* flow) {
    add_ether_hdr(p);
    struct ipv4_hdr ipv4_hdr;
    struct rtp_hdr rtp_hdr;
    struct rtp_ack_hdr rtp_ack_hdr;
    uint16_t size;
    size = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + 
        sizeof(struct rtp_hdr) + sizeof(struct rtp_ack_hdr);
    ipv4_hdr.src_addr = rte_cpu_to_be_32(flow->_f.dst_addr);
    ipv4_hdr.dst_addr = rte_cpu_to_be_32(flow->_f.src_addr);
    ipv4_hdr.total_length = rte_cpu_to_be_16(size);
    add_ip_hdr(p, &ipv4_hdr);

    rtp_hdr.type = RTP_ACK;
    add_rtp_hdr(p, &rtp_hdr);
    rtp_ack_hdr.flow_id = flow->_f.id;
    add_rtp_ack_hdr(p, & rtp_ack_hdr);
}
// find the smallest of long flows
struct rtp_flow* get_src_smallest_unfinished_flow(uint32_t src_addr, struct rte_hash* table) {
    uint32_t* flow_id = 0;
    int32_t position = 0;
    uint32_t next = 0;
    struct rtp_flow* flow;
    struct rtp_flow* smallest_flow = NULL;
    while(1) {
        position = rte_hash_iterate(table, (const void**) &flow_id, (void**)&flow, &next);
        if(position == -ENOENT) {
            break;
        }
        if(flow->finished_at_receiver) {
            continue;
        }
        if(flow->rd_ctrl_timeout_params != NULL) {
            continue;
        }
        if(flow->_f.src_addr != src_addr) {
            continue;
        }
        if(flow->_f.size_in_pkt <= params.small_flow_thre) {
            continue;
        }
        if(rtp_flow_compare(smallest_flow, flow)) {
            smallest_flow = flow;
        }
    }
    return smallest_flow;
}

void reset_rd_ctrl_timeout(struct rtp_receiver* receiver, struct rtp_flow* flow, double time) {
    // double time = params.token_resend_timeout * params.BDP * get_transmission_delay(1500) ;
    if(flow->rd_ctrl_timeout_params == NULL) {
        flow->rd_ctrl_timeout_params = rte_zmalloc("rd ctrl timeout param", 
            sizeof(struct rd_ctrl_timeout_params), 0);
        flow->rd_ctrl_timeout_params->receiver = receiver;
        flow->rd_ctrl_timeout_params->flow = flow;
    }
    if(debug_flow(flow->_f.id)) {
        printf("flow %u: flow received count: %u\n", flow->_f.id, flow->_f.received_count);
    }
    flow->rd_ctrl_timeout_times++;
    int ret = rte_timer_reset(&flow->rd_ctrl_timeout, rte_get_timer_hz() * time, SINGLE,
                    rte_lcore_id(), &rd_ctrl_timeout_handler, (void*)flow->rd_ctrl_timeout_params);
    if(ret != 0) {
        printf("%d: cannot reset timer\n", __LINE__);
        rte_exit(EXIT_FAILURE, "fail");
    }
}

void rd_ctrl_timeout_handler(__rte_unused struct rte_timer *timer, void* arg) {
    struct rd_ctrl_timeout_params* timeout_params = (struct rd_ctrl_timeout_params*) arg;
    struct rtp_flow* flow = timeout_params->flow;
    struct rtp_receiver *receiver = timeout_params->receiver;
    flow->rd_ctrl_timeout_params = NULL;
    if(debug_flow(flow->_f.id)){
        printf("redundancy ctl timeout for flow flow%u\n", flow->_f.id);
    }
    rte_free(timeout_params);
    // if flow is short then has to send tokens; otherwise treats the same as new large flow;
    if(flow->_f.size_in_pkt < params.small_flow_thre) {
        uint32_t i = 0;
        for(; i < flow->_f.size_in_pkt; i++) {
            if(rte_bitmap_get(flow->_f.bmp, i) == 0) {
                struct rte_mbuf* p = NULL;
                p = rte_pktmbuf_alloc(pktmbuf_pool);
                uint16_t size = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + 
                    sizeof(struct rtp_hdr) + sizeof(struct rtp_token_hdr);
                if(p == NULL) {
                    printf("size of long flow token q: %u\n",rte_ring_count(receiver->long_flow_token_q));
                    printf("size of short flow token q: %u\n",rte_ring_count(receiver->short_flow_token_q));
                    rte_exit(EXIT_FAILURE, "%s: pktmbuf_pool is full\n", __func__);
                }
                rte_pktmbuf_append(p, size);
                rtp_get_token_pkt(flow, p, -1, i);
                enqueue_ring(receiver->short_flow_token_q, p);
            }
        }
        reset_rd_ctrl_timeout(receiver, flow, get_transmission_delay(1500) * params.BDP);
    } else { 

    }
}

void finish_timeout_handler(__rte_unused struct rte_timer *timer, void* arg) {
    struct finish_timeout_params* timeout_params = (struct finish_timeout_params*) arg;
    struct rtp_flow* flow = lookup_table_entry(timeout_params->hash, timeout_params->flow_id);
    flow->finish_timeout_params = NULL;
    // printf("finish timeout handler for flow %u\n", flow->_f.id);
    delete_table_entry(timeout_params->hash, timeout_params->flow_id);
    rte_free(flow->_f.bmp);
    // rte_bitmap_free(flow->_f.bmp);
    rte_pktmbuf_free(flow->buf);
    rte_free(timeout_params);
}