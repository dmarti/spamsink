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
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>
#include <err.h>

#define TIMEOUT 60
#define BUFLEN	255

time_t start_time;
time_t end_time;
time_t stop_time;

int verbose;

char *from;
char *to;
char *subject;

int msgs;		/* total number of messages sent */
double mpm, mps, spm;

int nsenders;	/* number of parallel senders */
int nconns;		/* number of connections per sender */
int nmsgs;		/* number of messages per connection */
int nbytes;		/* number of bytes per message */

int nmessages;	/* number of messages to send */
int nseconds;	/* number of seconds to run */

int nterminat;	/* approx. number of terminated child processes */
int totsenders;	/* total number of senders used */

int time_to_quit;

extern int copy(FILE *, FILE *);

void
usage(void)
{
	fprintf(stderr, "usage: smtpsend [options] [host]\n"
		"options:\n"
		"\t-s senders\tNumber of parallel senders\n"
		"\t-c connections\tNumber of connections per sender\n"
		"\t-m messages\tSend n messages per connection\n"
		"\t-b bytes\tMessage size in bytes\n"
		"\noperation modes (exactly one is required):\n"
		"\t-n messages\tSend at least n messages\n"
		"\t-t seconds\tRun for n seconds\n"
		"\nmiscellaneous:\n"
		"\t-p port\t\tPort number to connect to\n"
		"\t-F from_address\tSpecify the senders e-mail address\n"
		"\t-T to_address\tSpecify the recipients e-mail address\n"
		"\t-S subject\tSpecify subject of the message\n"
		"\t-v \t\tBe verbose (give twice to show SMTP traffic)\n");

	exit(1);
}

void
sigalrm(int signo)
{
	errx(1, "exit due to timeout");
}

void
send_line(FILE *fp, char *line)
{
	alarm(TIMEOUT);
	
	fprintf(fp,  "%s\r\n", line);
			
	alarm(0);
	
	if (verbose > 1)
		printf("-> %s\n", line);

}

char
get_response(FILE *fp)
{
	char buf[BUFLEN];
	
	alarm(TIMEOUT);
	buf[0] = 0;
	
	
	if (!fgets(buf, sizeof(buf), fp)) {
		warnx("no response from server");
		return '?';
	}
	alarm(0);

	if (verbose > 1)
		printf("<- %s", buf);
	return buf[0];
}

char
smtp_send(FILE *fp, char *s)
{
	send_line(fp, s);
	return get_response(fp);
}

int
smtp_message(FILE *fp, char *from, char *to, char *subject, long size)
{
	char buf[BUFLEN];
	int verbosity;
	
	strlcpy(buf, "MAIL FROM:<", sizeof(buf));
	strlcat(buf, from, sizeof(buf));
	strlcat(buf, ">", sizeof(buf));
		
	if (smtp_send(fp, buf) != '2') {
		warnx("protocol error, MAIL FROM");
		return -1;
	}
	
	strlcpy(buf, "RCPT TO:<", sizeof(buf));
	strlcat(buf, to, sizeof(buf));
	strlcat(buf, ">", sizeof(buf));
	
	if (smtp_send(fp, buf) != '2') {
		warnx("protocol error, RCPT TO");
		return -1;
	}
	
	strlcpy(buf, "DATA", sizeof(buf));
	
	if (smtp_send(fp, buf) != '3') {
		warnx("protocol error, DATA");
		return -1;
	}
	
	verbosity = verbose;
	verbose = 0;
	
	strlcpy(buf, "To: ", sizeof(buf));
	strlcat(buf, to, sizeof(buf));
	send_line(fp, buf);
	
	strlcpy(buf, "From: ", sizeof(buf));
	strlcat(buf, from, sizeof(buf));
	send_line(fp, buf);
	
	strlcpy(buf, "Subject: ", sizeof(buf));
	strlcat(buf, subject, sizeof(buf));
	send_line(fp, buf);
	send_line(fp, "X-Mailer: smtpsend\r\n");
	
	/* Change the following message generator to something more
	   intelligent some day... */
	
	strlcpy(buf, "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890", sizeof(buf));
	
	while (size > 0) {
		send_line(fp, buf);
		size -= 36;
	}
	
	verbose = verbosity;
	
	if (smtp_send(fp, ".") != '2') {
		warnx("protocol error, after DATA");
		return -1;
	}
	
	return 0;
}

int
smtp_connection(char *host, int port, int nmsg)
{
	int fd;
	FILE *fp;
	struct sockaddr_in server_sockaddr;
	struct hostent *hostent;
	
	if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
		errx(1, "unable to obtain network");
  
	bzero((char *) &server_sockaddr, sizeof(server_sockaddr));
		
	server_sockaddr.sin_family = AF_INET;
	server_sockaddr.sin_port = htons(port);

	if (inet_aton(host, &server_sockaddr.sin_addr) == 0) {
		hostent = gethostbyname(host);
		if (!hostent)
			errx(1, "unknown host: %s", host);

		server_sockaddr.sin_family = hostent->h_addrtype;
		memcpy(&server_sockaddr.sin_addr, hostent->h_addr, hostent->h_length);
	}

	if (connect(fd, (struct sockaddr *) &server_sockaddr, sizeof(server_sockaddr)) == -1)
		errx(1, "unable to connect socket");
	
	if (!(fp = fdopen(fd, "a+")))
		errx(1, "can't open output");
	
	if (fp == NULL) {
		warnx("unable to open connection");
		return -1;
	}
	
	if (get_response(fp) != '2') {
		warnx("error reading connection");
		return -1;
	}

	if (smtp_send(fp, "HELO localhost") != '2') {
		warnx("protocol error in connection setup");
		return -1;
	}
	
	while (nmsg > 0) {
		if (smtp_message(fp, from, to, subject, nbytes)) {
			warnx("error sending message");
			return -1;
		}
		--nmsg;
	}
	smtp_send(fp, "QUIT");
	fclose(fp);
	
	return 0;
}

int
smtp_sender(char *host, int port)
{
	int conn;

	signal(SIGALRM, sigalrm);
	
	for (conn = 0; conn < nconns; conn++)
		smtp_connection(host, port, nmsgs);
		
	return 0;
}

void
sigchld(int signo)
{
	++nterminat;
}

void
sigalrm_quit(int signo)
{
	time_to_quit = 1;
}

int
smtp_benchmark(char *host, int port)
{
	int status;
	int sav_nterminat;
	int senders;
	
	totsenders = senders = 0;
	time_to_quit = 0;
	nterminat = sav_nterminat = 0;
	
	signal(SIGCHLD, sigchld);
	signal(SIGALRM, sigalrm_quit);
	
	if (verbose)
		printf("starting benchmark\n");
		
	time(&start_time);
	
	if (nseconds)
		alarm(nseconds);
	
	while (!time_to_quit) {		
		if (senders < nsenders) {
			switch (fork()) {
				case 0:
					if (smtp_sender(host, port))
						errx(1, "smtp sender failed");
					_exit(0);
				case -1:	/* We fork to fast/to many, sleep for 1/10-th of a second */
					if (verbose)
						printf("forking to fast...\n");
					usleep(100000);
					break;
				default:
					msgs += (nconns * nmsgs);
					if (nmessages && msgs >= nmessages)
						time_to_quit = 1;
					++senders;
					++totsenders;
					break;
			}
		} 
		
		if (sav_nterminat != nterminat) {
			sav_nterminat = nterminat;
			while (waitpid(0, 0, WNOHANG) > 0)
				--senders;
		}
	};

	/* Wait for all children to exit */
	
	if (verbose)
		printf("waiting for all senders to terminate\n");
	
	while (wait(&status) != -1) ;
	
	time(&end_time);
	
	if (verbose)
		printf("ending benchmark\n");
		
	signal(SIGCHLD, SIG_IGN);
	
	return 0;
}

int
main(int argc, char *argv[])
{
	int ch;
	char *host = "127.0.0.1";
	int port = 25;
	long time_elapsed;
	
	from = "smtpsend@localhost";
	to = "smtpsink@localhost";
	subject = "smtpsend";
	nsenders = 1;
	nconns = 1;
	nmsgs = 1;
	nbytes = 1024;
	verbose = 0;
	
	mpm = msgs = 0;
	
	while ((ch = getopt(argc, argv, "s:c:m:b:n:t:p:F:T:S:v?")) != -1) {
		switch (ch) {
			case 's':
				nsenders = atoi(optarg);
				break;
			case 'c':
				nconns = atoi(optarg);
				break;
			case 'm':
				nmsgs = atoi(optarg);
				break;
			case 'b':
				nbytes = atoi(optarg);
				break;
			case 'n':
				nmessages = atoi(optarg);
				break;
			case 't':
				nseconds = atoi(optarg);
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'F':
				from = optarg;
				break;
			case 'T':
				to = optarg;
				break;
			case 'S':
				subject = optarg;
				break;
			case 'v':
				++verbose;
				break;
			default:
				usage();
		}
	}
	
	if ((nmessages == 0 && nseconds == 0) || (nmessages != 0 && nseconds != 0))
		usage();
		
	if (verbose > 1) {
		printf("limiting number of senders to one\n");
		nsenders = 1;
	}
		
	argc -= optind;
	argv += optind;
	
	if (argc == 1)
		host = argv[0];
	
	signal(SIGCHLD, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	
	if (smtp_benchmark(host, port))
		errx(1, "Unable to measure SMTP performance.  May to aggressive or wrong parameters?");
		
	time_elapsed = (long) end_time - start_time;
	mps = (double) msgs / (double) time_elapsed;
	spm = (double) time_elapsed / (double) msgs;
	mpm = (double) msgs / ((double) time_elapsed / 60.0);
	printf("Sent %d messages in %ld seconds using a total of %d senders\n", msgs, time_elapsed, totsenders);
	printf("Sending rate: %8.2f messages/minute, %8.2f messages/second\n", mpm, mps);
	printf("Average delivery time: %8.2f seconds/message\n", spm);
	
	return 0;
}
