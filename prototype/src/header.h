#ifndef HEADER_H
#define HEADER_H

#include "debug.h"

#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
// RANKING
#define DATA 0
#define RTP_RTS 1
#define RTP_LISTSRCS 2
#define RTP_GOSRC 3
#define RTP_TOKEN 4 
#define RTP_ACK 5

#define MSS 1460

// ------- RTP -----
// RTP header format: ethernet | IPv4 header | rtp_hdr| (rts, gosrc, listsrc, token, data)
// If it is listsrc: listsrc | nrts_hdr (optional) | list of (src_addr, flow_size) pairs
struct rtp_hdr{
	uint8_t type;
};

struct rtp_rts_hdr {
	uint32_t flow_id;
	uint32_t flow_size;
	uint64_t start_time;
};

struct rtp_gosrc_hdr {
	uint32_t target_src_addr;
	uint32_t max_tokens;
};

struct rtp_listsrc_hdr {
	uint8_t has_nrts;
	uint32_t num_srcs; 
};

struct rtp_nrts_hdr {
	uint32_t nrts_src_addr;
	uint32_t nrts_dst_addr;
};

struct rtp_src_size_pair {
	uint32_t src_addr;
	uint32_t flow_size;
};

struct rtp_token_hdr {
	uint8_t priority;
	// uint8_t ttl;
	uint32_t flow_id;
	uint32_t round;
	uint32_t data_seq;
	uint32_t seq_num;
	uint32_t remaining_size;
}; 

struct rtp_data_hdr{
	uint8_t priority;
	uint32_t flow_id;
	uint32_t round;
	uint32_t data_seq;
	uint32_t seq_num;
};

struct rtp_ack_hdr {
	uint32_t flow_id;
};

void parse_header(struct rte_mbuf* p, struct ipv4_hdr** ipv4_hdr, struct rtp_hdr** rtp_hdr);
void add_ether_hdr(struct rte_mbuf* p);
void add_ip_hdr(struct rte_mbuf* p, struct ipv4_hdr* ipv4_hdr);
void add_rtp_hdr(struct rte_mbuf* p, struct rtp_hdr* rtp_hdr);
void add_rtp_rts_hdr(struct rte_mbuf *p, struct rtp_rts_hdr* rtp_rts_hdr);
void add_rtp_gosrc_hdr(struct rte_mbuf *p, struct rtp_gosrc_hdr* rtp_gosrc_hdr);
void add_rtp_listsrc_hdr(struct rte_mbuf *p, struct rtp_listsrc_hdr* rtp_listsrc_hdr);
void add_rtp_nrts_hdr(struct rte_mbuf *p, struct rtp_nrts_hdr* rtp_nrts_hdr);
void add_rtp_token_hdr(struct rte_mbuf *p, struct rtp_token_hdr* rtp_token_hdr);
void add_rtp_data_hdr(struct rte_mbuf *p, struct rtp_data_hdr* rtp_data_hdr);
void add_rtp_ack_hdr(struct rte_mbuf *p, struct rtp_ack_hdr* rtp_ack_hdr);

#endif

