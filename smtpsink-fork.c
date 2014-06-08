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

#define TIMEOUT		60
#define SERVER_PORT 25
#define ROOT_PATH	"/var/empty"

int verbose = 0;

void
sigalrm(int signo)
{
	errx(0, "premature exit due to timeout");
}

void
usage(void)
{
	fprintf(stderr, "usage: smtpsink [-v] [-p port]\n");
	exit(1);
}

void
send_line(FILE *fp, char *line)
{	
	alarm(TIMEOUT);
	
	fprintf(fp, "%s\r\n", line);
					
	alarm(0);
	
	if (verbose)
		printf("-> %s\n", line);
}

int
handle_connection(int fd)
{
	int data = 0;
	int terminate = 0;
	char buf[255];
	FILE *fp;
	
	fp = fdopen(fd, "a+");
	
	if (!fp) {
		warnx("unable to open input");
		alarm(TIMEOUT);
		close(fd);
		alarm(0);
		return -1;
	}
		
	send_line(fp, "220 SMTPSINK (all data will be lost)");
	
	do {
		alarm(TIMEOUT);
		if (!fgets(buf, 254, fp))
			terminate = 1;
		alarm(0);
		
		if (!terminate) {
		
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
	
	alarm(TIMEOUT);
	fclose(fp);
	alarm(0);
	
	return 0;
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
	
	signal(SIGCHLD, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGALRM, sigalrm);


	/* For performance reasons, smtpsink should run standalone, but
	   what do I care... */
	   
	if (argc > 1 && !strcmp(argv[1], "-i")) {
		/* I'm running from inetd, handle the request on stdin */
		fclose(stderr);
		handle_connection(0);
		_exit(0);
	}

	if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
		errx(1, "unable to obtain network");
  
	if ((setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void *)&true, sizeof(true))) == -1)
		errx(1, "setsockopt failed");

	server_sockaddr.sin_family = AF_INET;
	server_sockaddr.sin_port = htons(port);
	server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  
	if (bind(s, (struct sockaddr *) &server_sockaddr, sizeof(server_sockaddr)) == -1) 
		errx(1, "unable to bind socket");

	/* Take precaution if we are running as root */
	
	if (!getuid()) {
		struct passwd *passwd;
		
		if ((passwd = getpwnam("nobody")) == NULL)
			errx(1, "can't change to user nobody");
		
		if (chroot(ROOT_PATH))
			errx(1, "can't chroot to %s", ROOT_PATH);
			
		if (chdir("/"))
			errx(1, "can't change to root directory /");
		
		if (setuid(passwd->pw_uid))
			errx(1, "setuid failed");
	}
	
	if (listen(s, 16) == -1)
		errx(1, "deaf");

	if (verbose)
		printf("smtpsink: listen on port %d for incoming connections\n", port);
		
	while (1) {
 		len = sizeof(ec);
   
		if ((fd = accept(s, (void *) &ec, &len)) == -1)
			errx(5, "unable to accept connection");
        
		if (verbose) {
			handle_connection(fd);
		} else if (!fork()) {
				handle_connection(fd);
				_exit(0);
		}
		
		close(fd);
	}
}
