/* 
	BlueZ - Bluetooth protocol stack for Linux
	Copyright (C) 2000-2001 Qualcomm Incorporated

	Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License version 2 as
	published by the Free Software Foundation;

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
	OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
	IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY CLAIM,
	OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER
	RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
	NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE
	USE OR PERFORMANCE OF THIS SOFTWARE.

	ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, COPYRIGHTS,
	TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS SOFTWARE IS DISCLAIMED.
*/

/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <resolv.h>
#include <netdb.h>
#include <sys/socket.h>

#include <bluetooth.h>
#include <l2cap.h>

/* Test modes */
enum {
	SEND,
	RECV,
	RECONNECT,
	MULTY,
	DUMP,
	CONNECT
};

unsigned char *buf;

/* Default mtu */
int imtu = 672;
int omtu = 0;

/* Default data size */
long data_size = 672;

/* Default addr and psm */
bdaddr_t bdaddr;
unsigned short psm = 10;

int master = 0;
int auth = 0;
int encrypt = 0;

float tv2fl(struct timeval tv)
{
	return (float)tv.tv_sec + (float)(tv.tv_usec/1000000.0);
}

int do_connect(char *svr)
{
	struct sockaddr_l2 rem_addr, loc_addr;
	struct l2cap_options opts;
	int s, opt;

	if( (s = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP)) < 0 ) {
		syslog(LOG_ERR, "Can't create socket. %s(%d)", strerror(errno), errno);
		return -1;
	}

	memset(&loc_addr, 0, sizeof(loc_addr));
	loc_addr.l2_family = AF_BLUETOOTH;
	loc_addr.l2_bdaddr = bdaddr;
	if( bind(s, (struct sockaddr *) &loc_addr, sizeof(loc_addr)) < 0 ) {
		syslog(LOG_ERR, "Can't bind socket. %s(%d)", strerror(errno), errno);
		exit(1);
	}

	/* Get default options */
	opt = sizeof(opts);
	if( getsockopt(s, SOL_L2CAP, L2CAP_OPTIONS, &opts, &opt) < 0 ) {
		syslog(LOG_ERR, "Can't get default L2CAP options. %s(%d)", strerror(errno), errno);
		return -1;	
	}

	/* Set new options */
	opts.omtu = omtu;
	opts.imtu = imtu;
	if( setsockopt(s, SOL_L2CAP, L2CAP_OPTIONS, &opts, opt) < 0 ) {
		syslog(LOG_ERR, "Can't set L2CAP options. %s(%d)", strerror(errno), errno);
		return -1;
	}

	memset(&rem_addr, 0, sizeof(rem_addr));
	rem_addr.l2_family = AF_BLUETOOTH;
	baswap(&rem_addr.l2_bdaddr, strtoba(svr));
	rem_addr.l2_psm = htobs(psm);
	if( connect(s, (struct sockaddr *)&rem_addr, sizeof(rem_addr)) < 0 ){
		syslog(LOG_ERR, "Can't connect. %s(%d)", strerror(errno), errno);
		return -1;
	}

	opt = sizeof(opts);
	if( getsockopt(s, SOL_L2CAP, L2CAP_OPTIONS, &opts, &opt) < 0 ){
		syslog(LOG_ERR, "Can't get L2CAP options. %s(%d)", strerror(errno), errno);
		return -1;
	}

	syslog(LOG_INFO, "Connected [imtu %d, omtu %d, flush_to %d]\n",
	       opts.imtu, opts.omtu, opts.flush_to);

	return s;
}

void do_listen( void (*handler)(int sk) )
{
	struct sockaddr_l2 loc_addr, rem_addr;
	struct l2cap_options opts;
	int  s, s1, opt;
	bdaddr_t ba;

	if( (s = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP)) < 0 ) {
		syslog(LOG_ERR, "Can't create socket. %s(%d)", strerror(errno), errno);
		exit(1);
	}

	loc_addr.l2_family = AF_BLUETOOTH;
	loc_addr.l2_bdaddr = bdaddr;
	loc_addr.l2_psm    = htobs(psm);
	if( bind(s, (struct sockaddr *) &loc_addr, sizeof(loc_addr)) < 0 ) {
		syslog(LOG_ERR, "Can't bind socket. %s(%d)", strerror(errno), errno);
		exit(1);
	}

	if( listen(s, 10) ) {
		syslog(LOG_ERR,"Can not listen on the socket. %s(%d)", strerror(errno), errno);
		exit(1);
	}

	/* Get default options */
	opt = sizeof(opts);
	if (getsockopt(s, SOL_L2CAP, L2CAP_OPTIONS, &opts, &opt) < 0) {
		syslog(LOG_ERR, "Can't get default L2CAP options. %s(%d)", strerror(errno), errno);
		exit(1);
	}

	/* Set new options */
	opts.imtu = imtu;
	if (setsockopt(s, SOL_L2CAP, L2CAP_OPTIONS, &opts, opt) < 0) {
		syslog(LOG_ERR, "Can't set L2CAP options. %s(%d)", strerror(errno), errno);
		exit(1);
	}


	/* Set link mode */
	opt = 0;
	if (master)
		 opt |= L2CAP_LM_MASTER;

	if (auth)
		 opt |= L2CAP_LM_AUTH;

	if (encrypt)
		 opt |= L2CAP_LM_ENCRYPT;

	if (setsockopt(s, SOL_L2CAP, L2CAP_LM, &opt, sizeof(opt)) < 0) {
		syslog(LOG_ERR, "Can't set L2CAP link mode. %s(%d)", strerror(errno), errno);
		exit(1);
	}

	syslog(LOG_INFO,"Waiting for connection on psm %d ...", psm);

	while(1) {
		opt = sizeof(rem_addr);
		if( (s1 = accept(s, (struct sockaddr *)&rem_addr, &opt)) < 0 ) {
			syslog(LOG_ERR,"Accept failed. %s(%d)", strerror(errno), errno);
			exit(1);
		}
		if( fork() ) {
			/* Parent */
			close(s1);
			continue;
		}
		/* Child */

		close(s);

		opt = sizeof(opts);
		if( getsockopt(s1, SOL_L2CAP, L2CAP_OPTIONS, &opts, &opt) < 0 ) {
			syslog(LOG_ERR, "Can't get L2CAP options. %s(%d)", strerror(errno), errno);
			exit(1);
		}

		baswap(&ba, &rem_addr.l2_bdaddr);
		syslog(LOG_INFO, "Connect from %s [imtu %d, omtu %d, flush_to %d]\n",
		       batostr(&ba), opts.imtu, opts.omtu, opts.flush_to);

		handler(s1);

		syslog(LOG_INFO, "Disconnect\n");
		exit(0);
	}
}

void dump_mode(int s)
{
	int len;

	syslog(LOG_INFO,"Receiving ...");
	while ((len = read(s, buf, data_size)) > 0)
		syslog(LOG_INFO, "Recevied %d bytes\n", len);
}

void recv_mode(int s)
{
	struct timeval tv_beg,tv_end,tv_diff;
	long total;
	uint32_t seq;

	syslog(LOG_INFO,"Receiving ...");

	seq = 0;
	while (1) {
		gettimeofday(&tv_beg,NULL);
		total = 0;
		while (total < data_size) {
			uint32_t sq;
			uint16_t l;
			int i,r;

			if ((r = recv(s, buf, data_size, 0)) <= 0) {
				if (r < 0)
					syslog(LOG_ERR, "Read failed. %s(%d)",
							strerror(errno), errno);
				return;	
			}

			/* Check sequence */
			sq = btohl(*(uint32_t *)buf);
			if (seq != sq) {
				syslog(LOG_INFO, "seq missmatch: %d -> %d", seq, sq);
				seq = sq;
			}
			seq++;
			
			/* Check length */
			l = btohs(*(uint16_t *)(buf+4));
			if (r != l) {
				syslog(LOG_INFO, "size missmatch: %d -> %d", r, l);
				continue;
			}
			
			/* Verify data */	
			for (i=6; i < r; i++) {
				if (buf[i] != 0x7f)
					syslog(LOG_INFO, "data missmatch: byte %d 0x%2.2x", i, buf[i]);
			}

			total += r;
		}
		gettimeofday(&tv_end,NULL);

		timersub(&tv_end,&tv_beg,&tv_diff);

		syslog(LOG_INFO,"%ld bytes in %.2f sec, %.2f kB/s",total,
		       tv2fl(tv_diff), (float)(total / tv2fl(tv_diff) ) / 1024.0);
	}
}

void send_mode(char *svr)
{
	uint32_t seq;
	int s, i;

	if( (s = do_connect(svr)) < 0 )
		exit(1);

	syslog(LOG_INFO,"Sending ...");

	for(i=6; i < data_size; i++)
		buf[i]=0x7f;

	seq = 0;
	while(1){
		*(uint32_t *)buf = htobl(seq++);
		*(uint16_t *)(buf+4) = htobs(data_size);
		
		if( send(s, buf, data_size, 0) <= 0 ) {
			syslog(LOG_ERR, "Send failed. %s(%d)", strerror(errno), errno);
			exit(1);
		}
	}
}

void reconnect_mode(char *svr)
{
	while(1){
		int s;
		if( (s = do_connect(svr)) < 0 )
			exit(1);
		
		close(s);

		usleep(10);
	}
}

void connect_mode(char *svr)
{
	int s;
	if ((s = do_connect(svr)) < 0)
		exit(1);
	sleep(99999999);
}

void multy_connect_mode(char *svr)
{
	while(1){
		int i, s;
		for(i=0; i<10; i++){
			if( fork() ) continue;

			/* Child */
			s = do_connect(svr);
			close(s);
			exit(0);
		}
		sleep(19);
	}
}

void usage(void)
{
	printf("l2test - L2CAP testing\n"
		"Usage:\n");
	printf("\tl2test <mode> [options] [bdaddr]\n");
	printf("Modes:\n"
		"\t-r receive (server)\n"
		"\t-d dump (server)\n"
		"\t-n silent connect (client)\n"
		"\t-c reconnect (client)\n"
		"\t-m multiple connects (client)\n"
		"\t-s send (client)\n");
	printf("Options:\n"
		"\t[-b bytes] [-S bdaddr] [-P psm]\n"
	       	"\t[-I imtu] [-O omtu]\n"
		"\t[-A] request authentication\n"
		"\t[-E] request encryption\n"
	       	"\t[-M] become master\n");
}

extern int optind,opterr,optopt;
extern char *optarg;

int main(int argc ,char *argv[])
{
	struct sigaction sa;
	int opt, mode = RECV;

	while ((opt=getopt(argc,argv,"rdscmnb:P:I:O:S:MAE")) != EOF) {
		switch(opt) {
		case 'r':
			mode = RECV;
			break;
		
		case 's':
			mode = SEND;
			break;

		case 'd':
			mode = DUMP;
			break;

		case 'c':
			mode = RECONNECT;
			break;

		case 'n':
			mode = CONNECT;
			break;

		case 'm':
			mode = MULTY;
			break;

		case 'b':
			data_size = atoi(optarg);
			break;

		case 'S':
			baswap(&bdaddr, strtoba(optarg));
			break;

		case 'P':
			psm = atoi(optarg);
			break;

		case 'I':
			imtu = atoi(optarg);
			break;

		case 'O':
			omtu = atoi(optarg);
			break;

		case 'M':
			master = 1;
			break;

		case 'A':
			auth = 1;
			break;

		case 'E':
			encrypt = 1;
			break;

		default:
			usage();
			exit(1);
		}
	}

	if (!(argc - optind) && (mode!=RECV && mode !=DUMP)) {
		usage();
		exit(1);
	}

	if (!(buf = malloc(data_size))) {
		perror("Can't allocate data buffer");
		exit(1);
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sa.sa_flags   = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, NULL);

	openlog("l2test", LOG_PERROR | LOG_PID, LOG_LOCAL0);

	switch( mode ){
		case RECV:
			do_listen(recv_mode);
			break;

		case DUMP:
			do_listen(dump_mode);
			break;

		case SEND:
			send_mode(argv[optind]);
			break;

		case RECONNECT:
			reconnect_mode(argv[optind]);
			break;

		case MULTY:
			multy_connect_mode(argv[optind]);
			break;

		case CONNECT:
			connect_mode(argv[optind]);
			break;
	}
	syslog(LOG_INFO, "Exit");

	closelog();

	return 0;
}
