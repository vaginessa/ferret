/* Copyright (c) 2007 by Errata Security, All Rights Reserved
 * Programer(s): Robert David Graham [rdg]
 */
#include "stack-parser.h"
#include "stack-netframe.h"
#include "ferret.h"
#include "stack-extract.h"
#include "util-mystring.h"
#include <string.h>

#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

/**
 * Looks for a pattern within the payload.
 *
 * TODO: we need to swap this out for the generic pattern-search feature.
 */
static unsigned
udp_contains_sz(const unsigned char *px, unsigned length, const char *sz)
{
	unsigned sz_length = (unsigned)strlen(sz);
	unsigned offset=0;

	if (length < sz_length)
		return 0;
	length -= sz_length;

	while (offset<length) {
		if (px[offset] == sz[0] && memcmp(px+offset, sz, sz_length) == 0)
			return 1;
		offset++;
	}

	return 0;
}

enum {
    SMELLS_SRC,
    SMELLS_DST,
};

static unsigned
smellslike_udp_ntp(struct Ferret *ferret, struct NetFrame *frame, const unsigned char *px, unsigned length, unsigned direction)
{
    UNUSEDPARM(ferret);
    UNUSEDPARM(frame);
    UNUSEDPARM(px);
    UNUSEDPARM(length);
    UNUSEDPARM(direction);
    return 1;
}

static unsigned
smellslike_udp_tivoconnect(struct Ferret *ferret, struct NetFrame *frame, const unsigned char *px, unsigned length, unsigned direction)
{
    UNUSEDPARM(ferret);
    UNUSEDPARM(frame);
    UNUSEDPARM(direction);

	if (length> 12) {
		if (MATCHES("tivoconnect=",px, 12)) {
            return true;
		}
	}
    return false;
}

static unsigned
smellslike_udp_bootp(struct Ferret *ferret, struct NetFrame *frame, const unsigned char *px, unsigned length, unsigned direction)
{
    unsigned i;

    UNUSEDPARM(ferret);
    UNUSEDPARM(frame);
    UNUSEDPARM(direction);

    /* BOOTP packets must have at least 300 bytes */
    if (length < 300)
        return false;

    /* The first few bytes must have small values */
    for (i=0; i<4; i++) {
        if (px[i] > 30)
            return false;
    }

    /* The elapsed time must be less than 5 minutes */
    if (!(ex32le(px+8) < 900 || ex16le(px+8) < 900))
        return false;
    return true;
}

static unsigned
smellslike_udp_dhcp(struct Ferret *ferret, struct NetFrame *frame, const unsigned char *px, unsigned length, unsigned direction)
{
    UNUSEDPARM(ferret);
    UNUSEDPARM(frame);
    UNUSEDPARM(px);
    UNUSEDPARM(length);
    UNUSEDPARM(direction);
}

void process_udp(struct Ferret *ferret, struct NetFrame *frame, const unsigned char *px, unsigned length)
{
	unsigned offset=0;
	struct {
		unsigned src_port;
		unsigned dst_port;
		unsigned length;
		unsigned checksum;
	} udp;

	ferret->statistics.udp++;

	if (length == 0) {
		FRAMERR(frame, "udp: frame empty\n");
		return;
	}
	if (length < 8) {
		FRAMERR(frame, "udp: frame too short\n");
		return;
	}

	udp.src_port = ex16be(px+0);
	udp.dst_port = ex16be(px+2);
	udp.length = ex16be(px+4);
	udp.checksum = ex16be(px+6);

	frame->src_port = udp.src_port;
	frame->dst_port = udp.dst_port;

	if (udp.length < 8) {
		FRAMERR_TRUNCATED(frame, "udp");
		return;
	}

	if (length > udp.length)
		length = udp.length;

	offset += 8;

	switch (frame->dst_ipv4) {
	case 0xe0000123: /* 224.0.1.35 - SLP */
		if (udp.dst_port == 427)
			SAMPLE(ferret,"SLP", JOT_SZ("packet", "test"));
		else
			FRAMERR(frame, "unknown port %d\n", udp.dst_port);
		return;
	}

	SAMPLE(ferret,"UDP", JOT_NUM("src", udp.src_port));
	SAMPLE(ferret,"UDP", JOT_NUM("dst", udp.dst_port));



    /*********************
     *  SSS   RRR    CCC
     * S      R  R  C
     *  SSS   RRR   C
     *     S  R R   C
     *  SSS   R  R   CCC
     *********************/
    switch (udp.src_port) {
    case 123:
        if (smellslike_udp_ntp(ferret, frame, px+offset, length-offset, SMELLS_SRC))
           	ferret->statistics.udp_.ntp++;
        break;
    case 2190:
        if (smellslike_udp_tivoconnect(ferret, frame, px+offset, length-offset, SMELLS_SRC)) {
           	ferret->statistics.udp_.tivoconnect++;
   			parse_tivo_broadcast(ferret, frame, px+offset, length-offset);
			return;
        }
    }

    /*************************
     * DDD   EEEE  SSS  TTTTT
     * D  D  E    S       T
     * D  D  EEE   SSS    T
     * D  D  E        S   T
     * DDD   EEEE  SSS    T
     *************************/
    switch (udp.dst_port) {
    case 123:
        if (smellslike_udp_ntp(ferret, frame, px+offset, length-offset, SMELLS_DST))
           	ferret->statistics.udp_.ntp++;
        break;
    case 2190:
        if (smellslike_udp_tivoconnect(ferret, frame, px+offset, length-offset, SMELLS_DST)) {
           	ferret->statistics.udp_.tivoconnect++;
   			parse_tivo_broadcast(ferret, frame, px+offset, length-offset);
			return;
        }
    case 38293:
		if (	udp_contains_sz(px+offset, length-offset, "LDVPHiCM")
			||	udp_contains_sz(px+offset, length-offset, "HiCMHiCM")) {
			JOTDOWN(ferret,
				JOT_SRC("ID-IP", frame),
				JOT_SZ("Software", "Norton AntiVirus Corporate Edition"),
				0);
           	ferret->statistics.udp_.norton_av++;
			return;
		}
    }

	switch (udp.src_port) {
	case 68:
	case 67:
		process_dhcp(ferret, frame, px+offset, length-offset);
		break;
	case 53:
		process_dns(ferret, frame, px+offset, length-offset);
		break;
	case 137:
		process_dns(ferret, frame, px+offset, length-offset);
		break;
	case 138:
		process_netbios_dgm(ferret, frame, px+offset, length-offset);
		break;
	case 389:
		process_ldap(ferret, frame, px+offset, length-offset);
		break;
	case 631:
		if (udp.dst_port == 631) {
			process_cups(ferret, frame, px+offset, length-offset);
		}
		break;
	case 1900:
		if (length-offset > 9 && strnicmp((const char*)px+offset, "HTTP/1.1 ", 9) == 0) {
			process_upnp_response(ferret, frame, px+offset, length-offset);
		}
		break;
	case 14906: /* ??? */
		break;
	case 4500:
		break;
	default:
		switch (udp.dst_port) {
		case 0:
			break;
		case 68:
		case 67:
			process_dhcp(ferret, frame, px+offset, length-offset);
			break;
		case 53:
		case 5353:
			process_dns(ferret, frame, px+offset, length-offset);
			break;
		case 137:
			process_dns(ferret, frame, px+offset, length-offset);
			break;
		case 138:
			process_netbios_dgm(ferret, frame, px+offset, length-offset);
			break;
		case 1900:
			if (frame->dst_ipv4 == 0xeffffffa)
				parse_ssdp(ferret, frame, px+offset, length-offset);
			break;
		case 5369:
			break;
		case 29301:
			break;
		case 123:
			break;
		case 5499:
			break;
		case 2233: /*intel/shiva vpn*/
			break;
		case 27900: /* GameSpy*/
			break;
		case 9283:
			process_callwave_iam(ferret, frame, px+offset, length-offset);
			break;
		case 161:
			process_snmp(ferret, frame, px+offset, length-offset);
			break;
		case 192: /* ??? */
			break;
		case 389:
			process_ldap(ferret, frame, px+offset, length-offset);
			break;
		case 427: /* SRVLOC */
			process_srvloc(ferret, frame, px+offset, length-offset);
			break;
		case 14906: /* ??? */
			break;
		case 500:
			process_isakmp(ferret, frame, px+offset, length-offset);
			break;
		case 2222:
			break;
		default:
			if (frame->dst_ipv4 == 0xc0a8a89b || frame->src_ipv4 == 0xc0a8a89b)
				;
			else {
				if (smellslike_bittorrent_udp(px+offset, length-offset))
					;
				else
					; /*
				FRAMERR(frame, "udp: unknown, [%d.%d.%d.%d]->[%d.%d.%d.%d] src=%d, dst=%d\n", 
					(frame->src_ipv4>>24)&0xFF,(frame->src_ipv4>>16)&0xFF,(frame->src_ipv4>>8)&0xFF,(frame->src_ipv4>>0)&0xFF,
					(frame->dst_ipv4>>24)&0xFF,(frame->dst_ipv4>>16)&0xFF,(frame->dst_ipv4>>8)&0xFF,(frame->dst_ipv4>>0)&0xFF,
					frame->src_port, frame->dst_port);*/
			}
		}
	}


}

