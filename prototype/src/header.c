#include <rte_memcpy.h>
#include "header.h"

void parse_header(struct rte_mbuf* p, struct ipv4_hdr** ipv4_hdr, struct rtp_hdr** rtp_hdr) {
	uint32_t offset = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr);
	// get ip header
	*ipv4_hdr = rte_pktmbuf_mtod_offset(p, struct ipv4_hdr *, sizeof(struct ether_hdr));
	// get rtp header
	*rtp_hdr = rte_pktmbuf_mtod_offset(p, struct rtp_hdr *, offset);
}
void add_ether_hdr(struct rte_mbuf* p) {
	struct ether_hdr *eth_hdr;
	eth_hdr = rte_pktmbuf_mtod(p, struct ether_hdr *);
	eth_hdr->ether_type = htons(0x0800);
	eth_hdr->d_addr.addr_bytes[0] = 0;

}
void add_ip_hdr(struct rte_mbuf* p, struct ipv4_hdr* ipv4_hdr) {
	struct ipv4_hdr* hdr;
	uint32_t offset = sizeof(struct ether_hdr);
	hdr = rte_pktmbuf_mtod_offset(p, struct ipv4_hdr*, offset);
	rte_memcpy(hdr, ipv4_hdr, sizeof(struct ipv4_hdr));
}
void add_rtp_hdr(struct rte_mbuf* p, struct rtp_hdr* rtp_hdr) {
	struct  rtp_hdr* hdr;
	uint32_t offset = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr);
	hdr = rte_pktmbuf_mtod_offset(p, struct rtp_hdr*, offset);
	rte_memcpy(hdr, rtp_hdr, sizeof(struct rtp_hdr));
}
void add_rtp_rts_hdr(struct rte_mbuf *p, struct rtp_rts_hdr* rtp_rts_hdr) {
	struct rtp_rts_hdr *hdr;
	uint32_t offset = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) +
	sizeof(struct rtp_hdr);
	hdr = rte_pktmbuf_mtod_offset(p, struct rtp_rts_hdr*, offset);
	rte_memcpy(hdr, rtp_rts_hdr, sizeof(struct rtp_rts_hdr));
}
void add_rtp_gosrc_hdr(struct rte_mbuf *p, struct rtp_gosrc_hdr* rtp_gosrc_hdr) {
	struct rtp_gosrc_hdr *hdr;
	uint32_t offset = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) +
	sizeof(struct rtp_hdr);
	hdr = rte_pktmbuf_mtod_offset(p, struct rtp_gosrc_hdr*, offset);
	rte_memcpy(hdr, rtp_gosrc_hdr, sizeof(struct rtp_gosrc_hdr));
}
void add_rtp_listsrc_hdr(struct rte_mbuf *p, struct rtp_listsrc_hdr* rtp_listsrc_hdr) {
	struct rtp_listsrc_hdr *hdr;
	uint32_t offset = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) +
	sizeof(struct rtp_hdr);
	hdr = rte_pktmbuf_mtod_offset(p, struct rtp_listsrc_hdr*, offset);
	rte_memcpy(hdr, rtp_listsrc_hdr, sizeof(struct rtp_listsrc_hdr));
}
void add_rtp_nrts_hdr(struct rte_mbuf *p, struct rtp_nrts_hdr* rtp_nrts_hdr) {
	struct rtp_nrts_hdr *hdr;
	uint32_t offset = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) +
	sizeof(struct rtp_hdr) + sizeof(struct rtp_listsrc_hdr);
	hdr = rte_pktmbuf_mtod_offset(p, struct rtp_nrts_hdr*, offset);
	rte_memcpy(hdr, rtp_nrts_hdr, sizeof(struct rtp_nrts_hdr));
}
void add_rtp_token_hdr(struct rte_mbuf *p, struct rtp_token_hdr* rtp_token_hdr) {
	struct rtp_token_hdr *hdr;
	uint32_t offset = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) +
	sizeof(struct rtp_hdr);
	hdr = rte_pktmbuf_mtod_offset(p, struct rtp_token_hdr*, offset);
	rte_memcpy(hdr, rtp_token_hdr, sizeof(struct rtp_token_hdr));

}
void add_rtp_data_hdr(struct rte_mbuf *p, struct rtp_data_hdr* rtp_data_hdr) {
	struct rtp_data_hdr *hdr;
	uint32_t offset = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) +
	sizeof(struct rtp_hdr);
	hdr = rte_pktmbuf_mtod_offset(p, struct rtp_data_hdr*, offset);
	rte_memcpy(hdr, rtp_data_hdr, sizeof(struct rtp_data_hdr));
}
void add_rtp_ack_hdr(struct rte_mbuf *p, struct rtp_ack_hdr* rtp_ack_hdr) {
	struct rtp_ack_hdr *hdr;
	uint32_t offset = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) +
	sizeof(struct rtp_hdr);
	hdr = rte_pktmbuf_mtod_offset(p, struct rtp_ack_hdr*, offset);
	rte_memcpy(hdr, rtp_ack_hdr, sizeof(struct rtp_ack_hdr));
}