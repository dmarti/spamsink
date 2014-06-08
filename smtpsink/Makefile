PROG=	smtpsink
SRCS=	smtpsink.c

PIPE=	-pipe
    
CFLAGS+= -O2 -Wall
LDFLAGS+= -pthread

YFLAGS=
MAN=	smtpsink.8
MANDIR = /usr/local/man/cat
BINDIR = /usr/local/sbin
BINOWN  =       root
BINMODE =       0555

.include <bsd.prog.mk>
