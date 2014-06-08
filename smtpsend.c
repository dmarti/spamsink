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
#include <bsd/string.h>
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
#include <pthread.h>

#define TIMEOUT 60
#define BUFLEN	255

time_t start_time;
time_t end_time;
time_t stop_time;

int verbose;

char *host;
int port;

char *from;
char *to;
char *subject;

int msgs;		/* total number of messages sent */
double mpm, mps, spm;

int nsenders;	/* number of parallel senders */
int nmsgs;		/* number of messages per connection */
int nbytes;		/* number of bytes per message */

int nmessages;	/* number of messages to send */
int nseconds;	/* number of seconds to run */

int nterminat;	/* approx. number of terminated child processes */
int totsenders;	/* total number of senders used */

pthread_rwlock_t	ttq_lock;
int time_to_quit;

extern int copy(FILE *, FILE *);

struct senderinf {
	pthread_t tid;
	int senderid;
	int conns;
	int msgs;
};

void
usage(void)
{
	fprintf(stderr, "usage: smtpsend [options] [host]\n"
		"options:\n"
		"\t-s senders\tNumber of parallel senders\n"
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
send_line(FILE *fp, char *line)
{
	fprintf(fp,  "%s\r\n", line);
			
	if (verbose > 1)
		printf("-> %s\n", line);
}

char
get_response(FILE *fp)
{
	char buf[BUFLEN];
	
	buf[0] = '?';
		
	if (!fgets(buf, sizeof(buf), fp))
		warnx("no response from server");
	else if (verbose > 1)
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
smtp_message(FILE *fp)
{
	char buf[BUFLEN];
	int verbosity;
	int size;
	
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
	
	for (size = nbytes; size > 0; size -= 36)
		send_line(fp, buf);
	
	if (smtp_send(fp, ".") != '2') {
		warnx("protocol error, after DATA");
		return -1;
	}
	
	return 0;
}

int
smtp_connection(void)
{
	int fd;
	FILE *fp;
	struct sockaddr_in server_sockaddr;
	struct hostent *hostent;
	int msgs;
	
	if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		warnx("unable to obtain network");
		return 0;
	}
  
	bzero((char *) &server_sockaddr, sizeof(server_sockaddr));
		
	server_sockaddr.sin_family = AF_INET;
	server_sockaddr.sin_port = htons(port);

	if (inet_aton(host, &server_sockaddr.sin_addr) == 0) {
		hostent = gethostbyname(host);
		if (!hostent) {
			warnx("unknown host: %s", host);
			return 0;
		}

		server_sockaddr.sin_family = hostent->h_addrtype;
		memcpy(&server_sockaddr.sin_addr, hostent->h_addr, hostent->h_length);
	}

	if (connect(fd, (struct sockaddr *) &server_sockaddr, sizeof(server_sockaddr)) == -1) {
		warnx("unable to connect socket, errno=%d, %s", errno, strerror(errno));
		return 0;
	}
	
	if (!(fp = fdopen(fd, "a+"))) {
		warnx("can't open output");
		return 0;
	}

	if (fp == NULL) {
		warnx("unable to open connection");
		return 0;
	}
	
	if (get_response(fp) != '2') {
		warnx("error reading connection");
		return 0;
	}

	if (smtp_send(fp, "HELO localhost") != '2') {
		warnx("protocol error in connection setup");
		return 0;
	}
	
	for (msgs = 0; !time_to_quit && msgs < nmsgs; msgs++) {
		if (smtp_message(fp)) {
			warnx("error sending message");
			return 0;
		}
	}
	
	smtp_send(fp, "QUIT");
	fclose(fp);
	
	return msgs;
}


void
sigalrm(int signo)
{
	if (verbose)
		printf("time to quit\n");

	pthread_rwlock_wrlock(&ttq_lock);	
	time_to_quit = 1;
	pthread_rwlock_unlock(&ttq_lock);
}

void *
timer(void *arg)
{
	time_t now;
	
	do {
		time(&now);
		if (now > start_time + nseconds) {
			if (verbose)
				printf("benchmark ends, waiting for senders to terminate\n");
			time_to_quit = 1;
		}
	} while (!time_to_quit);
	
	return NULL;
}


void *
sender(void *arg)
{
	struct senderinf *inf;
	
	inf = (struct senderinf *) arg;
	inf->conns = inf->msgs = 0;
	
	/* printf("sender %d starts\n", inf->senderid); */
	
	while (!time_to_quit) {
		inf->msgs += smtp_connection();
		++inf->conns;
	}
	/* printf("sender %d terminates after %d conns and %d msgs\n", inf->senderid, inf->conns, inf->msgs); */

	return NULL;
}

int
main(int argc, char *argv[])
{
	int ch;
	long time_elapsed;
	pthread_t tid;
	struct senderinf *senderinf;
	
	int i;
	
	host = "127.0.0.1";
	port = 25;
	from = "smtpsend@localhost";
	to = "smtpsink@localhost";
	subject = "smtpsend";
	nsenders = 1;
	nmsgs = 1;
	nbytes = 1024;
	
	while ((ch = getopt(argc, argv, "s:m:b:n:t:p:F:T:S:v?")) != -1) {
		switch (ch) {
			case 's':
				nsenders = atoi(optarg);
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
	signal(SIGALRM, sigalrm);
	
	if ((senderinf = malloc(nsenders * sizeof(senderinf))) == NULL)
		errx(1, "memory allocation error, errno=%d, %s", errno, strerror(errno));
	
	if (!nseconds)
		exit(0);
	
	if (verbose)
		printf("starting benchmark\n");
			
	time(&start_time);
	 
	for (i = 0; i < nsenders; i++) {
		senderinf[i].senderid = i;
		if (pthread_create(&senderinf[i].tid, NULL, sender, &senderinf[i]))
			errx(1, "thread creation failed, errno=%d, %s", errno, strerror(errno));
	}

	if (pthread_create(&tid, NULL, timer, NULL))
		errx(1, "timer thread creation failed, errno=%d, %s", errno, strerror(errno));
		
	for (i = 0; i < nsenders; i++)
		pthread_join(senderinf[i].tid, NULL);	
	pthread_join(tid, NULL);
		
	time(&end_time);
	
	if (verbose) {
		printf("benchmark ended, per sender statistics are as follows:\n\n");
		printf(" sender | connections | messages\n"); 
		printf("--------|-------------|----------\n");
	}
	
	for (i = 0; i < nsenders; i++) {
		if (verbose)
			printf(" %5d  | %10d  | %7d\n", i, senderinf[i].conns, senderinf[i].msgs); 
		msgs += senderinf[i].msgs;
	}
	
	if (verbose)
		printf("\n");
	
	free(senderinf);
	
	time_elapsed = (long) end_time - start_time;
	mps = (double) msgs / (double) time_elapsed;
	spm = (double) time_elapsed / (double) msgs;
	mpm = (double) msgs / ((double) time_elapsed / 60.0);
	printf("Sent %d messages in %ld seconds\n", msgs, time_elapsed);
	printf("Sending rate: %8.2f messages/minute, %8.2f messages/second\n", mpm, mps);
	printf("Average delivery time: %8.2f seconds/message\n", spm);
	
	return 0;
}
