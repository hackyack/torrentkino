/*
Copyright 2014 Aiko Barz

This file is part of torrentkino.

torrentkino is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

torrentkino is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with torrentkino.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#include "../shr/config.h"
#include "../p2p/conf.h"
#include "../shr/log.h"
#include "resolver.h"
#include "../p2p/p2p.h"
#include "../p2p/send_udp.h"
#include "../p2p/bucket.h"
#include "../p2p/cache.h"
#include "../p2p/value.h"

void r_parse(UCHAR * buffer, size_t bufsize, IP * from)
{
	DNS_MSG msg;
	const char *hostname = NULL;
	int result = 0;

	/* 12 bytes DNS header to start with */
	if (bufsize < 12) {
		info(_log, from, "DNS: Too few bytes to even start from");
		return;
	}

	/* 512 bytes is enough for everybody */
	if (bufsize > 512) {
		info(_log, from, "DNS: Packet greater than 512 bytes from");
		return;
	}

	/* Ignore link-local address */
	if (ip_is_linklocal(from)) {
		info(_log, from, "DNS: Drop LINK-LOCAL message from");
		return;
	}

	result = p_decode_query(&msg, buffer, bufsize);
	switch (result) {
	case 1:
		/* Success */
		break;
	case 0:
		info(_log, from, "DNS Decoder: Decoding header failed");
		return;
	case -1:
		info(_log, from, "DNS Decoder: Received answer");
		return;
	case -2:
		info(_log, from, "DNS Decoder: Received authority");
		return;
	case -3:
		info(_log, from, "DNS Decoder: Received multiple questions");
		return;
	case -4:
		info(_log, from, "DNS Decoder: Decoding domain failed");
		return;
	case -5:
		info(_log, from, "DNS Decoder: Broken size");
		return;
	default:
		info(_log, from, "DNS Decoder: Something wicked happened");
		return;
	}

	hostname = msg.question.qName;

	/* FIXME? */
	if (hostname == NULL) {
		info(_log, from, "DNS: Missing hostname");
		return;
	}

	if (strlen(hostname) > 255) {
		info(_log, from, "DNS: Hostname too long from");
		return;
	}

	/* Validate hostname */
	if (!str_valid_hostname(hostname, strlen(hostname))) {
		info(_log, from, "DNS: Invalid hostname for lookup: '%s'",
		     hostname);
		return;
	}

	switch (msg.question.qType) {
#ifdef IPV6
	case AAAA_Resource_RecordType:
		r_lookup((char *)hostname, from, &msg);
		break;
#elif IPV4
	case A_Resource_RecordType:
		r_lookup((char *)hostname, from, &msg);
		break;
#endif
	case SRV_Resource_RecordType:
		r_lookup((char *)hostname, from, &msg);
		break;
	default:
		r_failure(from, &msg);
	}
}

void r_lookup(char *hostname, IP * from, DNS_MSG * msg)
{
	UCHAR target[SHA1_SIZE];
	int result = FALSE;

	/* Compute lookup key */
	id_hostid(target, hostname,
		  _main->conf->realm, _main->conf->bool_realm);

	/* Check local cache */
	result = r_lookup_cache_db(target, from, msg);
	if (result == TRUE) {
		info(_log, from, "LOOKUP %s (cached)", hostname);
		return;
	}

	/* Check local database */
	result = r_lookup_local_db(target, from, msg);
	if (result == TRUE) {
		info(_log, from, "LOOKUP %s (local)", hostname);
		return;
	}

	/* Start remote search */
	r_lookup_remote(target, P2P_GET_PEERS, from, msg);
	info(_log, from, "LOOKUP %s (remote)", hostname);
}

int r_lookup_cache_db(UCHAR * target, IP * from, DNS_MSG * msg)
{
	UCHAR nodes_compact_list[IP_SIZE_META_PAIR8];
	int nodes_compact_size = 0;

	/* Check cache for hostname */
	nodes_compact_size = cache_compact_list(nodes_compact_list, target);
	if (nodes_compact_size <= 0) {
		return FALSE;
	}

	r_success(from, msg, nodes_compact_list, nodes_compact_size);

	return TRUE;
}

/* Use local info_hash database for lookups too. This is nessecary if only 2
 * nodes are active: Node A announces its name to node B. But Node B cannot
 * talk to itself to lookup A. So, it must use its database directly. */
int r_lookup_local_db(UCHAR * target, IP * from, DNS_MSG * msg)
{
	UCHAR nodes_compact_list[IP_SIZE_META_PAIR8];
	int nodes_compact_size = 0;

	/* Check cache for hostname */
	nodes_compact_size = val_compact_list(nodes_compact_list, target);
	if (nodes_compact_size <= 0) {
		return FALSE;
	}

	r_success(from, msg, nodes_compact_list, nodes_compact_size);

	return TRUE;
}

void r_lookup_remote(UCHAR * target, int type, IP * from, DNS_MSG * msg)
{
	UCHAR nodes_compact_list[IP_SIZE_META_TRIPLE8];
	int nodes_compact_size = 0;
	LOOKUP *l = NULL;
	UCHAR *p = NULL;
	UCHAR *id = NULL;
	ITEM *ti = NULL;
	int j = 0;
	IP sin;

	/* Start the incremental remote search program */
	nodes_compact_size = bckt_compact_list(_main->nbhd->bucket,
					       nodes_compact_list, target);

	/* Create tid and get the lookup table */
	ti = tdb_put(type);
	l = ldb_init(target, from, msg);
	tdb_link_ldb(ti, l);

	p = nodes_compact_list;
	for (j = 0; j < nodes_compact_size; j += IP_SIZE_META_TRIPLE) {

		/* ID */
		id = p;
		p += SHA1_SIZE;

		/* IP + Port */
		p = ip_tuple_to_sin(&sin, p);

		/* Remember queried node */
		ldb_put(l, id, &sin);

		/* Query node */
		send_get_peers_request(&sin, target, tdb_tid(ti));
	}
}

void r_success(IP * from, DNS_MSG * msg, UCHAR * nodes_compact_list,
	       int nodes_compact_size)
{
	UCHAR buffer[UDP_BUF];
	UCHAR *p = NULL;
	int buflen = 0;

	if (nodes_compact_size == 0) {
		return;
	}

	p_reply_msg(msg, nodes_compact_list, nodes_compact_size);
	p = p_encode_response(msg, buffer);

	buflen = p - buffer;
	info(_log, from, "Send %d bytes DNS packet to", buflen);

	sendto(_main->dns->sockfd, buffer, buflen, 0,
	       (struct sockaddr *)from, sizeof(IP));
}

/* Send empty reply */
void r_failure(IP * from, DNS_MSG * msg)
{
	UCHAR buffer[UDP_BUF];
	UCHAR *p = buffer;
	int buflen = 0;

	p_reset_msg(msg);
	p = p_encode_response(msg, buffer);

	buflen = p - buffer;
	info(_log, from, "Send %d bytes DNS packet to", buflen);

	sendto(_main->dns->sockfd, buffer, buflen, 0,
	       (struct sockaddr *)from, sizeof(IP));
}
