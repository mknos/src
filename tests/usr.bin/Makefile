#	$NetBSD: Makefile,v 1.40 2024/03/14 21:00:33 rillig Exp $
#

.include <bsd.own.mk>

TESTSDIR=       ${TESTSBASE}/usr.bin

TESTS_SUBDIRS=	awk basename bzip2 cc cmp compress config cpio col cut \
		diff dirname error find fstat gdb grep gzip id indent \
		infocmp jot ld locale m4 make mixerctl mkdep mtree nbperf \
		netpgpverify patch pkill pr printf pwhash realpath rump_server \
		shmif_dumpbus sdiff sed sort stat tar tmux tr unifdef uniq \
		vmstat xlint ztest

.if ${MKCXX} != "no"
TESTS_SUBDIRS+=	c++
.endif

.include <bsd.test.mk>
