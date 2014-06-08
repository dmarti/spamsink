PROGS = spamsink.so smtpsend

all : ${PROGS}

spamsink.so : spamsink.c
	$(CC) -o $@ -std=gnu99 -fPIC -shared $^

smtpsend : smtpsend.c
	$(CC) -o $@ -std=gnu99 -lbsd -lpthread $^

hooks : .git/hooks/pre-commit

.git/hooks/% : Makefile
	echo "#!/bin/sh" > $@
	echo "make `basename $@`" >> $@
	chmod 755 $@

pre-commit :
	git diff-index --check HEAD

# Remove anything listed in the .gitignore file.
# Remove empty directories because they cannot be versioned.
clean :
	rm -f ${PROGS}
	find . -path ./.git -prune -o -print0 | \
	git check-ignore -z --stdin | xargs -0 rm -f
	find . -depth -mindepth 1 -type d -print0 | \
	xargs -0 rmdir --ignore-fail-on-non-empty

.PHONY : all clean hooks
