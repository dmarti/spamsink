/*
 * Copyright (c) 2003, 2004 Marc Balmer.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <pwd.h>
#include <err.h>
#include <pthread.h>

#define SERVER_PORT 25
#define MAXLEN		1024

#define ROOT_PATH	"/var/empty"

int verbose;

void
usage(void)
{
	fprintf(stderr, "usage: smtpsink [-v] [-p port]\n");
	exit(1);
}

void
send_line(FILE *fp, char *line)
{	
	fprintf(fp, "%s\r\n", line);
					
	if (verbose)
		printf("-> %s\n", line);
}

void *
handle_connection(void *arg)
{
	int data = 0;
	int terminate = 0;
	char buf[MAXLEN];
	FILE *fp;

	fp = (FILE *) arg;
		
	send_line(fp, "220 SMTPSINK (all data will be lost)");
	
	do {
		if (!fgets(buf, sizeof(buf), fp))
			terminate = 1;
		else {
		
			if (verbose)
				printf("<- %s", buf);
				
			if (data) {
				if (strlen(buf) <= 3 && buf[0] == '.') {
					data = 0;
					send_line(fp, "250 ACCEPTED");
				}
			} else {
				if (!strncasecmp(buf, "QUIT", 4)) {
					send_line(fp, "221 BYE");
					terminate = 1;
				} else if (!(strncasecmp(buf, "RCPT TO", 7))) {
					send_line(fp, "250 RCPT OK");
				} else if (!(strncasecmp(buf, "DATA", 4))) {
					data = 1;
					send_line(fp, "354 SEND DATA");
				} else
					send_line(fp, "250 OK");
			}
		}
	} while (!terminate);
	
	fclose(fp);
	
	return NULL;
}

int
main(int argc, char *argv[])
{
	int fd, s;
	int len;
	volatile int true;
	struct sockaddr_in ec;
	struct sockaddr_in server_sockaddr;
	int port = SERVER_PORT;
	int ch;
	pthread_t tid;
	pthread_attr_t attr;
	FILE *fp;
	
	while ((ch = getopt(argc, argv, "vp:?")) != -1) {
		switch (ch) {
			case 'v':
				verbose = 1;
				break;
			case 'p':
				port = atoi(optarg);
				break;
			default:
				usage();
		}
	}
	
	argc -= optind;
	argv += optind;
	
	signal(SIGPIPE, SIG_IGN);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	
	if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
		errx(1, "unable to obtain network, errno=%d, %s", errno, strerror(errno));
  
	if ((setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void *)&true, sizeof(true))) == -1)
		errx(1, "setsockopt failed, errno=%d, %s", errno, strerror(errno));

	server_sockaddr.sin_family = AF_INET;
	server_sockaddr.sin_port = htons(port);
	server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  
	if (bind(s, (struct sockaddr *) &server_sockaddr, sizeof(server_sockaddr)) == -1) 
		errx(1, "bind to socket failed, errno=%d, %s", errno, strerror(errno));

	/* Take precaution if we are running as root */
	
	if (!getuid()) {
		struct passwd *passwd;
		
		if ((passwd = getpwnam("nobody")) == NULL)
			errx(1, "can't change to user nobody, errno=%d, %s", errno, strerror(errno));
		
		if (chroot(ROOT_PATH))
			errx(1, "can't chroot to %s, errno=%d, %s", ROOT_PATH, errno, strerror(errno));
			
		if (chdir("/"))
			errx(1, "can't change to root directory /, errno=%d, %s", errno, strerror(errno));
		
		if (setuid(passwd->pw_uid))
			errx(1, "setuid failed, errno=%d, %s", errno, strerror(errno));
	}
	
	if (listen(s, 16) == -1)
		errx(1, "deaf, errno=%d, %s", errno, strerror(errno));

	if (verbose)
		printf("smtpsink: listen on port %d for incoming connections\n", port);
		
	while (1) {
 		len = sizeof(ec);
   
		if ((fd = accept(s, (void *) &ec, &len)) == -1)
			errx(5, "unable to accept connection, errno=%d, %s", errno, strerror(errno));
			
		if ((fp = fdopen(fd, "r+")) == NULL)
			errx(5, "unable to open input, errno=%d,%s", errno, strerror(errno));
		if (verbose) {
			handle_connection(fp);
		} else {
			if (pthread_create(&tid, &attr, handle_connection, fp))
				errx(1, "can't create thread, errno=%d, %s", errno, strerror(errno));
		}
	}
}
