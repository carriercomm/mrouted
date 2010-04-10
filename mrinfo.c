/*
 * This tool requests configuration info from a multicast router
 * and prints the reply (if any).  Invoke it as:
 *
 *	mrinfo router-name-or-address
 *
 * Written Wed Mar 24 1993 by Van Jacobson (adapted from the
 * multicast mapper written by Pavel Curtis).
 *
 * The lawyers insist we include the following UC copyright notice.
 * The mapper from which this is derived contained a Xerox copyright
 * notice which follows the UC one.  Try not to get depressed noting
 * that the legal gibberish is larger than the program.
 *
 * Copyright (c) 1993 Regents of the University of California.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
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
 * ---------------------------------
 * Copyright (c) 1992, 2001 Xerox Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.

 * Neither name of the Xerox, PARC, nor the names of its contributors may be used
 * to endorse or promote products derived from this software 
 * without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE XEROX CORPORATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "defs.h"

#ifndef lint
static char UNUSED rcsid[] = "@(#) $Id: \
mrinfo.c,v 3.8.4.7 1998/03/01 03:05:20 fenner Exp $";
#endif

#include <netdb.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <stdarg.h>

#define DEFAULT_TIMEOUT	4	/* How long to wait before retrying requests */
#define DEFAULT_RETRIES 3	/* How many times to ask each router */

u_int32	our_addr, target_addr = 0;	/* in NET order */
int     debug = 0;
int	nflag = 0;
int     retries = DEFAULT_RETRIES;
int     timeout = DEFAULT_TIMEOUT;
int	target_level = 0;
vifi_t  numvifs;		/* to keep loader happy */
				/* (see COPY_TABLES macro called in kern.c) */

char *	inet_name(u_int32 addr);
void	ask(u_int32 dst);
void	ask2(u_int32 dst);
int	get_number(int *var, int deflt, char ***pargv, int *pargc);
u_int32	host_addr(char *name);
void	usage(void);

char   *
inet_name(addr)
	u_int32  addr;
{
	struct hostent *e;
	struct in_addr in;

	if (addr == 0)
		return "local";

	if (nflag ||
	    (e = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET)) == NULL) {
		in.s_addr = addr;
		return (inet_ntoa(in));
	}
	return (e->h_name);
}

/*
 * Log errors and other messages to stderr, according to the severity of the
 * message and the current debug level.  For errors of severity LOG_ERR or
 * worse, terminate the program.
 */
void
logit(int severity, int syserr, const char *format, ...)
{
	va_list ap;
	char    fmt[100];

	switch (debug) {
	case 0:
		if (severity > LOG_WARNING)
			return;
	case 1:
		if (severity > LOG_NOTICE)
			return;
	case 2:
		if (severity > LOG_INFO)
			return;
	default:
		fmt[0] = '\0';
		if (severity == LOG_WARNING)
			strcat(fmt, "warning - ");
		strncat(fmt, format, 80);
		va_start(ap, format);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
		if (syserr == 0)
			fprintf(stderr, "\n");
		else
			fprintf(stderr, ": %s\n", strerror(syserr));
	}

	if (severity <= LOG_ERR)
		exit(1);
}

/*
 * Send a neighbors-list request.
 */
void 
ask(dst)
	u_int32  dst;
{
	send_igmp(our_addr, dst, IGMP_DVMRP, DVMRP_ASK_NEIGHBORS,
			htonl(MROUTED_LEVEL), 0);
}

void 
ask2(dst)
	u_int32  dst;
{
	send_igmp(our_addr, dst, IGMP_DVMRP, DVMRP_ASK_NEIGHBORS2,
			htonl(MROUTED_LEVEL), 0);
}

/*
 * Process an incoming neighbor-list message.
 */
void 
accept_neighbors(src, dst, p, datalen, level)
	u_int32	src, UNUSED dst, UNUSED level;
	u_char	*p;
	int     datalen;
{
	u_char *ep = p + datalen;
#define GET_ADDR(a) (a = ((u_int32)*p++ << 24), a += ((u_int32)*p++ << 16),\
		     a += ((u_int32)*p++ << 8), a += *p++)

	printf("%s (%s):\n", inet_fmt(src, s1), inet_name(src));
	while (p < ep) {
		register u_int32 laddr;
		register u_char metric;
		register u_char thresh;
		register int ncount;

		GET_ADDR(laddr);
		laddr = htonl(laddr);
		metric = *p++;
		thresh = *p++;
		ncount = *p++;
		while (--ncount >= 0) {
			register u_int32 neighbor;
			GET_ADDR(neighbor);
			neighbor = htonl(neighbor);
			printf("  %s -> ", inet_fmt(laddr, s1));
			printf("%s (%s) [%d/%d]\n", inet_fmt(neighbor, s1),
			       inet_name(neighbor), metric, thresh);
		}
	}
}

void 
accept_neighbors2(src, dst, p, datalen, level)
	u_int32	src, UNUSED dst, level;
	u_char	*p;
	int     datalen;
{
	u_char *ep = p + datalen;
	u_int broken_cisco = ((level & 0xffff) == 0x020a); /* 10.2 */
	/* well, only possibly_broken_cisco, but that's too long to type. */
	u_int majvers = level & 0xff;
	u_int minvers = (level >> 8) & 0xff;

	printf("%s (%s) [", inet_fmt(src, s1), inet_name(src));
	if (majvers == 3 && minvers == 0xff)
		printf("DVMRPv3 compliant");
	else
		printf("version %d.%d", majvers, minvers);
	printf ("]:\n");
	
	while (p < ep) {
		register u_char metric;
		register u_char thresh;
		register u_char flags;
		register int ncount;
		register u_int32 laddr = *(u_int32*)p;

		p += 4;
		metric = *p++;
		thresh = *p++;
		flags = *p++;
		ncount = *p++;
		if (broken_cisco && ncount == 0)	/* dumb Ciscos */
			ncount = 1;
		if (broken_cisco && ncount > 15)	/* dumb Ciscos */
			ncount = ncount & 0xf;
		while (--ncount >= 0 && p < ep) {
			register u_int32 neighbor = *(u_int32*)p;
			p += 4;
			printf("  %s -> ", inet_fmt(laddr, s1));
			printf("%s (%s) [%d/%d", inet_fmt(neighbor, s1),
			       inet_name(neighbor), metric, thresh);
			if (flags & DVMRP_NF_TUNNEL)
				printf("/tunnel");
			if (flags & DVMRP_NF_SRCRT)
				printf("/srcrt");
			if (flags & DVMRP_NF_PIM)
				printf("/pim");
			if (flags & DVMRP_NF_QUERIER)
				printf("/querier");
			if (flags & DVMRP_NF_DISABLED)
				printf("/disabled");
			if (flags & DVMRP_NF_DOWN)
				printf("/down");
			if (flags & DVMRP_NF_LEAF)
				printf("/leaf");
			printf("]\n");
		}
	}
}

int 
get_number(var, deflt, pargv, pargc)
	int    *var, *pargc, deflt;
	char ***pargv;
{
	if ((*pargv)[0][2] == '\0') {	/* Get the value from the next
					 * argument */
		if (*pargc > 1 && isdigit((*pargv)[1][0])) {
			(*pargv)++, (*pargc)--;
			*var = atoi((*pargv)[0]);
			return 1;
		} else if (deflt >= 0) {
			*var = deflt;
			return 1;
		} else
			return 0;
	} else {		/* Get value from the rest of this argument */
		if (isdigit((*pargv)[0][2])) {
			*var = atoi((*pargv)[0] + 2);
			return 1;
		} else {
			return 0;
		}
	}
}

void
usage()
{
	fprintf(stderr,
	    "Usage: mrinfo [-n] [-t timeout] [-r retries] [router]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int tries;
	int trynew;
	struct timeval et;
	struct hostent *hp;
	struct hostent bogus;
	char *host;
	int curaddr;

	if (geteuid() != 0) {
		fprintf(stderr, "mrinfo: must be root\n");
		exit(1);
	}

	init_igmp();
	seteuid(getuid());

	setlinebuf(stderr);

	argv++, argc--;
	while (argc > 0 && argv[0][0] == '-') {
		switch (argv[0][1]) {
		case 'd':
			if (!get_number(&debug, DEFAULT_DEBUG, &argv, &argc))
				usage();
			break;
		case 'n':
			++nflag;
			break;
		case 'r':
			if (!get_number(&retries, -1, &argv, &argc))
				usage();
			break;
		case 't':
			if (!get_number(&timeout, -1, &argv, &argc))
				usage();
			break;
		default:
			usage();
		}
		argv++, argc--;
	}
	if (argc > 1)
		usage();
	if (argc == 1)
		host = argv[0];
	else
		host = "127.0.0.1";

	if ((target_addr = inet_addr(host)) != INADDR_NONE) {
		hp = &bogus;
		hp->h_length = sizeof(target_addr);
		if (!(hp->h_addr_list = (char **)malloc(2 * sizeof(char *))))
			err(1, "Not enough memory");
		if (!(hp->h_addr_list[0] = malloc(hp->h_length)))
			err(1, "Not enough memory");
		memcpy(hp->h_addr_list[0], &target_addr, hp->h_length);
		hp->h_addr_list[1] = 0;
	} else
		hp = gethostbyname(host);

	if (hp == NULL) {
		fprintf(stderr, "mrinfo: %s: no such host\n", argv[0]);
		exit(1);
	}
	if (debug)
		fprintf(stderr, "Debug level %u\n", debug);

	/* Check all addresses; mrouters often have unreachable interfaces */
	for (curaddr = 0; hp->h_addr_list[curaddr] != NULL; curaddr++) {
	    memcpy(&target_addr, hp->h_addr_list[curaddr], hp->h_length);
	    {			/* Find a good local address for us. */
		int     udp;
		struct sockaddr_in addr;
		socklen_t addrlen = sizeof(addr);

		memset(&addr, 0, sizeof addr);
		addr.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
		addr.sin_len = sizeof addr;
#endif
		addr.sin_addr.s_addr = target_addr;
		addr.sin_port = htons(2000);	/* any port over 1024 will
						 * do... */
		if ((udp = socket(AF_INET, SOCK_DGRAM, 0)) < 0
		|| connect(udp, (struct sockaddr *) & addr, sizeof(addr)) < 0
		    || getsockname(udp, (struct sockaddr *) & addr, &addrlen) < 0) {
			perror("Determining local address");
			exit(1);
		}
		close(udp);
		our_addr = addr.sin_addr.s_addr;
	    }

	    tries = 0;
	    trynew = 1;
	    /*
	     * New strategy: send 'ask2' for two timeouts, then fall back
	     * to 'ask', since it's not very likely that we are going to
	     * find someone who only responds to 'ask' these days
	     */
	    ask2(target_addr);

	    gettimeofday(&et, 0);
	    et.tv_sec += timeout;

	    /* Main receive loop */
	    for (;;) {
		fd_set  fds;
		struct timeval tv, now;
		int     count;
                ssize_t recvlen;
                socklen_t dummy = 0;
		register u_int32 src, dst, group;
		struct ip *ip;
		struct igmp *igmp;
		int     ipdatalen, iphdrlen, igmpdatalen;

		FD_ZERO(&fds);
		FD_SET(igmp_socket, &fds);

		gettimeofday(&now, 0);
		tv.tv_sec = et.tv_sec - now.tv_sec;
		tv.tv_usec = et.tv_usec - now.tv_usec;

		if (tv.tv_usec < 0) {
			tv.tv_usec += 1000000L;
			--tv.tv_sec;
		}
		if (tv.tv_sec < 0)
			tv.tv_sec = tv.tv_usec = 0;

		count = select(igmp_socket + 1, &fds, 0, 0, &tv);

		if (count < 0) {
			if (errno != EINTR)
				perror("select");
			continue;
		} else if (count == 0) {
			logit(LOG_DEBUG, 0, "Timed out receiving neighbor lists");
			if (++tries > retries)
				break;
			/* If we've tried ASK_NEIGHBORS2 twice with
			 * no response, fall back to ASK_NEIGHBORS
			 */
			if (tries == 2 && target_level == 0)
				trynew = 0;
			if (target_level == 0 && trynew == 0)
				ask(target_addr);
			else
				ask2(target_addr);
			gettimeofday(&et, 0);
			et.tv_sec += timeout;
			continue;
		}
		recvlen = recvfrom(igmp_socket, recv_buf, RECV_BUF_SIZE,
				   0, NULL, &dummy);
		if (recvlen <= 0) {
			if (recvlen && errno != EINTR)
				perror("recvfrom");
			continue;
		}

		if (recvlen < (ssize_t)sizeof(struct ip)) {
			logit(LOG_WARNING, 0,
			    "packet too short (%u bytes) for IP header",
			    recvlen);
			continue;
		}
		ip = (struct ip *) recv_buf;
		if (ip->ip_p == 0)
			continue;	/* Request to install cache entry */
		src = ip->ip_src.s_addr;
		dst = ip->ip_dst.s_addr;
		iphdrlen = ip->ip_hl << 2;
		ipdatalen = ntohs(ip->ip_len) - iphdrlen;
		if (iphdrlen + ipdatalen != recvlen) {
		    logit(LOG_WARNING, 0,
		      "packet shorter (%u bytes) than hdr+data length (%u+%u)",
		      recvlen, iphdrlen, ipdatalen);
		    continue;
		}
		igmp = (struct igmp *) (recv_buf + iphdrlen);
		group = igmp->igmp_group.s_addr;
		igmpdatalen = ipdatalen - IGMP_MINLEN;
		if (igmpdatalen < 0) {
		    logit(LOG_WARNING, 0,
			"IP data field too short (%u bytes) for IGMP, from %s",
			ipdatalen, inet_fmt(src, s1));
		    continue;
		}
		if (igmp->igmp_type != IGMP_DVMRP)
			continue;

		switch (igmp->igmp_code) {
		case DVMRP_NEIGHBORS:
		case DVMRP_NEIGHBORS2:
			if (src != target_addr) {
				fprintf(stderr, "mrinfo: got reply from %s",
					inet_fmt(src, s1));
				fprintf(stderr, " instead of %s\n",
					inet_fmt(target_addr, s1));
				/*continue;*/
			}
			break;
		default:
			continue;	/* ignore all other DVMRP messages */
		}

		switch (igmp->igmp_code) {

		case DVMRP_NEIGHBORS:
			if (group) {
				/* knows about DVMRP_NEIGHBORS2 msg */
				if (target_level == 0) {
					target_level = ntohl(group);
					ask2(target_addr);
				}
			} else {
				accept_neighbors(src, dst, (u_char *)(igmp + 1),
						 igmpdatalen, ntohl(group));
				exit(0);
			}
			break;

		case DVMRP_NEIGHBORS2:
			accept_neighbors2(src, dst, (u_char *)(igmp + 1),
					  igmpdatalen, ntohl(group));
			exit(0);
		}
	    }
	}
	exit(1);
}

/* dummies */
void accept_probe(src, dst, p, datalen, level)
	u_int32 UNUSED src, UNUSED dst, UNUSED level;
	char UNUSED *p;
	int UNUSED datalen;
{
}
void accept_group_report(src, dst, group, r_type)
	u_int32 UNUSED src, UNUSED dst, UNUSED group;
	int UNUSED r_type;
{
}
void accept_neighbor_request2(src, dst)
	u_int32 UNUSED src, UNUSED dst;
{
}
void accept_report(src, dst, p, datalen, level)
	u_int32 UNUSED src, UNUSED dst, UNUSED level;
	char UNUSED *p;
	int UNUSED datalen;
{
}
void accept_neighbor_request(src, dst)
	u_int32 UNUSED src, UNUSED dst;
{
}
void accept_prune(src, dst, p, datalen)
	u_int32 UNUSED src, UNUSED dst;
	char UNUSED *p;
	int UNUSED datalen;
{
}
void accept_graft(src, dst, p, datalen)
	u_int32 UNUSED src, UNUSED dst;
	char UNUSED *p;
	int UNUSED datalen;
{
}
void accept_g_ack(src, dst, p, datalen)
	u_int32 UNUSED src, UNUSED dst;
	char UNUSED *p;
	int UNUSED datalen;
{
}
void add_table_entry(origin, mcastgrp)
	u_int32 UNUSED origin, UNUSED mcastgrp;
{
}
void check_vif_state()
{
}
void accept_leave_message(src, dst, group)
	u_int32 UNUSED src, UNUSED dst, UNUSED group;
{
}
void accept_mtrace(src, dst, group, data, no, datalen)
	u_int32 UNUSED src, UNUSED dst, UNUSED group;
	char UNUSED *data;
	u_int8_t UNUSED no;
	size_t UNUSED datalen;
{
}
void accept_membership_query(src, dst, group, tmo)
	u_int32 UNUSED src, UNUSED dst, UNUSED group;
	int UNUSED tmo;
{
}
void accept_info_request(src, dst, p, datalen)
	u_int32 UNUSED src, UNUSED dst;
	u_char UNUSED *p;
	int UNUSED datalen;
{
}
void accept_info_reply(src, dst, p, datalen)
	u_int32 UNUSED src, UNUSED dst;
	u_char UNUSED *p;
	int UNUSED datalen;
{
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "bsd"
 * End:
 */
