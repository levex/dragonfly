/*
 * Copyright (c) 2003 Jeffrey Hsu
 * All rights reserved.
 *
 * $DragonFly: src/sys/netinet/ip_demux.c,v 1.17 2004/04/09 23:33:02 hsu Exp $
 */

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/thread.h>
#include <sys/sysctl.h>
#include <sys/globaldata.h>

#include <net/if.h>
#include <net/netisr.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcpip.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

extern struct thread netisr_cpu[];

static struct thread tcp_thread[MAXCPU];
static struct thread udp_thread[MAXCPU];

static __inline int
INP_MPORT_HASH(in_addr_t faddr, in_addr_t laddr,
	       in_port_t fport, in_port_t lport)
{
	/*
	 * Use low order bytes.
	 */

#if (BYTE_ORDER == LITTLE_ENDIAN)
	KASSERT(ncpus2 < 256, ("need different hash function"));  /* XXX JH */
	return (((faddr >> 24) ^ (fport >> 8) ^ (laddr >> 24) ^ (lport >> 8)) &
		ncpus2_mask);
#else
	return ((faddr ^ fport ^ laddr ^ lport) & ncpus2_mask);
#endif
}

/*
 * Map a packet to a protocol processing thread.
 */
lwkt_port_t
ip_mport(struct mbuf *m)
{
	struct ip *ip;
	int iphlen;
	struct tcphdr *th;
	struct udphdr *uh;
	int thoff;				/* TCP data offset */
	lwkt_port_t port;
	int cpu;

	if (m->m_pkthdr.len < sizeof(struct ip)) {
		ipstat.ips_tooshort++;
		return (NULL);
	}

	if (m->m_len < sizeof(struct ip) &&
	    (m = m_pullup(m, sizeof(struct ip))) == NULL) {
		ipstat.ips_toosmall++;
		return (NULL);
	}

	ip = mtod(m, struct ip *);

	/*
	 * XXX generic packet handling defrag on CPU 0 for now.
	 */
	if (ntohs(ip->ip_off) & (IP_MF | IP_OFFMASK))
		return (&netisr_cpu[0].td_msgport);

	iphlen = ip->ip_hl << 2;
	if (iphlen < sizeof(struct ip)) {	/* minimum header length */
		ipstat.ips_badhlen++;
		return (NULL);
	}

	switch (ip->ip_p) {
	case IPPROTO_TCP:
		if (m->m_len < iphlen + sizeof(struct tcphdr) &&
		    (m = m_pullup(m, iphlen + sizeof(struct tcphdr))) == NULL) {
			tcpstat.tcps_rcvshort++;
			return (NULL);
		}
		th = (struct tcphdr *)((caddr_t)ip + iphlen);
		thoff = th->th_off << 2;
		if (thoff < sizeof(struct tcphdr) ||
		    thoff > ntohs(ip->ip_len)) {
			tcpstat.tcps_rcvbadoff++;
			return (NULL);
		}
		if (m->m_len < iphlen + thoff) {
			m = m_pullup(m, iphlen + thoff);
			if (m == NULL) {
				tcpstat.tcps_rcvshort++;
				return (NULL);
			}
			ip = mtod(m, struct ip *);
			th = (struct tcphdr *)((caddr_t)ip + iphlen);
		}

		cpu = INP_MPORT_HASH(ip->ip_src.s_addr, ip->ip_dst.s_addr,
		    th->th_sport, th->th_dport);
		port = &tcp_thread[cpu].td_msgport;
		break;
	case IPPROTO_UDP:
		if (m->m_len < iphlen + sizeof(struct udphdr)) {
			m = m_pullup(m, iphlen + sizeof(struct udphdr));
			if (m == NULL) {
				udpstat.udps_hdrops++;
				return (NULL);
			}
			ip = mtod(m, struct ip *);
		}
		uh = (struct udphdr *)((caddr_t)ip + iphlen);

		if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr)) ||
		    in_broadcast(ip->ip_dst, m->m_pkthdr.rcvif)) {
			cpu = 0;
		} else {
			cpu = INP_MPORT_HASH(ip->ip_src.s_addr,
			    ip->ip_dst.s_addr, uh->uh_sport, uh->uh_dport);
		}
		port = &udp_thread[cpu].td_msgport;
		break;
	default:
		if (m->m_len < iphlen && (m = m_pullup(m, iphlen)) == NULL) {
			ipstat.ips_badhlen++;
			return (NULL);
		}
		port = &netisr_cpu[0].td_msgport;
		break;
	}
	KKASSERT(port->mp_putport != NULL);

	return (port);
}

/*
 * Map a TCP socket to a protocol processing thread.
 */
lwkt_port_t
tcp_soport(struct socket *so, struct sockaddr *nam)
{
	struct inpcb *inp;

	/*
	 * The following processing all take place on Protocol Thread 0:
	 *   only bind() and connect() have a non-null nam parameter
	 *   attach() has a null socket parameter
	 *   Fast and slow timeouts pass in two NULLs
	 */
	if (nam != NULL || so == NULL)
		return (&tcp_thread[0].td_msgport);

	/*
	 * Already bound and connected.  For TCP connections, the
	 * (faddr, fport, laddr, lport) association cannot change now.
	 *
	 * Note: T/TCP code needs some reorganization to fit into
	 * this model.  XXX JH
	 */
	inp = sotoinpcb(so);
	if (!inp)		/* connection reset by peer */
		return (&tcp_thread[0].td_msgport);

	/*
	 * Rely on type-stable memory and check in protocol handler
	 * to fix race condition here w/ deallocation of inp.  XXX JH
	 */

	return (&tcp_thread[INP_MPORT_HASH(inp->inp_faddr.s_addr,
	    inp->inp_laddr.s_addr, inp->inp_fport, inp->inp_lport)].td_msgport);
}

/*
 * Map a UDP socket to a protocol processing thread.
 */
lwkt_port_t
udp_soport(struct socket *so, struct sockaddr *nam)
{
	struct inpcb *inp;

	/*
	 * The following processing all take place on Protocol Thread 0:
	 *   only bind() and connect() have a non-null nam parameter
	 *   attach() has a null socket parameter
	 *   Fast and slow timeouts pass in two NULLs
	 */
	if (nam != NULL || so == NULL)
		return (&udp_thread[0].td_msgport);

	inp = sotoinpcb(so);

	if (IN_MULTICAST(ntohl(inp->inp_laddr.s_addr)))
		return (&udp_thread[0].td_msgport);

	/*
	 * Rely on type-stable memory and check in protocol handler
	 * to fix race condition here w/ deallocation of inp.  XXX JH
	 */

	return (&udp_thread[INP_MPORT_HASH(inp->inp_faddr.s_addr,
	    inp->inp_laddr.s_addr, inp->inp_fport, inp->inp_lport)].td_msgport);
}

/*
 * Map a network address to a processor.
 */
int
tcp_addrcpu(in_addr_t faddr, in_port_t fport, in_addr_t laddr, in_port_t lport)
{
	return (INP_MPORT_HASH(faddr, laddr, fport, lport));
}

int
udp_addrcpu(in_addr_t faddr, in_port_t fport, in_addr_t laddr, in_port_t lport)
{
	if (IN_MULTICAST(ntohl(laddr)))
		return (0);
	else
		return (INP_MPORT_HASH(faddr, laddr, fport, lport));
}

/*
 * Return LWKT port for cpu.
 */
lwkt_port_t
tcp_cport(int cpu)
{
	return (&tcp_thread[cpu].td_msgport);
}

/*
 * We must construct a custom putport function (which runs in the context
 * of the message originator)
 * Our custom putport must check for self-referential messages, which can
 * occur when the so_upcall routine is called (e.g. nfs).  Self referential
 * messages are simply executed synchronously.
 */
static int
netmsg_put_port(lwkt_port_t port, lwkt_msg_t lmsg)
{
    /*
     * If it's a synchronous message for the same thread,
     * execute it directly.
     */
    if (!(lmsg->ms_flags & MSGF_ASYNC) && port->mp_td == curthread) {
	struct netmsg *msg = (struct netmsg *)lmsg;

	msg->nm_handler(msg);
    } else {
        lwkt_default_putport(port, lmsg);
    }

    return (EASYNC);
}

void
tcp_thread_init(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus2; cpu++) {
		lwkt_create(netmsg_service_loop, NULL, NULL, 
			&tcp_thread[cpu], 0, cpu, "tcp_thread %d", cpu);
		tcp_thread[cpu].td_msgport.mp_putport = netmsg_put_port;
	}
}

void
udp_thread_init(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus2; cpu++) {
		lwkt_create(netmsg_service_loop, NULL, NULL,
			&udp_thread[cpu], 0, cpu, "udp_thread %d", cpu);
		udp_thread[cpu].td_msgport.mp_putport = netmsg_put_port;
	}
}
