srcdir = src
SRC = ${srcdir}/vsm.c ${srcdir}/text.c ${srcdir}/text-motions.c ${srcdir}/text-regex.c ${srcdir}/text-util.c ${srcdir}/text-objects.c
ELF = vsm

CFLAGS = -g
LDFLAGS = -lncurses

all: $(ELF)

vsm: ${srcdir}/*.c ${srcdir}/*.h
	${CC} ${CFLAGS} ${SRC} ${LDFLAGS} -o $@
