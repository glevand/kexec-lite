
ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = $(DEFAULT_CPPFLAGS)
AM_CFLAGS = $(DEFAULT_CFLAGS)

LIBTOOL_DEPS = @LIBTOOL_DEPS@

libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status libtool

EXTRA_DIST = bootstrap configure.ac.in $(srcdir)/m4

MAINTAINERCLEANFILES = \
	aclocal.m4 \
	compile \
	config.* \
	configure \
	configure.ac \
	depcomp \
	install-sh \
	ltmain.sh \
	Makefile.in \
	missing \
	$(PACKAGE)-*.gz

maintainer-clean-local:
	rm -rf m4

common_libs = \
	-lfdt \
	-lelf

sbin_PROGRAMS = kexec

kexec_SOURCES = \
	check_type.h \
	container_of.h \
	kexec.c \
	kexec_memory_map.c \
	kexec_memory_map.h \
	kexec_trampoline.h \
	kexec_trampoline.S \
	list.c \
	list.h \
	lite.h \
	simple_allocator.c \
	simple_allocator.h

kexec_LDADD = $(common_libs)
