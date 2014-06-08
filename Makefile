.include <bsd.own.mk>

SUBDIR= smtpsend smtpsink

.include <bsd.subdir.mk>

NAME=smtp-benchmark
VERSION=1.0.4

DIST=${NAME}-${VERSION}.tar.gz

PRINT=	a2ps
PFLAGS=	-E -g -C -T 4 --header="${NAME}-${VERSION}"

dist: cleandir
	(cd ..; tar czvf ${DIST} ${NAME}; md5 ${DIST} > ${DIST}.md5)

print: cleandir
	find . -type f -exec ${PRINT} ${PFLAGS} {} \;

