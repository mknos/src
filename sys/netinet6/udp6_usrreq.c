/* $NetBSD: udp6_usrreq.c,v 1.155 2024/07/05 04:31:54 rin Exp $ */
/* $KAME: udp6_usrreq.c,v 1.86 2001/05/27 17:33:00 itojun Exp $ */
/* $KAME: udp6_output.c,v 1.43 2001/10/15 09:19:52 itojun Exp $ */

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)udp_var.h	8.1 (Berkeley) 6/10/93
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: udp6_usrreq.c,v 1.155 2024/07/05 04:31:54 rin Exp $");

#ifdef _KERNEL_OPT
#include "opt_inet.h"
#include "opt_inet_csum.h"
#include "opt_ipsec.h"
#include "opt_net_mpsafe.h"
#endif

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/syslog.h>
#include <sys/domain.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/in_offload.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/in_pcb.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/udp_private.h>

#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/ip6_private.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/udp6_var.h>
#include <netinet6/udp6_private.h>
#include <netinet6/ip6protosw.h>
#include <netinet6/scope6_var.h>

#ifdef IPSEC
#include <netipsec/ipsec.h>
#include <netipsec/esp.h>
#ifdef INET6
#include <netipsec/ipsec6.h>
#endif
#endif

#include "faith.h"
#if defined(NFAITH) && NFAITH > 0
#include <net/if_faith.h>
#endif

/*
 * UDP protocol implementation.
 * Per RFC 768, August, 1980.
 */

extern struct inpcbtable udbtable;

percpu_t *udp6stat_percpu;

/* UDP on IP6 parameters */
static int udp6_sendspace = 9216;	/* really max datagram size */
static int udp6_recvspace = 40 * (1024 + sizeof(struct sockaddr_in6));
					/* 40 1K datagrams */

static void udp6_notify(struct inpcb *, int);
static void sysctl_net_inet6_udp6_setup(struct sysctllog **);
#ifdef IPSEC
static int udp6_espinudp(struct mbuf **, int);
#endif

#ifdef UDP_CSUM_COUNTERS
#include <sys/device.h>
struct evcnt udp6_hwcsum_bad = EVCNT_INITIALIZER(EVCNT_TYPE_MISC,
    NULL, "udp6", "hwcsum bad");
struct evcnt udp6_hwcsum_ok = EVCNT_INITIALIZER(EVCNT_TYPE_MISC,
    NULL, "udp6", "hwcsum ok");
struct evcnt udp6_hwcsum_data = EVCNT_INITIALIZER(EVCNT_TYPE_MISC,
    NULL, "udp6", "hwcsum data");
struct evcnt udp6_swcsum = EVCNT_INITIALIZER(EVCNT_TYPE_MISC,
    NULL, "udp6", "swcsum");

EVCNT_ATTACH_STATIC(udp6_hwcsum_bad);
EVCNT_ATTACH_STATIC(udp6_hwcsum_ok);
EVCNT_ATTACH_STATIC(udp6_hwcsum_data);
EVCNT_ATTACH_STATIC(udp6_swcsum);

#define	UDP_CSUM_COUNTER_INCR(ev)	(ev)->ev_count++
#else
#define	UDP_CSUM_COUNTER_INCR(ev)	/* nothing */
#endif

void
udp6_init(void)
{
	sysctl_net_inet6_udp6_setup(NULL);
	udp6stat_percpu = percpu_alloc(sizeof(uint64_t) * UDP6_NSTATS);

	udp_init_common();
}

/*
 * Notify a udp user of an asynchronous error;
 * just wake up so that he can collect error status.
 */
static	void
udp6_notify(struct inpcb *inp, int errno)
{
	inp->inp_socket->so_error = errno;
	sorwakeup(inp->inp_socket);
	sowwakeup(inp->inp_socket);
}

void *
udp6_ctlinput(int cmd, const struct sockaddr *sa, void *d)
{
	struct udphdr uh;
	struct ip6_hdr *ip6;
	const struct sockaddr_in6 *sa6 = (const struct sockaddr_in6 *)sa;
	struct mbuf *m;
	int off;
	void *cmdarg;
	struct ip6ctlparam *ip6cp = NULL;
	const struct sockaddr_in6 *sa6_src = NULL;
	void (*notify)(struct inpcb *, int) = udp6_notify;
	struct udp_portonly {
		u_int16_t uh_sport;
		u_int16_t uh_dport;
	} *uhp;

	if (sa->sa_family != AF_INET6 ||
	    sa->sa_len != sizeof(struct sockaddr_in6))
		return NULL;

	if ((unsigned)cmd >= PRC_NCMDS)
		return NULL;
	if (PRC_IS_REDIRECT(cmd))
		notify = in6pcb_rtchange, d = NULL;
	else if (cmd == PRC_HOSTDEAD)
		d = NULL;
	else if (cmd == PRC_MSGSIZE) {
		/* special code is present, see below */
		notify = in6pcb_rtchange;
	}
	else if (inet6ctlerrmap[cmd] == 0)
		return NULL;

	/* if the parameter is from icmp6, decode it. */
	if (d != NULL) {
		ip6cp = (struct ip6ctlparam *)d;
		m = ip6cp->ip6c_m;
		ip6 = ip6cp->ip6c_ip6;
		off = ip6cp->ip6c_off;
		cmdarg = ip6cp->ip6c_cmdarg;
		sa6_src = ip6cp->ip6c_src;
	} else {
		m = NULL;
		ip6 = NULL;
		cmdarg = NULL;
		sa6_src = &sa6_any;
		off = 0;
	}

	if (ip6) {
		/* check if we can safely examine src and dst ports */
		if (m->m_pkthdr.len < off + sizeof(*uhp)) {
			if (cmd == PRC_MSGSIZE)
				icmp6_mtudisc_update((struct ip6ctlparam *)d, 0);
			return NULL;
		}

		memset(&uh, 0, sizeof(uh));
		m_copydata(m, off, sizeof(*uhp), (void *)&uh);

		if (cmd == PRC_MSGSIZE) {
			int valid = 0;

			/*
			 * Check to see if we have a valid UDP socket
			 * corresponding to the address in the ICMPv6 message
			 * payload.
			 */
			if (in6pcb_lookup(&udbtable, &sa6->sin6_addr,
			    uh.uh_dport, (const struct in6_addr *)&sa6_src->sin6_addr,
			    uh.uh_sport, 0, 0))
				valid++;
#if 0
			/*
			 * As the use of sendto(2) is fairly popular,
			 * we may want to allow non-connected pcb too.
			 * But it could be too weak against attacks...
			 * We should at least check if the local address (= s)
			 * is really ours.
			 */
			else if (in6pcb_lookup_bound(&udbtable, &sa6->sin6_addr,
			    uh.uh_dport, 0))
				valid++;
#endif

			/*
			 * Depending on the value of "valid" and routing table
			 * size (mtudisc_{hi,lo}wat), we will:
			 * - recalculate the new MTU and create the
			 *   corresponding routing entry, or
			 * - ignore the MTU change notification.
			 */
			icmp6_mtudisc_update((struct ip6ctlparam *)d, valid);

			/*
			 * regardless of if we called
			 * icmp6_mtudisc_update(), we need to call
			 * in6pcb_notify(), to notify path MTU change
			 * to the userland (RFC3542), because some
			 * unconnected sockets may share the same
			 * destination and want to know the path MTU.
			 */
		}

		(void)in6pcb_notify(&udbtable, sa, uh.uh_dport,
		    sin6tocsa(sa6_src), uh.uh_sport, cmd, cmdarg,
		    notify);
	} else {
		(void)in6pcb_notify(&udbtable, sa, 0,
		    sin6tocsa(sa6_src), 0, cmd, cmdarg, notify);
	}
	return NULL;
}

int
udp6_ctloutput(int op, struct socket *so, struct sockopt *sopt)
{
	int s;
	int error = 0;
	struct inpcb *inp;
	int family;
	int optval;

	family = so->so_proto->pr_domain->dom_family;

	s = splsoftnet();
	switch (family) {
#ifdef INET
	case PF_INET:
		if (sopt->sopt_level != IPPROTO_UDP) {
			error = ip_ctloutput(op, so, sopt);
			goto end;
		}
		break;
#endif
#ifdef INET6
	case PF_INET6:
		if (sopt->sopt_level != IPPROTO_UDP) {
			error = ip6_ctloutput(op, so, sopt);
			goto end;
		}
		break;
#endif
	default:
		error = EAFNOSUPPORT;
		goto end;
	}

	switch (op) {
	case PRCO_SETOPT:
		inp = sotoinpcb(so);

		switch (sopt->sopt_name) {
		case UDP_ENCAP:
			error = sockopt_getint(sopt, &optval);
			if (error)
				break;

			switch(optval) {
			case 0:
				inp->inp_flags &= ~IN6P_ESPINUDP;
				break;

			case UDP_ENCAP_ESPINUDP:
				inp->inp_flags |= IN6P_ESPINUDP;
				break;

			default:
				error = EINVAL;
				break;
			}
			break;

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;

	default:
		error = EINVAL;
		break;
	}

end:
	splx(s);
	return error;
}

static void
udp6_sendup(struct mbuf *m, int off /* offset of data portion */,
    struct sockaddr *src, struct socket *so)
{
	struct mbuf *opts = NULL;
	struct mbuf *n;
	struct inpcb *inp;

	KASSERT(so != NULL);
	KASSERT(so->so_proto->pr_domain->dom_family == AF_INET6);
	inp = sotoinpcb(so);
	KASSERT(inp != NULL);

#if defined(IPSEC)
	if (ipsec_used && ipsec_in_reject(m, inp)) {
		if ((n = m_copypacket(m, M_DONTWAIT)) != NULL)
			icmp6_error(n, ICMP6_DST_UNREACH,
			    ICMP6_DST_UNREACH_ADMIN, 0);
		return;
	}
#endif

	if ((n = m_copypacket(m, M_DONTWAIT)) != NULL) {
		if (inp->inp_flags & IN6P_CONTROLOPTS ||
		    SOOPT_TIMESTAMP(inp->inp_socket->so_options)) {
			struct ip6_hdr *ip6 = mtod(n, struct ip6_hdr *);
			ip6_savecontrol(inp, &opts, ip6, n);
		}

		m_adj(n, off);
		if (sbappendaddr(&so->so_rcv, src, n, opts) == 0) {
			m_freem(n);
			m_freem(opts);
			UDP6_STATINC(UDP6_STAT_FULLSOCK);
			soroverflow(so);
		} else
			sorwakeup(so);
	}
}

int
udp6_realinput(int af, struct sockaddr_in6 *src, struct sockaddr_in6 *dst,
    struct mbuf **mp, int off)
{
	u_int16_t sport, dport;
	int rcvcnt;
	struct in6_addr src6, *dst6;
	const struct in_addr *dst4;
	struct inpcb *inp;
	struct mbuf *m = *mp;

	rcvcnt = 0;
	off += sizeof(struct udphdr);	/* now, offset of payload */

	if (af != AF_INET && af != AF_INET6)
		goto bad;
	if (src->sin6_family != AF_INET6 || dst->sin6_family != AF_INET6)
		goto bad;

	src6 = src->sin6_addr;
	if (sa6_recoverscope(src) != 0) {
		/* XXX: should be impossible. */
		goto bad;
	}
	sport = src->sin6_port;

	dport = dst->sin6_port;
	dst4 = (struct in_addr *)&dst->sin6_addr.s6_addr[12];
	dst6 = &dst->sin6_addr;

	if (IN6_IS_ADDR_MULTICAST(dst6) ||
	    (af == AF_INET && IN_MULTICAST(dst4->s_addr))) {
		/*
		 * Deliver a multicast or broadcast datagram to *all* sockets
		 * for which the local and remote addresses and ports match
		 * those of the incoming datagram.  This allows more than
		 * one process to receive multi/broadcasts on the same port.
		 * (This really ought to be done for unicast datagrams as
		 * well, but that would cause problems with existing
		 * applications that open both address-specific sockets and
		 * a wildcard socket listening to the same port -- they would
		 * end up receiving duplicates of every unicast datagram.
		 * Those applications open the multiple sockets to overcome an
		 * inadequacy of the UDP socket interface, but for backwards
		 * compatibility we avoid the problem here rather than
		 * fixing the interface.  Maybe 4.5BSD will remedy this?)
		 */

		/*
		 * KAME note: traditionally we dropped udpiphdr from mbuf here.
		 * we need udpiphdr for IPsec processing so we do that later.
		 */
		/*
		 * Locate pcb(s) for datagram.
		 */
		TAILQ_FOREACH(inp, &udbtable.inpt_queue, inp_queue) {
			if (inp->inp_af != AF_INET6)
				continue;

			if (inp->inp_lport != dport)
				continue;
			if (!IN6_IS_ADDR_UNSPECIFIED(&in6p_laddr(inp))) {
				if (!IN6_ARE_ADDR_EQUAL(&in6p_laddr(inp),
				    dst6))
					continue;
			} else {
				if (IN6_IS_ADDR_V4MAPPED(dst6) &&
				    (inp->inp_flags & IN6P_IPV6_V6ONLY))
					continue;
			}
			if (!IN6_IS_ADDR_UNSPECIFIED(&in6p_faddr(inp))) {
				if (!IN6_ARE_ADDR_EQUAL(&in6p_faddr(inp),
				    &src6) || inp->inp_fport != sport)
					continue;
			} else {
				if (IN6_IS_ADDR_V4MAPPED(&src6) &&
				    (inp->inp_flags & IN6P_IPV6_V6ONLY))
					continue;
			}

			udp6_sendup(m, off, sin6tosa(src), inp->inp_socket);
			rcvcnt++;

			/*
			 * Don't look for additional matches if this one does
			 * not have either the SO_REUSEPORT or SO_REUSEADDR
			 * socket options set.  This heuristic avoids searching
			 * through all pcbs in the common case of a non-shared
			 * port.  It assumes that an application will never
			 * clear these options after setting them.
			 */
			if ((inp->inp_socket->so_options &
			    (SO_REUSEPORT|SO_REUSEADDR)) == 0)
				break;
		}
	} else {
		/*
		 * Locate pcb for datagram.
		 */
		inp = in6pcb_lookup(&udbtable, &src6, sport, dst6,
					     dport, 0, 0);
		if (inp == NULL) {
			UDP_STATINC(UDP_STAT_PCBHASHMISS);
			inp = in6pcb_lookup_bound(&udbtable, dst6, dport, 0);
			if (inp == NULL)
				return rcvcnt;
		}

#ifdef IPSEC
		/* Handle ESP over UDP */
		if (inp->inp_flags & IN6P_ESPINUDP) {
			switch (udp6_espinudp(mp, off)) {
			case -1: /* Error, m was freed */
				rcvcnt = -1;
				goto bad;

			case 1: /* ESP over UDP */
				rcvcnt++;
				goto bad;

			case 0: /* plain UDP */
			default: /* Unexpected */
				/*
				 * Normal UDP processing will take place,
				 * m may have changed.
				 */
				m = *mp;
				break;
			}
		}
#endif

		if (inp->inp_overudp_cb != NULL) {
			int ret;
			ret = inp->inp_overudp_cb(mp, off, inp->inp_socket,
			    sin6tosa(src), inp->inp_overudp_arg);
			switch (ret) {
			case -1: /* Error, m was freed */
				rcvcnt = -1;
				goto bad;

			case 1: /* Foo over UDP */
				KASSERT(*mp == NULL);
				rcvcnt++;
				goto bad;

			case 0: /* plain UDP */
			default: /* Unexpected */
				/*
				 * Normal UDP processing will take place,
				 * m may have changed.
				 */
				break;
			}
		}

		udp6_sendup(m, off, sin6tosa(src), inp->inp_socket);
		rcvcnt++;
	}

bad:
	return rcvcnt;
}

int
udp6_input_checksum(struct mbuf *m, const struct udphdr *uh, int off, int len)
{

	/*
	 * XXX it's better to record and check if this mbuf is
	 * already checked.
	 */

	if (__predict_false((m->m_flags & M_LOOP) && !udp_do_loopback_cksum)) {
		goto good;
	}
	if (uh->uh_sum == 0) {
		UDP6_STATINC(UDP6_STAT_NOSUM);
		goto bad;
	}

	switch (m->m_pkthdr.csum_flags &
	    ((m_get_rcvif_NOMPSAFE(m)->if_csum_flags_rx & M_CSUM_UDPv6) |
	    M_CSUM_TCP_UDP_BAD | M_CSUM_DATA)) {
	case M_CSUM_UDPv6|M_CSUM_TCP_UDP_BAD:
		UDP_CSUM_COUNTER_INCR(&udp6_hwcsum_bad);
		UDP6_STATINC(UDP6_STAT_BADSUM);
		goto bad;

#if 0 /* notyet */
	case M_CSUM_UDPv6|M_CSUM_DATA:
#endif

	case M_CSUM_UDPv6:
		/* Checksum was okay. */
		UDP_CSUM_COUNTER_INCR(&udp6_hwcsum_ok);
		break;

	default:
		/*
		 * Need to compute it ourselves.  Maybe skip checksum
		 * on loopback interfaces.
		 */
		UDP_CSUM_COUNTER_INCR(&udp6_swcsum);
		if (in6_cksum(m, IPPROTO_UDP, off, len) != 0) {
			UDP6_STATINC(UDP6_STAT_BADSUM);
			goto bad;
		}
	}

good:
	return 0;
bad:
	return -1;
}

int
udp6_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	int off = *offp;
	struct sockaddr_in6 src, dst;
	struct ip6_hdr *ip6;
	struct udphdr *uh;
	u_int32_t plen, ulen;

	ip6 = mtod(m, struct ip6_hdr *);

#if defined(NFAITH) && 0 < NFAITH
	if (faithprefix(&ip6->ip6_dst)) {
		/* send icmp6 host unreach? */
		m_freem(m);
		return IPPROTO_DONE;
	}
#endif

	UDP6_STATINC(UDP6_STAT_IPACKETS);

	/* Check for jumbogram is done in ip6_input. We can trust pkthdr.len. */
	plen = m->m_pkthdr.len - off;
	IP6_EXTHDR_GET(uh, struct udphdr *, m, off, sizeof(struct udphdr));
	if (uh == NULL) {
		IP6_STATINC(IP6_STAT_TOOSHORT);
		return IPPROTO_DONE;
	}

	/*
	 * Enforce alignment requirements that are violated in
	 * some cases, see kern/50766 for details.
	 */
	if (ACCESSIBLE_POINTER(uh, struct udphdr) == 0) {
		m = m_copyup(m, off + sizeof(struct udphdr), 0);
		if (m == NULL) {
			IP6_STATINC(IP6_STAT_TOOSHORT);
			return IPPROTO_DONE;
		}
		ip6 = mtod(m, struct ip6_hdr *);
		uh = (struct udphdr *)(mtod(m, char *) + off);
	}
	KASSERT(ACCESSIBLE_POINTER(uh, struct udphdr));
	ulen = ntohs((u_short)uh->uh_ulen);

	/*
	 * RFC2675 section 4: jumbograms will have 0 in the UDP header field,
	 * iff payload length > 0xffff.
	 */
	if (ulen == 0 && plen > 0xffff)
		ulen = plen;

	if (plen != ulen) {
		UDP6_STATINC(UDP6_STAT_BADLEN);
		goto bad;
	}

	/* destination port of 0 is illegal, based on RFC768. */
	if (uh->uh_dport == 0)
		goto bad;

	/*
	 * Checksum extended UDP header and data.  Maybe skip checksum
	 * on loopback interfaces.
	 */
	if (udp6_input_checksum(m, uh, off, ulen))
		goto bad;

	/*
	 * Construct source and dst sockaddrs.
	 */
	memset(&src, 0, sizeof(src));
	src.sin6_family = AF_INET6;
	src.sin6_len = sizeof(struct sockaddr_in6);
	src.sin6_addr = ip6->ip6_src;
	src.sin6_port = uh->uh_sport;
	memset(&dst, 0, sizeof(dst));
	dst.sin6_family = AF_INET6;
	dst.sin6_len = sizeof(struct sockaddr_in6);
	dst.sin6_addr = ip6->ip6_dst;
	dst.sin6_port = uh->uh_dport;

	if (udp6_realinput(AF_INET6, &src, &dst, &m, off) == 0) {
		if (m->m_flags & M_MCAST) {
			UDP6_STATINC(UDP6_STAT_NOPORTMCAST);
			goto bad;
		}
		UDP6_STATINC(UDP6_STAT_NOPORT);
		icmp6_error(m, ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_NOPORT, 0);
		m = NULL;
	}

bad:
	m_freem(m);
	return IPPROTO_DONE;
}

int
udp6_output(struct inpcb * const inp, struct mbuf *m,
    struct sockaddr_in6 * const addr6, struct mbuf * const control,
    struct lwp * const l)
{
	u_int32_t ulen = m->m_pkthdr.len;
	u_int32_t plen = sizeof(struct udphdr) + ulen;
	struct ip6_hdr *ip6;
	struct udphdr *udp6;
	struct in6_addr _laddr, *laddr, *faddr;
	struct in6_addr laddr_mapped; /* XXX ugly */
	struct sockaddr_in6 *sin6 = NULL;
	struct ifnet *oifp = NULL;
	int scope_ambiguous = 0;
	u_int16_t fport;
	int error = 0;
	struct ip6_pktopts *optp = NULL;
	struct ip6_pktopts opt;
	int af = AF_INET6, hlen = sizeof(struct ip6_hdr);
#ifdef INET
	struct ip *ip;
	struct udpiphdr *ui;
	int flags = 0;
#endif
	struct sockaddr_in6 tmp;

	if (addr6) {
		sin6 = addr6;
		if (sin6->sin6_len != sizeof(*sin6)) {
			error = EINVAL;
			goto release;
		}
		if (sin6->sin6_family != AF_INET6) {
			error = EAFNOSUPPORT;
			goto release;
		}

		/* protect *sin6 from overwrites */
		tmp = *sin6;
		sin6 = &tmp;

		/*
		 * Application should provide a proper zone ID or the use of
		 * default zone IDs should be enabled.  Unfortunately, some
		 * applications do not behave as it should, so we need a
		 * workaround.  Even if an appropriate ID is not determined,
		 * we'll see if we can determine the outgoing interface.  If we
		 * can, determine the zone ID based on the interface below.
		 */
		if (sin6->sin6_scope_id == 0 && !ip6_use_defzone)
			scope_ambiguous = 1;
		if ((error = sa6_embedscope(sin6, ip6_use_defzone)) != 0)
			goto release;
	}

	if (control) {
		if (__predict_false(l == NULL)) {
			panic("%s: control but no lwp", __func__);
		}
		if ((error = ip6_setpktopts(control, &opt,
		    in6p_outputopts(inp), l->l_cred, IPPROTO_UDP)) != 0)
			goto release;
		optp = &opt;
	} else
		optp = in6p_outputopts(inp);


	if (sin6) {
		/*
		 * Slightly different than v4 version in that we call
		 * in6_selectsrc and in6pcb_set_port to fill in the local
		 * address and port rather than inpcb_connect. inpcb_connect
		 * sets inp_faddr which causes EISCONN below to be hit on
		 * subsequent sendto.
		 */
		if (sin6->sin6_port == 0) {
			error = EADDRNOTAVAIL;
			goto release;
		}

		if (!IN6_IS_ADDR_UNSPECIFIED(&in6p_faddr(inp))) {
			/* how about ::ffff:0.0.0.0 case? */
			error = EISCONN;
			goto release;
		}

		faddr = &sin6->sin6_addr;
		fport = sin6->sin6_port; /* allow 0 port */

		if (IN6_IS_ADDR_V4MAPPED(faddr)) {
			if ((inp->inp_flags & IN6P_IPV6_V6ONLY)) {
				/*
				 * I believe we should explicitly discard the
				 * packet when mapped addresses are disabled,
				 * rather than send the packet as an IPv6 one.
				 * If we chose the latter approach, the packet
				 * might be sent out on the wire based on the
				 * default route, the situation which we'd
				 * probably want to avoid.
				 * (20010421 jinmei@kame.net)
				 */
				error = EINVAL;
				goto release;
			}
			if (!IN6_IS_ADDR_UNSPECIFIED(&in6p_laddr(inp)) &&
			    !IN6_IS_ADDR_V4MAPPED(&in6p_laddr(inp))) {
				/*
				 * when remote addr is an IPv4-mapped address,
				 * local addr should not be an IPv6 address,
				 * since you cannot determine how to map IPv6
				 * source address to IPv4.
				 */
				error = EINVAL;
				goto release;
			}

			af = AF_INET;
		}

		if (!IN6_IS_ADDR_V4MAPPED(faddr)) {
			struct psref psref;
			int bound = curlwp_bind();

			error = in6_selectsrc(sin6, optp,
			    in6p_moptions(inp),
			    &inp->inp_route,
			    &in6p_laddr(inp), &oifp, &psref, &_laddr);
			if (error)
				laddr = NULL;
			else
				laddr = &_laddr;
			if (oifp && scope_ambiguous &&
			    (error = in6_setscope(&sin6->sin6_addr,
			    oifp, NULL))) {
				if_put(oifp, &psref);
				curlwp_bindx(bound);
				goto release;
			}
			if_put(oifp, &psref);
			curlwp_bindx(bound);
		} else {
			/*
			 * XXX: freebsd[34] does not have in_selectsrc, but
			 * we can omit the whole part because freebsd4 calls
			 * udp_output() directly in this case, and thus we'll
			 * never see this path.
			 */
			if (IN6_IS_ADDR_UNSPECIFIED(&in6p_laddr(inp))) {
				struct sockaddr_in sin_dst;
				struct in_addr ina;
				struct in_ifaddr *ia4;
				struct psref _psref;
				int bound;

				memcpy(&ina, &faddr->s6_addr[12], sizeof(ina));
				sockaddr_in_init(&sin_dst, &ina, 0);
				bound = curlwp_bind();
				ia4 = in_selectsrc(&sin_dst, &inp->inp_route,
				    inp->inp_socket->so_options, NULL,
				    &error, &_psref);
				if (ia4 == NULL) {
					curlwp_bindx(bound);
					if (error == 0)
						error = EADDRNOTAVAIL;
					goto release;
				}
				memset(&laddr_mapped, 0, sizeof(laddr_mapped));
				laddr_mapped.s6_addr16[5] = 0xffff; /* ugly */
				memcpy(&laddr_mapped.s6_addr[12],
				      &IA_SIN(ia4)->sin_addr,
				      sizeof(IA_SIN(ia4)->sin_addr));
				ia4_release(ia4, &_psref);
				curlwp_bindx(bound);
				laddr = &laddr_mapped;
			} else
			{
				laddr = &in6p_laddr(inp);	/* XXX */
			}
		}
		if (laddr == NULL) {
			if (error == 0)
				error = EADDRNOTAVAIL;
			goto release;
		}
		if (inp->inp_lport == 0) {
			/*
			 * Craft a sockaddr_in6 for the local endpoint. Use the
			 * "any" as a base, set the address, and recover the
			 * scope.
			 */
			struct sockaddr_in6 lsin6 =
			    *((const struct sockaddr_in6 *)inp->inp_socket->so_proto->pr_domain->dom_sa_any);
			lsin6.sin6_addr = *laddr;
			error = sa6_recoverscope(&lsin6);
			if (error)
				goto release;

			error = in6pcb_set_port(&lsin6, inp, l);

			if (error) {
				in6p_laddr(inp) = in6addr_any;
				goto release;
			}
		}
	} else {
		if (IN6_IS_ADDR_UNSPECIFIED(&in6p_faddr(inp))) {
			error = ENOTCONN;
			goto release;
		}
		if (IN6_IS_ADDR_V4MAPPED(&in6p_faddr(inp))) {
			if ((inp->inp_flags & IN6P_IPV6_V6ONLY))
			{
				/*
				 * XXX: this case would happen when the
				 * application sets the V6ONLY flag after
				 * connecting the foreign address.
				 * Such applications should be fixed,
				 * so we bark here.
				 */
				log(LOG_INFO, "udp6_output: IPV6_V6ONLY "
				    "option was set for a connected socket\n");
				error = EINVAL;
				goto release;
			} else
				af = AF_INET;
		}
		laddr = &in6p_laddr(inp);
		faddr = &in6p_faddr(inp);
		fport = inp->inp_fport;
	}

	if (af == AF_INET)
		hlen = sizeof(struct ip);

	/*
	 * Calculate data length and get a mbuf
	 * for UDP and IP6 headers.
	 */
	M_PREPEND(m, hlen + sizeof(struct udphdr), M_DONTWAIT);
	if (m == NULL) {
		error = ENOBUFS;
		goto release;
	}

	/*
	 * Stuff checksum and output datagram.
	 */
	udp6 = (struct udphdr *)(mtod(m, char *) + hlen);
	udp6->uh_sport = inp->inp_lport; /* lport is always set in the PCB */
	udp6->uh_dport = fport;
	if (plen <= 0xffff)
		udp6->uh_ulen = htons((u_int16_t)plen);
	else
		udp6->uh_ulen = 0;
	udp6->uh_sum = 0;

	switch (af) {
	case AF_INET6:
		ip6 = mtod(m, struct ip6_hdr *);
		ip6->ip6_flow	= in6p_flowinfo(inp) & IPV6_FLOWINFO_MASK;
		ip6->ip6_vfc 	&= ~IPV6_VERSION_MASK;
		ip6->ip6_vfc 	|= IPV6_VERSION;
#if 0		/* ip6_plen will be filled in ip6_output. */
		ip6->ip6_plen	= htons((u_int16_t)plen);
#endif
		ip6->ip6_nxt	= IPPROTO_UDP;
		ip6->ip6_hlim	= in6pcb_selecthlim_rt(inp);
		ip6->ip6_src	= *laddr;
		ip6->ip6_dst	= *faddr;

		udp6->uh_sum = in6_cksum_phdr(laddr, faddr,
		    htonl(plen), htonl(IPPROTO_UDP));
		m->m_pkthdr.csum_flags = M_CSUM_UDPv6;
		m->m_pkthdr.csum_data = offsetof(struct udphdr, uh_sum);

		UDP6_STATINC(UDP6_STAT_OPACKETS);
		error = ip6_output(m, optp, &inp->inp_route, 0,
		    in6p_moptions(inp), inp, NULL);
		break;
	case AF_INET:
#ifdef INET
		/* can't transmit jumbogram over IPv4 */
		if (plen > 0xffff) {
			error = EMSGSIZE;
			goto release;
		}

		ip = mtod(m, struct ip *);
		ui = (struct udpiphdr *)ip;
		memset(ui->ui_x1, 0, sizeof(ui->ui_x1));
		ui->ui_pr = IPPROTO_UDP;
		ui->ui_len = htons(plen);
		memcpy(&ui->ui_src, &laddr->s6_addr[12], sizeof(ui->ui_src));
		ui->ui_ulen = ui->ui_len;

		flags = (inp->inp_socket->so_options &
			 (SO_DONTROUTE | SO_BROADCAST));
		memcpy(&ui->ui_dst, &faddr->s6_addr[12], sizeof(ui->ui_dst));

		udp6->uh_sum = in_cksum(m, hlen + plen);
		if (udp6->uh_sum == 0)
			udp6->uh_sum = 0xffff;

		ip->ip_len = htons(hlen + plen);
		ip->ip_ttl = in6pcb_selecthlim(inp, NULL); /* XXX */
		ip->ip_tos = 0;	/* XXX */

		UDP_STATINC(UDP_STAT_OPACKETS);
		error = ip_output(m, NULL, &inp->inp_route, flags /* XXX */,
		    inp->inp_moptions, NULL);
		break;
#else
		error = EAFNOSUPPORT;
		goto release;
#endif
	}
	goto releaseopt;

release:
	m_freem(m);

releaseopt:
	if (control) {
		if (optp == &opt)
			ip6_clearpktopts(&opt, -1);
		m_freem(control);
	}
	return (error);
}

static int
udp6_attach(struct socket *so, int proto)
{
	struct inpcb *inp;
	int s, error;

	KASSERT(sotoinpcb(so) == NULL);
	sosetlock(so);

	error = soreserve(so, udp6_sendspace, udp6_recvspace);
	if (error) {
		return error;
	}

	/*
	 * MAPPED_ADDR implementation spec:
	 *  Always attach for IPv6, and only when necessary for IPv4.
	 */
	s = splsoftnet();
	error = inpcb_create(so, &udbtable);
	splx(s);
	if (error) {
		return error;
	}

	inp = sotoinpcb(so);
	in6p_cksum(inp) = -1;	/* just to be sure */

	KASSERT(solocked(so));
	return 0;
}

static void
udp6_detach(struct socket *so)
{
	struct inpcb *inp = sotoinpcb(so);
	int s;

	KASSERT(solocked(so));
	KASSERT(inp != NULL);

	s = splsoftnet();
	inpcb_destroy(inp);
	splx(s);
}

static int
udp6_accept(struct socket *so, struct sockaddr *nam)
{
	KASSERT(solocked(so));

	return EOPNOTSUPP;
}

static int
udp6_bind(struct socket *so, struct sockaddr *nam, struct lwp *l)
{
	struct inpcb *inp = sotoinpcb(so);
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)nam;
	int error = 0;
	int s;

	KASSERT(solocked(so));
	KASSERT(inp != NULL);

	s = splsoftnet();
	error = in6pcb_bind(inp, sin6, l);
	splx(s);
	return error;
}

static int
udp6_listen(struct socket *so, struct lwp *l)
{
	KASSERT(solocked(so));

	return EOPNOTSUPP;
}

static int
udp6_connect(struct socket *so, struct sockaddr *nam, struct lwp *l)
{
	struct inpcb *inp = sotoinpcb(so);
	int error = 0;
	int s;

	KASSERT(solocked(so));
	KASSERT(inp != NULL);

	if (!IN6_IS_ADDR_UNSPECIFIED(&in6p_faddr(inp)))
		return EISCONN;
	s = splsoftnet();
	error = in6pcb_connect(inp, (struct sockaddr_in6 *)nam, l);
	splx(s);
	if (error == 0)
		soisconnected(so);

	return error;
}

static int
udp6_connect2(struct socket *so, struct socket *so2)
{
	KASSERT(solocked(so));

	return EOPNOTSUPP;
}

static int
udp6_disconnect(struct socket *so)
{
	struct inpcb *inp = sotoinpcb(so);
	int s;

	KASSERT(solocked(so));
	KASSERT(inp != NULL);

	if (IN6_IS_ADDR_UNSPECIFIED(&in6p_faddr(inp)))
		return ENOTCONN;

	s = splsoftnet();
	in6pcb_disconnect(inp);
	memset((void *)&in6p_laddr(inp), 0, sizeof(in6p_laddr(inp)));
	splx(s);

	so->so_state &= ~SS_ISCONNECTED;	/* XXX */
	in6pcb_set_state(inp, INP_BOUND);		/* XXX */
	return 0;
}

static int
udp6_shutdown(struct socket *so)
{
	int s;

	s = splsoftnet();
	socantsendmore(so);
	splx(s);

	return 0;
}

static int
udp6_abort(struct socket *so)
{
	int s;

	KASSERT(solocked(so));
	KASSERT(sotoinpcb(so) != NULL);

	s = splsoftnet();
	soisdisconnected(so);
	inpcb_destroy(sotoinpcb(so));
	splx(s);

	return 0;
}

static int
udp6_ioctl(struct socket *so, u_long cmd, void *addr6, struct ifnet *ifp)
{
	/*
	 * MAPPED_ADDR implementation info:
	 *  Mapped addr support for PRU_CONTROL is not necessary.
	 *  Because typical user of PRU_CONTROL is such as ifconfig,
	 *  and they don't associate any addr to their socket.  Then
	 *  socket family is only hint about the PRU_CONTROL'ed address
	 *  family, especially when getting addrs from kernel.
	 *  So AF_INET socket need to be used to control AF_INET addrs,
	 *  and AF_INET6 socket for AF_INET6 addrs.
	 */
	return in6_control(so, cmd, addr6, ifp);
}

static int
udp6_stat(struct socket *so, struct stat *ub)
{
	KASSERT(solocked(so));

	/* stat: don't bother with a blocksize */
	return 0;
}

static int
udp6_peeraddr(struct socket *so, struct sockaddr *nam)
{
	KASSERT(solocked(so));
	KASSERT(sotoinpcb(so) != NULL);
	KASSERT(nam != NULL);

	in6pcb_fetch_peeraddr(sotoinpcb(so), (struct sockaddr_in6 *)nam);
	return 0;
}

static int
udp6_sockaddr(struct socket *so, struct sockaddr *nam)
{
	KASSERT(solocked(so));
	KASSERT(sotoinpcb(so) != NULL);
	KASSERT(nam != NULL);

	in6pcb_fetch_sockaddr(sotoinpcb(so), (struct sockaddr_in6 *)nam);
	return 0;
}

static int
udp6_rcvd(struct socket *so, int flags, struct lwp *l)
{
	KASSERT(solocked(so));

	return EOPNOTSUPP;
}

static int
udp6_recvoob(struct socket *so, struct mbuf *m, int flags)
{
	KASSERT(solocked(so));

	return EOPNOTSUPP;
}

static int
udp6_send(struct socket *so, struct mbuf *m, struct sockaddr *nam,
    struct mbuf *control, struct lwp *l)
{
	struct inpcb *inp = sotoinpcb(so);
	int error = 0;
	int s;

	KASSERT(solocked(so));
	KASSERT(inp != NULL);
	KASSERT(m != NULL);

	s = splsoftnet();
	error = udp6_output(inp, m, (struct sockaddr_in6 *)nam, control, l);
	splx(s);

	return error;
}

static int
udp6_sendoob(struct socket *so, struct mbuf *m, struct mbuf *control)
{
	KASSERT(solocked(so));

	m_freem(m);
	m_freem(control);

	return EOPNOTSUPP;
}

static int
udp6_purgeif(struct socket *so, struct ifnet *ifp)
{

	mutex_enter(softnet_lock);
	in6pcb_purgeif0(&udbtable, ifp);
#ifdef NET_MPSAFE
	mutex_exit(softnet_lock);
#endif
	in6_purgeif(ifp);
#ifdef NET_MPSAFE
	mutex_enter(softnet_lock);
#endif
	in6pcb_purgeif(&udbtable, ifp);
	mutex_exit(softnet_lock);

	return 0;
}

static int
sysctl_net_inet6_udp6_stats(SYSCTLFN_ARGS)
{

	return (NETSTAT_SYSCTL(udp6stat_percpu, UDP6_NSTATS));
}

static void
sysctl_net_inet6_udp6_setup(struct sysctllog **clog)
{

	sysctl_createv(clog, 0, NULL, NULL,
		       CTLFLAG_PERMANENT,
		       CTLTYPE_NODE, "inet6", NULL,
		       NULL, 0, NULL, 0,
		       CTL_NET, PF_INET6, CTL_EOL);
	sysctl_createv(clog, 0, NULL, NULL,
		       CTLFLAG_PERMANENT,
		       CTLTYPE_NODE, "udp6",
		       SYSCTL_DESCR("UDPv6 related settings"),
		       NULL, 0, NULL, 0,
		       CTL_NET, PF_INET6, IPPROTO_UDP, CTL_EOL);

	sysctl_createv(clog, 0, NULL, NULL,
		       CTLFLAG_PERMANENT|CTLFLAG_READWRITE,
		       CTLTYPE_INT, "sendspace",
		       SYSCTL_DESCR("Default UDP send buffer size"),
		       NULL, 0, &udp6_sendspace, 0,
		       CTL_NET, PF_INET6, IPPROTO_UDP, UDP6CTL_SENDSPACE,
		       CTL_EOL);
	sysctl_createv(clog, 0, NULL, NULL,
		       CTLFLAG_PERMANENT|CTLFLAG_READWRITE,
		       CTLTYPE_INT, "recvspace",
		       SYSCTL_DESCR("Default UDP receive buffer size"),
		       NULL, 0, &udp6_recvspace, 0,
		       CTL_NET, PF_INET6, IPPROTO_UDP, UDP6CTL_RECVSPACE,
		       CTL_EOL);
	sysctl_createv(clog, 0, NULL, NULL,
		       CTLFLAG_PERMANENT|CTLFLAG_READWRITE,
		       CTLTYPE_INT, "do_loopback_cksum",
		       SYSCTL_DESCR("Perform UDP checksum on loopback"),
		       NULL, 0, &udp_do_loopback_cksum, 0,
		       CTL_NET, PF_INET6, IPPROTO_UDP, UDP6CTL_LOOPBACKCKSUM,
		       CTL_EOL);
	sysctl_createv(clog, 0, NULL, NULL,
		       CTLFLAG_PERMANENT,
		       CTLTYPE_STRUCT, "pcblist",
		       SYSCTL_DESCR("UDP protocol control block list"),
		       sysctl_inpcblist, 0, &udbtable, 0,
		       CTL_NET, PF_INET6, IPPROTO_UDP, CTL_CREATE,
		       CTL_EOL);
	sysctl_createv(clog, 0, NULL, NULL,
		       CTLFLAG_PERMANENT,
		       CTLTYPE_STRUCT, "stats",
		       SYSCTL_DESCR("UDPv6 statistics"),
		       sysctl_net_inet6_udp6_stats, 0, NULL, 0,
		       CTL_NET, PF_INET6, IPPROTO_UDP, UDP6CTL_STATS,
		       CTL_EOL);
}

void
udp6_statinc(u_int stat)
{

	KASSERT(stat < UDP6_NSTATS);
	UDP6_STATINC(stat);
}

#ifdef IPSEC
/*
 * Returns:
 *     1 if the packet was processed
 *     0 if normal UDP processing should take place
 *    -1 if an error occurred and m was freed
 */
static int
udp6_espinudp(struct mbuf **mp, int off)
{
	const size_t skip = sizeof(struct udphdr);
	size_t len;
	void *data;
	size_t minlen;
	int ip6hdrlen;
	struct ip6_hdr *ip6;
	struct m_tag *tag;
	struct udphdr *udphdr;
	u_int16_t sport, dport;
	struct mbuf *m = *mp;
	uint32_t *marker;

	/*
	 * Collapse the mbuf chain if the first mbuf is too short
	 * The longest case is: UDP + non ESP marker + ESP
	 */
	minlen = off + sizeof(u_int64_t) + sizeof(struct esp);
	if (minlen > m->m_pkthdr.len)
		minlen = m->m_pkthdr.len;

	if (m->m_len < minlen) {
		if ((*mp = m_pullup(m, minlen)) == NULL) {
			return -1;
		}
		m = *mp;
	}

	len = m->m_len - off;
	data = mtod(m, char *) + off;

	/* Ignore keepalive packets */
	if ((len == 1) && (*(unsigned char *)data == 0xff)) {
		m_freem(m);
		*mp = NULL; /* avoid any further processing by caller ... */
		return 1;
	}

	/* Handle Non-ESP marker (32bit). If zero, then IKE. */
	marker = (uint32_t *)data;
	if (len <= sizeof(uint32_t))
		return 0;
	if (marker[0] == 0)
		return 0;

	/*
	 * Get the UDP ports. They are handled in network
	 * order everywhere in IPSEC_NAT_T code.
	 */
	udphdr = (struct udphdr *)((char *)data - skip);
	sport = udphdr->uh_sport;
	dport = udphdr->uh_dport;

	/*
	 * Remove the UDP header (and possibly the non ESP marker)
	 * IPv6 header length is ip6hdrlen
	 * Before:
	 *   <---- off --->
	 *   +-----+------+-----+
	 *   | IP6 |  UDP | ESP |
	 *   +-----+------+-----+
	 *         <-skip->
	 * After:
	 *          +-----+-----+
	 *          | IP6 | ESP |
	 *          +-----+-----+
	 *   <-skip->
	 */
	ip6hdrlen = off - sizeof(struct udphdr);
	memmove(mtod(m, char *) + skip, mtod(m, void *), ip6hdrlen);
	m_adj(m, skip);

	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_plen = htons(ntohs(ip6->ip6_plen) - skip);
	ip6->ip6_nxt = IPPROTO_ESP;

	/*
	 * We have modified the packet - it is now ESP, so we should not
	 * return to UDP processing ...
	 *
	 * Add a PACKET_TAG_IPSEC_NAT_T_PORT tag to remember
	 * the source UDP port. This is required if we want
	 * to select the right SPD for multiple hosts behind
	 * same NAT
	 */
	if ((tag = m_tag_get(PACKET_TAG_IPSEC_NAT_T_PORTS,
	    sizeof(sport) + sizeof(dport), M_DONTWAIT)) == NULL) {
		m_freem(m);
		return -1;
	}
	((u_int16_t *)(tag + 1))[0] = sport;
	((u_int16_t *)(tag + 1))[1] = dport;
	m_tag_prepend(m, tag);

	if (ipsec_used)
		ipsec6_common_input(&m, &ip6hdrlen, IPPROTO_ESP);
	else
		m_freem(m);

	/* We handled it, it shouldn't be handled by UDP */
	*mp = NULL; /* avoid free by caller ... */
	return 1;
}
#endif /* IPSEC */

PR_WRAP_USRREQS(udp6)
#define	udp6_attach	udp6_attach_wrapper
#define	udp6_detach	udp6_detach_wrapper
#define	udp6_accept	udp6_accept_wrapper
#define	udp6_bind	udp6_bind_wrapper
#define	udp6_listen	udp6_listen_wrapper
#define	udp6_connect	udp6_connect_wrapper
#define	udp6_connect2	udp6_connect2_wrapper
#define	udp6_disconnect	udp6_disconnect_wrapper
#define	udp6_shutdown	udp6_shutdown_wrapper
#define	udp6_abort	udp6_abort_wrapper
#define	udp6_ioctl	udp6_ioctl_wrapper
#define	udp6_stat	udp6_stat_wrapper
#define	udp6_peeraddr	udp6_peeraddr_wrapper
#define	udp6_sockaddr	udp6_sockaddr_wrapper
#define	udp6_rcvd	udp6_rcvd_wrapper
#define	udp6_recvoob	udp6_recvoob_wrapper
#define	udp6_send	udp6_send_wrapper
#define	udp6_sendoob	udp6_sendoob_wrapper
#define	udp6_purgeif	udp6_purgeif_wrapper

const struct pr_usrreqs udp6_usrreqs = {
	.pr_attach	= udp6_attach,
	.pr_detach	= udp6_detach,
	.pr_accept	= udp6_accept,
	.pr_bind	= udp6_bind,
	.pr_listen	= udp6_listen,
	.pr_connect	= udp6_connect,
	.pr_connect2	= udp6_connect2,
	.pr_disconnect	= udp6_disconnect,
	.pr_shutdown	= udp6_shutdown,
	.pr_abort	= udp6_abort,
	.pr_ioctl	= udp6_ioctl,
	.pr_stat	= udp6_stat,
	.pr_peeraddr	= udp6_peeraddr,
	.pr_sockaddr	= udp6_sockaddr,
	.pr_rcvd	= udp6_rcvd,
	.pr_recvoob	= udp6_recvoob,
	.pr_send	= udp6_send,
	.pr_sendoob	= udp6_sendoob,
	.pr_purgeif	= udp6_purgeif,
};
