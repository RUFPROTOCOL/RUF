#include <rte_bitmap.h>
#include <rte_hash.h>
#include <rte_jhash.h>
#include "config.h"
#include "ds.h"
#include "rtp_host.h"
#include "rtp_pacer.h"

extern struct rte_mempool* pktmbuf_pool;
// set go src
 void init_gosrc(struct gosrc_info *gosrc) {
    gosrc->max_tokens = -1;
    gosrc->remain_tokens = -1;
    gosrc->round = 0;
    gosrc->src_addr = 0;
    gosrc->send_nrts = false;
    gosrc->has_gosrc = false;
    gosrc->current_flow = NULL;
};

void reset_gosrc(struct gosrc_info *gosrc) {
    gosrc->max_tokens = -1;
    gosrc->remain_tokens = -1;
    gosrc->src_addr = 0;
    gosrc->current_flow = NULL;
    gosrc->has_gosrc = false;
}

void init_sender(struct rtp_sender *sender, uint32_t socket_id) {
	sender->tx_flow_pool = create_mempool("tx_flow_pool", sizeof(struct rtp_flow) + RTE_PKTMBUF_HEADROOM, 131072, socket_id);
	sender->short_flow_token_q = create_ring("tx_short_flow_token_q", sizeof(struct rtp_token_hdr), 256, RING_F_SC_DEQ, socket_id);
	sender->long_flow_token_q = create_ring("tx_long_flow_token_q", sizeof(struct rtp_token_hdr), 256, RING_F_SC_DEQ | RING_F_SP_ENQ, socket_id);
	// allow multiple read/write
	sender->tx_flow_table = create_hash_table("tx_flow_table", sizeof(uint32_t), 131072, RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY, socket_id);
	sender->finished_flow = 0;
	sender->sent_bytes = 0;
	// sender->control_message_q = create_ring("tx_control_message_queue", 1500, 256, RING_F_SC_DEQ | RING_F_SP_ENQ);
}
void init_receiver(struct rtp_receiver *receiver, uint32_t socket_id) {
	rte_timer_init(&receiver->idle_timeout);
	rte_timer_init(&receiver->send_token_evt_timer);
	rte_timer_init(&receiver->send_listsrc_timer);
	receiver->idle_timeout_params = NULL;
	receiver->send_token_evt_params = NULL;
	// receiver->send_listsrc_params = NULL;
///---------	
	init_gosrc(&receiver->gosrc_info);
	receiver->num_token_sent = 0;
	receiver->idle_timeout_times = 0;
	receiver->received_bytes = 0;
	receiver->src_minflow_table = create_hash_table("src_minflow_table", sizeof(uint32_t), 16, 0, socket_id);
	receiver->rx_flow_table = create_hash_table("rx_flow_table", sizeof(uint32_t), 65536, 0, socket_id);
	receiver->short_flow_token_q = create_ring("rx_short_flow_token_q", sizeof(struct rtp_token_hdr), 256, RING_F_SC_DEQ | RING_F_SP_ENQ, socket_id);
	receiver->long_flow_token_q = create_ring("rx_long_flow_token_q", 1500, 256, RING_F_SC_DEQ | RING_F_SP_ENQ, socket_id);
	receiver->temp_pkt_buffer = create_ring("temp_pkt_buffer", 1500, 256, RING_F_SC_DEQ | RING_F_SP_ENQ, socket_id);	
	receiver->rx_flow_pool = create_mempool("rx_flow_pool", sizeof(struct rtp_flow) + RTE_PKTMBUF_HEADROOM, 65536, socket_id);
	// printf("rtp_flow_size:%u\n", sizeof(rtp_flow) + RTE_PKTMBUF_HEADROOM);
}

void host_dump(struct rtp_sender* sender, struct rtp_receiver *receiver, struct rtp_pacer *pacer) {
	printf("size of long flow token q: %u\n",rte_ring_count(receiver->long_flow_token_q));
	printf("size of short flow token q: %u\n",rte_ring_count(receiver->short_flow_token_q));
	printf("size of temp_pkt_buffer: %u\n",rte_ring_count(receiver->temp_pkt_buffer));
	printf("size of control q: %u\n", rte_ring_count(pacer->ctrl_q)); 
}

void rtp_new_flow_comes(struct rtp_sender * sender, struct rtp_pacer* pacer, uint32_t flow_id, uint32_t dst_addr, uint32_t flow_size) {
	struct rtp_flow* exist_flow = lookup_table_entry(sender->tx_flow_table, flow_id);
	if(exist_flow != NULL) {
		rte_exit(EXIT_FAILURE, "Twice new flows comes");
	}
	struct rtp_flow* new_flow = rtp_flow_new(sender->tx_flow_pool);
	if(new_flow == NULL) {
		printf("flow is NULL");
		rte_exit(EXIT_FAILURE, "flow is null");
	}
	init_rtp_flow(new_flow, flow_id, flow_size, params.ip, dst_addr, rte_get_tsc_cycles(), 0);

	if(debug_flow(flow_id)){
		rtp_flow_dump(new_flow);
	}
	insert_table_entry(sender->tx_flow_table, new_flow->_f.id, new_flow);
	// send rts
	if(debug_flow(flow_id)) {
		printf("%"PRIu64" new flow arrives:%u; size: %u\n", rte_get_tsc_cycles(), flow_id, flow_size);
	}
	send_rts(sender, pacer, new_flow);
	// push all tokens
	if(new_flow->_f.size_in_pkt <= params.small_flow_thre) {
		uint32_t i = 0;	
		for(; i < new_flow->_f.size_in_pkt; i++) {
		 	struct rte_mbuf* p = NULL;
			p = rte_pktmbuf_alloc(pktmbuf_pool);
			uint16_t size = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + 
				sizeof(struct rtp_hdr) + sizeof(struct rtp_token_hdr);
			if(p == NULL) {
				printf("%s: Pktbuf pool full\n", __func__);
				rte_exit(EXIT_FAILURE ,"");
			}
			rte_pktmbuf_append(p, size);
			rtp_get_token_pkt(new_flow, p, -1, i);
			enqueue_ring(sender->short_flow_token_q , p);
		}
	}
	// printf("finish\n");
}
// receiver logic 
void rtp_rx_packets(struct rtp_receiver* receiver, struct rtp_sender* sender, struct rtp_pacer* pacer,
struct rte_mbuf* p) {
	struct rtp_hdr *rtp_hdr;
	struct ipv4_hdr *ipv4_hdr;
	uint32_t offset = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr);
	// get ip header
	ipv4_hdr = rte_pktmbuf_mtod_offset(p, struct ipv4_hdr *, sizeof(struct ether_hdr));
	// get rtp header
	rtp_hdr = rte_pktmbuf_mtod_offset(p, struct rtp_hdr *, offset);
	offset += sizeof(struct rtp_hdr);
	// parse packet
	if(rtp_hdr->type == RTP_RTS) {
		struct rtp_rts_hdr *rtp_rts_hdr = rte_pktmbuf_mtod_offset(p, struct rtp_rts_hdr*, offset);
		if(debug_flow(rtp_rts_hdr->flow_id)) {
			printf("receive rts header; flow id:%d\n", rtp_rts_hdr->flow_id);
		}
		rtp_receive_rts(receiver, pacer, ipv4_hdr, rtp_rts_hdr);
	} else if (rtp_hdr->type == RTP_GOSRC) {
		struct rtp_gosrc_hdr *rtp_gosrc_hdr = rte_pktmbuf_mtod_offset(p, struct rtp_gosrc_hdr*, offset);
		rtp_receive_gosrc(receiver, pacer, rtp_gosrc_hdr);

	} else if (rtp_hdr->type == RTP_TOKEN) {
		struct rtp_token_hdr *rtp_token_hdr = rte_pktmbuf_mtod_offset(p, struct rtp_token_hdr*, offset);
		rtp_receive_token(sender, rtp_token_hdr, p);
		// free p is the repsonbility of the sender
		return;
	} else if (rtp_hdr->type == RTP_ACK) {
		struct rtp_ack_hdr *rtp_ack_hdr = rte_pktmbuf_mtod_offset(p, struct rtp_ack_hdr*, offset);
		rtp_receive_ack(sender, rtp_ack_hdr);
	}  else if (rtp_hdr->type == RTP_LISTSRCS) {
        // printf("%d: should not recieve listsrc\n", __LINE__);
        // rte_exit(EXIT_FAILURE, "receive listsrc");
	} else if(rtp_hdr->type == DATA) {
		struct rtp_data_hdr *rtp_data_hdr = rte_pktmbuf_mtod_offset(p, struct rtp_data_hdr*, offset);
		// if(debug_flow(rtp_data_hdr->flow_id)) {
		// 	printf("receive data %u for flow id:%d\n",rtp_data_hdr->data_seq, rtp_data_hdr->flow_id);
		// }
		receiver->received_bytes += 1500;
		rtp_receive_data(receiver, pacer, rtp_data_hdr, p);
		return;
	}
	else {
        printf("%d: receive unknown packets\n", __LINE__);
        rte_exit(EXIT_FAILURE, "receive unknown types");
	}
	rte_pktmbuf_free(p);
}

void rtp_receive_rts(struct rtp_receiver* receiver, struct rtp_pacer *pacer, 
	struct ipv4_hdr* ipv4_hdr, struct rtp_rts_hdr* rtp_rts_hdr) {
	struct rtp_flow* exist_flow = lookup_table_entry(receiver->rx_flow_table, rtp_rts_hdr->flow_id);
	if(exist_flow != NULL && exist_flow->_f.size_in_pkt > params.small_flow_thre) {
		rtp_flow_dump(exist_flow);
		printf("long flow send twice RTS");
		rte_exit(EXIT_FAILURE, "Twice RTS for long flow");
	}
	if(exist_flow != NULL) {
		return;
	}
	uint32_t src_addr = rte_be_to_cpu_32(ipv4_hdr->src_addr);
	uint32_t dst_addr = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
	struct rtp_flow* new_flow = rtp_flow_new(receiver->rx_flow_pool);
	new_flow->rts_received = true;
	init_rtp_flow(new_flow, rtp_rts_hdr->flow_id, rtp_rts_hdr->flow_size, src_addr, dst_addr, rtp_rts_hdr->start_time, 1);
	// rtp_flow_dump(new_flow);
	insert_table_entry(receiver->rx_flow_table, new_flow->_f.id, new_flow);
	if(new_flow->_f.size_in_pkt <= params.small_flow_thre) {
		int init_token = rtp_init_token_size(new_flow);
		new_flow->token_count = init_token;
    	new_flow->last_token_data_seq_num_sent = init_token - 1;
    	// set rd ctrl timeout
    	reset_rd_ctrl_timeout(receiver, new_flow, (init_token + params.BDP) * get_transmission_delay(1500));
		// printf("ctrl timeout setup: %f\n", (init_token + params.BDP) * get_transmission_delay(1500));
		if(rte_ring_count(receiver->temp_pkt_buffer) != 0) {
			iterate_temp_pkt_buf(receiver, pacer, rtp_rts_hdr->flow_id);
		}
		// add hold on?

		// token scheduling event?
	} else {
		if(!receiver->gosrc_info.has_gosrc) {
			// int ret = rte_timer_stop(&receiver->idle_timeout);
			// if(ret != 0) {
		 //        printf("%d: cannot stop timer\n", __LINE__);
		 //        rte_exit(EXIT_FAILURE, "fail");
			// }
			// send listsrc
			invoke_send_listsrc(receiver, pacer, -1);
			// reset idle_timeout
			reset_idle_timeout(receiver, pacer);	
		} else if(receiver->gosrc_info.has_gosrc 
			&& receiver->gosrc_info.src_addr == new_flow->_f.src_addr) {
			// if(rtp_flow_compare(receiver->gosrc_info.send_flow, new_flow)) {
			// 	receiver->gosrc_info.send_flow = new_flow;
			// } 
			// invoke token send evt;
		}
	}
}

void rtp_receive_gosrc(struct rtp_receiver *receiver, struct rtp_pacer *pacer,
struct rtp_gosrc_hdr *rtp_gosrc_hdr) {
	struct rtp_flow* f = get_src_smallest_unfinished_flow(rtp_gosrc_hdr->target_src_addr, receiver->rx_flow_table);
	if (f == NULL) {
		// 
		invoke_send_listsrc(receiver, pacer, rtp_gosrc_hdr->target_src_addr);
		reset_idle_timeout(receiver, pacer);
		reset_gosrc(&receiver->gosrc_info);
		rte_timer_stop(&receiver->send_token_evt_timer);
		rte_free(receiver->send_token_evt_params);
		receiver->send_token_evt_params = NULL;
	} else {
	    receiver->gosrc_info.max_tokens = rtp_gosrc_hdr->max_tokens;
	    receiver->gosrc_info.remain_tokens = rtp_gosrc_hdr->max_tokens;
	    receiver->gosrc_info.src_addr = rtp_gosrc_hdr->target_src_addr;
	    receiver->gosrc_info.round += 1;
	    receiver->gosrc_info.send_nrts = false;
	    receiver->gosrc_info.has_gosrc = true;
	    receiver->gosrc_info.current_flow = f;	
	    int ret = rte_timer_stop(&receiver->idle_timeout);
		if(ret != 0) {
	        printf("%d: cannot stop timer\n", __LINE__);
	        rte_exit(EXIT_FAILURE, "fail");
		}
		rte_free(receiver->idle_timeout_params);
		receiver->idle_timeout_params = NULL;
		// send token event
		reset_send_tokens_evt(receiver, pacer, 0);
	}
}

void rtp_receive_data(struct rtp_receiver *receiver, struct rtp_pacer* pacer,
 struct rtp_data_hdr * rtp_data_hdr, struct rte_mbuf* p) {
	uint32_t flow_id = rtp_data_hdr->flow_id;
	struct rtp_flow* f = lookup_table_entry(receiver->rx_flow_table, flow_id);
	if(f == NULL && rtp_data_hdr->priority == 1) {
		if(rte_ring_free_count(receiver->temp_pkt_buffer) == 0) {
			struct rte_mbuf *temp = 
			(struct rte_mbuf*) dequeue_ring(receiver->temp_pkt_buffer);
			rte_pktmbuf_free(temp);
		}

		enqueue_ring(receiver->temp_pkt_buffer, p);
        // printf("%s: the receiver doesn't receive rts;\n", __func__);
        // printf("flow id:%u, data seq:%u \n ",rtp_data_hdr->flow_id, rtp_data_hdr->data_seq);
        return;
        // rte_exit(EXIT_FAILURE, "fail");
	}
	if(f == NULL) {
		// large flow should not hold, since the flow is finished and removed from the 
		// data structure;
		rte_pktmbuf_free(p);
		return;
	}
	if(f->_f.id != flow_id) {
        printf("%d: flow id mismatch;\n", __LINE__);
        rte_exit(EXIT_FAILURE, "fail");
	}
	if(f->finished_at_receiver) {
		rte_pktmbuf_free(p); 
		return;
	}
	struct rte_bitmap* bmp = f->_f.bmp;
    if(rte_bitmap_get(bmp, rtp_data_hdr->data_seq) == 0) {
    	rte_bitmap_set(bmp, rtp_data_hdr->data_seq);
        f->_f.received_count++;
        while(f->received_until < (int)f->_f.size_in_pkt && rte_bitmap_get(bmp, f->received_until) != 0) {
            f->received_until++;
        }
        // if(num_outstanding_packets >= ((p->size - hdr_size) / (mss)))
        //     num_outstanding_packets -= ((p->size - hdr_size) / (mss));
        // else
        //     num_outstanding_packets = 0;
        if(f->largest_token_data_seq_received < (int)rtp_data_hdr->data_seq) {
            f->largest_token_data_seq_received =  (int)rtp_data_hdr->data_seq;
        }
    }
    // hard code part
    f->_f.received_bytes += 1460;

    if((int)rtp_data_hdr->seq_num > f->largest_token_seq_received)
        f->largest_token_seq_received = (int)rtp_data_hdr->seq_num;
    if (f->_f.received_count >= f->_f.size_in_pkt) {
    	struct rte_mbuf* p = NULL;
		p = rte_pktmbuf_alloc(pktmbuf_pool);

		uint16_t size = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + 
			sizeof(struct rtp_hdr) + sizeof(struct rtp_ack_hdr);
		char* data = rte_pktmbuf_append(p, size);
		if(data == NULL) {
			printf("size of long flow token q: %u\n",rte_ring_count(receiver->long_flow_token_q));
			printf("size of short flow token q: %u\n",rte_ring_count(receiver->short_flow_token_q));
			printf("size of temp_pkt_buffer: %u\n",rte_ring_count(receiver->temp_pkt_buffer));
			printf("size of control q: %u\n", rte_ring_count(pacer->ctrl_q));
			rte_exit(EXIT_FAILURE, "%s: pkt buffer is FULL\n", __func__);
		}
		rtp_get_ack_pkt(p, f);
		enqueue_ring(pacer->ctrl_q, p);
        // sending_ack(p->ranking_round);
        f->finished_at_receiver = true;
        rtp_flow_finish_at_receiver(receiver, f);
        // clean up memory and timer;
        if(f->rd_ctrl_timeout_params != NULL){
			rte_timer_stop(&f->rd_ctrl_timeout);
			rte_free(f->rd_ctrl_timeout_params);
			f->rd_ctrl_timeout_params = NULL;
        }
		f->finish_timeout_params = rte_zmalloc("finish timeout param", 
			sizeof(struct finish_timeout_params), 0);
		if(f->finish_timeout_params == NULL) {
	        printf("%d: no memory for timeout param \n", __LINE__);
	        rte_exit(EXIT_FAILURE, "fail");
		}
		f->finish_timeout_params->hash = receiver->rx_flow_table;
		f->finish_timeout_params->flow_id = flow_id;
		int ret = rte_timer_reset(&f->finish_timeout, rte_get_timer_hz() * 2 * get_rtt(params.propagation_delay, 3, 1500), SINGLE,
	                    rte_lcore_id(), &finish_timeout_handler, (void *)f->finish_timeout_params);
		if(ret != 0) {
	        printf("%d: cannot set up finish timer\n", __LINE__);
	        rte_exit(EXIT_FAILURE, "fail");
		}
    }
    rte_pktmbuf_free(p); 
}


void rtp_flow_finish_at_receiver(struct rtp_receiver *receiver, struct rtp_flow * f) {
	if(debug_flow(f->_f.id)) {
		printf("flow finish at receiver:%u\n", f->_f.id);
	}
	if(f->_f.size_in_pkt <= params.small_flow_thre)
		return;
	if(f->_f.src_addr == receiver->gosrc_info.src_addr) {
		// if(f == receiver->gosrc_info.send_flow && !receiver->gosrc_info.send_nrts) {
	 //        printf("%d: Should send nrts when flow finishes\n", __LINE__);
	 //        rte_exit(EXIT_FAILURE, "fail");
		// }
	}
}
void invoke_send_listsrc(struct rtp_receiver* receiver, struct rtp_pacer *pacer, int nrts_src_addr) {
	// if(receiver->send_listsrc_params != NULL && nrts_src_addr != -1) {
	// 	if(nrts_src_addr != -1) {
	// 		while(receiver->send_listsrc_params != NULL) {
	// 		}
	// 	} else {
	// 		return;
	// 	}
	// 	// rte_exit(EXIT_FAILURE, "send_listsrc_params is not NULL");
	// }
	struct send_listsrc_params* send_listsrc_params = rte_zmalloc(" send_listsrc_params", 
            sizeof(struct send_listsrc_params), 0);
	send_listsrc_params->receiver = receiver;
	send_listsrc_params->pacer = pacer;
	send_listsrc_params->nrts_src_addr = nrts_src_addr;
	if(nrts_src_addr != -1) {
		rte_timer_reset_sync(&receiver->send_listsrc_timer, 0, SINGLE,
		                    5, &send_listsrc, (void *)send_listsrc_params);
	} else {
		int ret = rte_timer_reset(&receiver->send_listsrc_timer, 0, SINGLE,
		                    5, &send_listsrc, (void *)send_listsrc_params);
		if(ret != 0){
			rte_free(send_listsrc_params);
		}
	}
}
void send_listsrc(__rte_unused struct rte_timer *timer, void* arg) {
	// printf("cycels:%"PRIu64" send listsrc \n", rte_get_tsc_cycles());

	struct send_listsrc_params* timeout_params = (struct send_listsrc_params*)arg;
	struct rtp_receiver * receiver = timeout_params->receiver;
	struct rtp_pacer* pacer = timeout_params->pacer;
	int nrts_src_addr = timeout_params->nrts_src_addr;

	uint32_t* flow_id = 0;
	int32_t position = 0;
	uint32_t next = 0;
	struct rtp_flow* flow;
	rte_hash_reset(receiver->src_minflow_table);
	while(1) {
		position = rte_hash_iterate(receiver->rx_flow_table, (const void**) &flow_id, (void**)&flow, &next);
		if(position == -ENOENT) {
			break;
		}
		if(flow->finished_at_receiver) {
			continue;
		}
		if(flow->rd_ctrl_timeout_params != NULL) {
			continue;
		}
		struct rtp_flow* smallest_flow = lookup_table_entry(receiver->src_minflow_table, flow->_f.src_addr);
		if(rtp_flow_compare(smallest_flow, flow)) {
			if(smallest_flow != NULL) {
				delete_table_entry(receiver->src_minflow_table, flow->_f.src_addr);
			}
			insert_table_entry(receiver->src_minflow_table,flow->_f.src_addr, flow);
		}
	}
	// generate listsrc packets and pushes to the 
	next = 0;
	uint32_t* src_addr = 0;
	int32_t count = rte_hash_count(receiver->src_minflow_table);
	struct ipv4_hdr ipv4_hdr;
	struct rtp_hdr rtp_hdr;
	struct rtp_listsrc_hdr rtp_listsrc_hdr;
	struct rtp_nrts_hdr rtp_nrts_hdr;
	if(count == 0 && nrts_src_addr == -1){
		rte_free(timeout_params);
		// receiver->send_listsrc_params = NULL;
		return;
	}
	struct rte_mbuf* p = NULL;
	p = rte_pktmbuf_alloc(pktmbuf_pool);
	uint16_t size = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + 
		sizeof(struct rtp_hdr) + sizeof(struct rtp_listsrc_hdr);
	if(nrts_src_addr != -1) {
		size += sizeof(struct rtp_nrts_hdr);
	}
	size += count * sizeof(struct rtp_src_size_pair);
	if(p == NULL) {
		printf("size: %u\n", size);
		rte_exit(EXIT_FAILURE, "%s, p is NULL\n", __func__);
	}
	rte_pktmbuf_append(p, size);
	add_ether_hdr(p);
	ipv4_hdr.src_addr = rte_cpu_to_be_32(params.ip);
	ipv4_hdr.dst_addr = rte_cpu_to_be_32(params.controller_ip);
	ipv4_hdr.total_length = rte_cpu_to_be_16(size); 
	add_ip_hdr(p, &ipv4_hdr);
	rtp_hdr.type = RTP_LISTSRCS;
	add_rtp_hdr(p, &rtp_hdr);
	rtp_listsrc_hdr.num_srcs = count;
	if(nrts_src_addr != -1) {
		rtp_nrts_hdr.nrts_src_addr = (uint32_t)nrts_src_addr;
		rtp_nrts_hdr.nrts_dst_addr = (uint32_t)params.ip;
		rtp_listsrc_hdr.has_nrts = 1;
		add_rtp_listsrc_hdr(p, &rtp_listsrc_hdr);
		add_rtp_nrts_hdr(p, &rtp_nrts_hdr);
	} else {
		rtp_listsrc_hdr.has_nrts = 0;
		add_rtp_listsrc_hdr(p, &rtp_listsrc_hdr);
	}
	uint32_t offset = size - count * sizeof(struct rtp_src_size_pair);
	// printf("send list src\n");
	while(1) {
		struct rtp_src_size_pair* rtp_src_size_pair;
		rtp_src_size_pair = rte_pktmbuf_mtod_offset(p, struct rtp_src_size_pair*, offset);
		position = rte_hash_iterate(receiver->src_minflow_table, (const void**) &src_addr, (void**)&flow, &next);
		if(position == -ENOENT) {
			break;
		}
		rtp_src_size_pair->src_addr = *src_addr;
		rtp_src_size_pair->flow_size = flow->_f.size_in_pkt;
		offset += sizeof(struct rtp_src_size_pair);
		// printf("send listsrc: src address: %u; flow: %u\n", *src_addr, flow->_f.id);
	}
	// push the packet to the control packet of the pacer
	enqueue_ring(pacer->ctrl_q, p);
	rte_free(timeout_params);
	// receiver->send_listsrc_params = NULL;
}

void send_rts(struct rtp_sender *sender, struct rtp_pacer* pacer, struct rtp_flow* flow) {
	struct rte_mbuf* p = NULL;
	struct ipv4_hdr ipv4_hdr;
	struct rtp_hdr rtp_hdr;
	struct rtp_rts_hdr rtp_rts_hdr;
	p = rte_pktmbuf_alloc(pktmbuf_pool);
	uint16_t size = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + 
		sizeof(struct rtp_hdr) + sizeof(struct rtp_rts_hdr);
	if(p == NULL) {
		printf("new flow comes:%u\n", flow->_f.id);
		uint32_t q_size_0 = rte_ring_count(sender->long_flow_token_q);
		uint32_t q_size_1 = rte_ring_count(sender->short_flow_token_q);
		printf("size of sender long flow token q: %u\n", q_size_0);
		printf("size of sender short flow token q: %u\n", q_size_1);
		printf("size of sender control q: %u\n", rte_ring_count(pacer->ctrl_q));
		rte_exit(EXIT_FAILURE, "%s: pkt buffer is FULL\n", __func__);
	}
	rte_pktmbuf_append(p, size);
	if(p == NULL) {
		// printf("size of long flow token q: %u\n",rte_ring_count(receiver->long_flow_token_q));
		// printf("size of short flow token q: %u\n",rte_ring_count(receiver->short_flow_token_q));
		// printf("size of temp_pkt_buffer: %u\n",rte_ring_count(receiver->temp_pkt_buffer));
		// printf("size of control q: %u\n", rte_ring_count(pacer->ctrl_q));
		rte_exit(EXIT_FAILURE, "%s: pkt buffer is FULL\n", __func__);
	}
	add_ether_hdr(p);
	ipv4_hdr.src_addr = rte_cpu_to_be_32(flow->_f.src_addr);

	ipv4_hdr.dst_addr = rte_cpu_to_be_32(flow->_f.dst_addr);

	ipv4_hdr.total_length = rte_cpu_to_be_16(size); 

	add_ip_hdr(p, &ipv4_hdr);

	rtp_hdr.type = RTP_RTS;
	add_rtp_hdr(p, &rtp_hdr);
	rtp_rts_hdr.flow_id = flow->_f.id;
	rtp_rts_hdr.flow_size = flow->_f.size;
	rtp_rts_hdr.start_time = flow->_f.start_time;
	add_rtp_rts_hdr(p, & rtp_rts_hdr);
	//push the packet
    if(debug_flow(flow->_f.id)){
        printf("send rts %u\n", flow->_f.id);
    }
	enqueue_ring(pacer->ctrl_q, p);
}
void reset_idle_timeout(struct rtp_receiver *receiver, struct rtp_pacer* pacer) {
	double time = ((double)params.BDP) * params.idle_timeout * get_transmission_delay(1500);
	if(rte_ring_count(receiver->rx_flow_table) == 0) {
		return;
	}
	if(receiver->idle_timeout_params == NULL) {
		receiver->idle_timeout_params = rte_zmalloc("idle timeout param", 
    		sizeof(struct idle_timeout_params), 0);
		receiver->idle_timeout_params->receiver = receiver;
		receiver->idle_timeout_params->pacer = pacer;
	}
    int ret = rte_timer_reset(&receiver->idle_timeout, rte_get_timer_hz() * time, SINGLE,
                        rte_lcore_id(), &idle_timeout_handler, (void*)receiver->idle_timeout_params);
	if(ret != 0) {
        printf("%d: cannot reset timer\n", __LINE__);
        rte_exit(EXIT_FAILURE, "fail");
	}
}

void reset_send_tokens_evt(struct rtp_receiver *receiver, struct rtp_pacer* pacer, int sent_token) {
	double time = sent_token * get_transmission_delay(1500);
	if(receiver->send_token_evt_params == NULL) {
		receiver->send_token_evt_params = rte_zmalloc("idle timeout param", 
    		sizeof(struct send_token_evt_params), 0);
		receiver->send_token_evt_params->receiver = receiver;
		receiver->send_token_evt_params->pacer = pacer;
	}
    int ret = rte_timer_reset(&receiver->send_token_evt_timer, rte_get_timer_hz() * time, SINGLE,
                        rte_lcore_id(), &send_token_evt_handler, (void*)receiver->send_token_evt_params);
	if(ret != 0) {
        printf("%d: cannot reset timer\n", __LINE__);
        rte_exit(EXIT_FAILURE, "fail");
	}
}


void idle_timeout_handler(__rte_unused struct rte_timer *timer, void* arg) {
	struct idle_timeout_params *timeout_params = (struct idle_timeout_params *) arg;
	if(timeout_params->receiver->gosrc_info.has_gosrc && !timeout_params->receiver->gosrc_info.send_nrts) {
		printf("idle happens while the host has go_src");
		rte_exit(EXIT_FAILURE, "idle happens while the host has go_src");
	}
	timeout_params->receiver->idle_timeout_times += 1;
	invoke_send_listsrc(timeout_params->receiver, timeout_params->pacer, -1);
    reset_idle_timeout(timeout_params->receiver, timeout_params->pacer);
}

void send_token_evt_handler(__rte_unused struct rte_timer *timer, void* arg) {
	struct send_token_evt_params* evt_params = (struct send_token_evt_params*) arg;
	struct rtp_receiver* receiver = evt_params->receiver;
	struct rtp_pacer* pacer = evt_params->pacer;
	int rd_ctrl_set = 0;
	int sent_token = 0;
	if(!receiver->gosrc_info.has_gosrc) {
		rte_exit(EXIT_FAILURE, "send token without gosrc");
	}
    struct rtp_flow* rtp_flow = receiver->gosrc_info.current_flow;
    if(rtp_flow == NULL || rtp_flow->finished_at_receiver || rtp_flow->rd_ctrl_timeout_params != NULL) {
    	rtp_flow = get_src_smallest_unfinished_flow(receiver->gosrc_info.src_addr, receiver->rx_flow_table);

    }
	// case: when a flow finishes after receiving gosrc and no other flow exists.
	if (rtp_flow == NULL) {
		if(!receiver->gosrc_info.send_nrts) {
			receiver->gosrc_info.send_nrts = true;
			reset_gosrc(&receiver->gosrc_info);
			invoke_send_listsrc(receiver, pacer, receiver->gosrc_info.src_addr);
            if(receiver->idle_timeout_params != NULL) {
		        rte_exit(EXIT_FAILURE, "idle timeout params should be null");
            }
			reset_idle_timeout(receiver, pacer);
		}
		rte_free(receiver->send_token_evt_params);
		receiver->send_token_evt_params = NULL;
		return;
	}
    
    // push the batch_token number of tokens to the long flow token queue;
    int num_tokens;
    num_tokens = params.batch_tokens < receiver->gosrc_info.remain_tokens? 
    	params.batch_tokens: receiver->gosrc_info.remain_tokens;
    for(int i = 0; i < num_tokens; i++) {
    	if(rtp_flow == NULL) {
    		break;
    	}
    	int data_seq = rtp_get_next_token_seq_num(rtp_flow);
    	// allocate new packet
	 	struct rte_mbuf* p = NULL;
		p = rte_pktmbuf_alloc(pktmbuf_pool);
		if(p == NULL) {
			printf("size of long flow token q: %u\n",rte_ring_count(receiver->long_flow_token_q));
			printf("size of short flow token q: %u\n",rte_ring_count(receiver->short_flow_token_q));
			printf("size of temp_pkt_buffer: %u\n",rte_ring_count(receiver->temp_pkt_buffer));
			printf("size of control q: %u\n", rte_ring_count(pacer->ctrl_q));
			rte_exit(EXIT_FAILURE, "%s: pkt buffer is FULL\n", __func__);
		}
		uint16_t size = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + 
			sizeof(struct rtp_hdr) + sizeof(struct rtp_token_hdr);
		rte_pktmbuf_append(p, size);
		rtp_get_token_pkt(rtp_flow, p, receiver->gosrc_info.round, data_seq);
		
		enqueue_ring(receiver->long_flow_token_q, p);
		receiver->gosrc_info.remain_tokens -= 1;

		sent_token += 1;

		// check whether should set up the redundancy ctrl timeout
    	if (data_seq >= rtp_get_next_token_seq_num(rtp_flow)) {
    		if(rtp_flow->rd_ctrl_timeout_params != NULL) {
    			rte_exit(EXIT_FAILURE, "rd ctrl timeout is not null");
    		}
    		// set up redundancy ctrl timeout
    		reset_rd_ctrl_timeout(receiver, rtp_flow, params.BDP * get_transmission_delay(1500));
			rd_ctrl_set = 1;
			rtp_flow = get_src_smallest_unfinished_flow(receiver->gosrc_info.src_addr, receiver->rx_flow_table);
			receiver->gosrc_info.current_flow = rtp_flow;
    	}

    }
    // For pipeline; send liststc to the controller
    if(!receiver->gosrc_info.send_nrts) {
		int gap = 0;
	    double ctrl_pkt_rtt = get_rtt(params.propagation_delay, 3, 40);
	    if(rtp_flow == NULL) {
	    	gap = receiver->gosrc_info.remain_tokens;
	    	if(rd_ctrl_set != 1) {
		        printf("flow should be rd ctrl timeout: %u\n", __LINE__);
		        rte_exit(EXIT_FAILURE, "fail");
	    	}
	    }
	    else if(receiver->gosrc_info.remain_tokens > rtp_remaining_pkts(rtp_flow) - rtp_token_gap(rtp_flow)) {
	        gap = (int)(rtp_remaining_pkts(rtp_flow) - rtp_token_gap(rtp_flow));
	    } else {
	        gap = receiver->gosrc_info.remain_tokens;
	    }
        if ((rd_ctrl_set == 1 || 
            gap * get_transmission_delay(1500) <= 
            ctrl_pkt_rtt + 
            params.control_epoch * params.BDP * get_transmission_delay(1500))) {
            // this->fake_flow->sending_nrts_to_arbiter(f->src->id, f->dst->id);
            // this->gosrc_info.send_nrts = true;
			receiver->gosrc_info.send_nrts = true;
			invoke_send_listsrc(receiver, pacer, receiver->gosrc_info.src_addr);
            if(receiver->idle_timeout_params != NULL) {
		        rte_exit(EXIT_FAILURE, "idle timeout params should be null");
            }
			reset_idle_timeout(receiver, pacer);
        } 
    }
    // check whether all tokens has been used up
	if(receiver->gosrc_info.remain_tokens == 0) {
		if(receiver->gosrc_info.send_nrts == false) {
			rte_exit(EXIT_FAILURE, "Doesn't send nrts\n");
		}
		reset_gosrc(&receiver->gosrc_info);
		rte_free(receiver->send_token_evt_params);
		receiver->send_token_evt_params = NULL;
	} else {
		reset_send_tokens_evt(receiver, pacer, sent_token);
	}
}

// sender logic

void rtp_receive_token(struct rtp_sender *sender, struct rtp_token_hdr *rtp_token_hdr, struct rte_mbuf* p) {
	uint32_t flow_id = rtp_token_hdr->flow_id;
	struct rtp_flow* f = lookup_table_entry(sender->tx_flow_table, flow_id);
	if(f == NULL || f->_f.finished) {
		rte_pktmbuf_free(p);
		return;
	}
	f->remaining_pkts_at_sender = rtp_token_hdr->remaining_size;
	// need token timeout?
	if(rtp_token_hdr->priority == 1) {
		enqueue_ring(sender->short_flow_token_q, p);
	} else {
		enqueue_ring(sender->long_flow_token_q, p);
	}
	// schedule send event??
}
void iterate_temp_pkt_buf(struct rtp_receiver* receiver, struct rtp_pacer* pacer,
 uint32_t flow_id) {
	struct rte_ring* buf = receiver->temp_pkt_buffer;
	uint32_t size = rte_ring_count(buf);
	uint32_t i = 0;
	for(; i < size; i++) {
		struct rte_mbuf* p = NULL;
		p = (struct rte_mbuf*)dequeue_ring(buf);
		uint32_t offset = sizeof(struct ether_hdr) + 
		sizeof(struct ipv4_hdr) + sizeof(struct rtp_hdr);
		struct rtp_data_hdr *rtp_data_hdr = rte_pktmbuf_mtod_offset(p, struct rtp_data_hdr*, offset);
		if(rtp_data_hdr->flow_id == flow_id) {
			// packet free by rtp_receive_data
			rtp_receive_data(receiver, pacer, rtp_data_hdr, p);
		} else {
			enqueue_ring(buf, p);
		}
	}
}

void rtp_receive_ack(struct rtp_sender *sender, struct rtp_ack_hdr *rtp_ack_hdr) {
	uint32_t flow_id = rtp_ack_hdr->flow_id;
	struct rtp_flow* f = lookup_table_entry(sender->tx_flow_table, flow_id);
	f->_f.finished = true;
	f->_f.finish_time = rte_get_tsc_cycles();

	// struct finish_timeout_params *timeout_params = rte_zmalloc("finish timeout param", 
	// 	sizeof(struct finish_timeout_params), 0);
	// if(timeout_params == NULL) {
 //        printf("%d: no memory for timeout param \n", __LINE__);
 //        rte_exit(EXIT_FAILURE, "fail");
	// }
	sender->finished_flow += 1;
	// timeout_params->hash = sender->tx_flow_table;
	// // timeout_params->pool = sender->tx_flow_pool;
	// timeout_params->flow_id = flow_id;
	// printf("flow finish:%d\n", f->_f.id);
	// int ret = rte_timer_reset(&f->finish_timeout, rte_get_timer_hz() * 2 * get_rtt(params.propagation_delay, 3, 1500), SINGLE,
 //                    rte_lcore_id(), &finish_timeout_handler, (void *)timeout_params);
	// if(ret != 0) {
 //        printf("%d: cannot set up finish timer\n", __LINE__);
 //        rte_exit(EXIT_FAILURE, "fail");
	// }
}

//controller logic
void init_controller(struct rtp_controller* controller, uint32_t socket_id) {
	// controller->node_pool = create_mempool("mode_pool", sizeof(Node) + RTE_PKTMBUF_HEADROOM, 65536);
	// controller->element_pool = create_mempool("mode_pool", sizeof(64) + RTE_PKTMBUF_HEADROOM, 65536);
	controller->sender_state = create_hash_table("sender_state_table", sizeof(uint32_t), 100, 0, socket_id);
	controller->receiver_state = create_hash_table("receiver_state_table", sizeof(uint32_t), 100, 0, socket_id);
	for(uint32_t i = 0; i < 16; i++) {
		insert_table_entry(controller->sender_state, i, (void*)1);
		insert_table_entry(controller->receiver_state, i, (void*)1);
	}
	controller->head = NULL;
	rte_timer_init(&controller->handle_rq_timer);
}

void rtp_receive_listsrc(struct rtp_controller *controller, struct rte_mbuf *p) {
	struct ipv4_hdr* ipv4_hdr = NULL;
	struct rtp_hdr* rtp_hdr = NULL;
	struct rtp_listsrc_hdr* listsrc_hdr = NULL;
	struct rtp_nrts_hdr* nrts_hdr = NULL;
	parse_header(p, &ipv4_hdr, &rtp_hdr);
	uint32_t offset = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) +
		sizeof(struct rtp_hdr);
	if(rtp_hdr->type == RTP_LISTSRCS) {
		listsrc_hdr = rte_pktmbuf_mtod_offset(p, struct rtp_listsrc_hdr*, offset);
		offset += sizeof(struct rtp_listsrc_hdr);
		if(listsrc_hdr->has_nrts == 1) {
			nrts_hdr = rte_pktmbuf_mtod_offset(p, struct rtp_nrts_hdr*, offset);
			offset += sizeof(struct rtp_nrts_hdr);
			 // printf("receive nrts src:%u dst %u\n",nrts_hdr->nrts_src_addr, 
			// nrts_hdr->nrts_dst_addr);
			delete_table_entry(controller->sender_state, ip_to_id(nrts_hdr->nrts_src_addr));
			insert_table_entry(controller->sender_state, ip_to_id(nrts_hdr->nrts_src_addr), (void*)1);
			delete_table_entry(controller->receiver_state, ip_to_id(nrts_hdr->nrts_dst_addr));
			insert_table_entry(controller->receiver_state, ip_to_id(nrts_hdr->nrts_dst_addr), (void*)1);
		}
		struct rtp_src_size_pair* rtp_src_size_pair = NULL;
		uint32_t i = 0;
		uint32_t size = listsrc_hdr->num_srcs;
		for(; i < size; i++) {
			rtp_src_size_pair = rte_pktmbuf_mtod_offset(p, struct rtp_src_size_pair*, offset);
			struct src_dst_pair* src_dst_pair = rte_zmalloc("", sizeof(struct src_dst_pair), 0);
			src_dst_pair->src = ip_to_id(rtp_src_size_pair->src_addr);
			// src address is the receiver address
			src_dst_pair->dst = ip_to_id(rte_be_to_cpu_32(ipv4_hdr->src_addr));
			if(controller->head == NULL) {
				controller->head = newNode(src_dst_pair, rtp_src_size_pair->flow_size);
			} else {
				push(&controller->head, src_dst_pair, rtp_src_size_pair->flow_size);
			}
			offset += sizeof(struct rtp_src_size_pair);
		}
		rte_pktmbuf_free(p);

	} else {
		rte_exit(EXIT_FAILURE, "should only receive RTP_LISTSRCS");
	}
}

void handle_requests(__rte_unused struct rte_timer *timer, void* arg) {
	struct rtp_controller* controller = (struct rtp_controller*) arg;
	while(!isEmpty(&controller->head)) {
		struct src_dst_pair* src_dst_pair = peek(&controller->head);
		void* src_state = lookup_table_entry(controller->sender_state, src_dst_pair->src);
		void* dst_state = lookup_table_entry(controller->receiver_state, src_dst_pair->dst);
		if(src_state == (void*)1 && dst_state == (void*)1) {
			// printf("src_state:%d dst_state:%d\n", src_state, dst_state);
			delete_table_entry(controller->sender_state, src_dst_pair->src);
			delete_table_entry(controller->receiver_state, src_dst_pair->dst);
			insert_table_entry(controller->sender_state, src_dst_pair->src, 0);
			insert_table_entry(controller->receiver_state, src_dst_pair->dst, 0);
			// send gosrc packets
			uint32_t src_addr = id_to_ip[src_dst_pair->src];
			uint32_t dst_addr = id_to_ip[src_dst_pair->dst];
			uint32_t token_num = ((uint32_t)rte_rand()) % 
				((uint32_t)(params.max_tokens * params.BDP - params.min_tokens * params.BDP)) + params.min_tokens * params.BDP;
			struct rte_mbuf* p = NULL;
			p = rte_pktmbuf_alloc(pktmbuf_pool);
			uint16_t size = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + 
			sizeof(struct rtp_hdr) + sizeof(struct rtp_gosrc_hdr);
			if(p == NULL) {
				rte_exit(EXIT_FAILURE, "P is NULL");
			}
			rte_pktmbuf_append(p, size);
			get_gosrc_pkt(p, src_addr, dst_addr, token_num);
			rte_eth_tx_burst(get_port_by_ip(dst_addr) ,0,&p,1);
			// printf("assign sender %u to receiver %d; tokens:%u \n", src_addr, dst_addr, token_num);
		}
		rte_free(src_dst_pair);
		pop(&controller->head);
	}
}

void get_gosrc_pkt(struct rte_mbuf* p, uint32_t src_addr, uint32_t dst_addr, uint32_t token_num) {
    add_ether_hdr(p);
    struct ipv4_hdr ipv4_hdr;
    struct rtp_hdr rtp_hdr;
    struct rtp_gosrc_hdr rtp_gosrc_hdr;
    uint16_t size;
    size = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + 
        sizeof(struct rtp_hdr) + sizeof(struct rtp_gosrc_hdr);
    ipv4_hdr.src_addr = rte_cpu_to_be_32(params.ip);
    ipv4_hdr.dst_addr = rte_cpu_to_be_32(dst_addr);
    ipv4_hdr.total_length = rte_cpu_to_be_16(size);
    add_ip_hdr(p, &ipv4_hdr);

    rtp_hdr.type = RTP_GOSRC;
    add_rtp_hdr(p, &rtp_hdr);
    rtp_gosrc_hdr.target_src_addr = src_addr;
    rtp_gosrc_hdr.max_tokens = token_num;
    add_rtp_gosrc_hdr(p, & rtp_gosrc_hdr);
}