
AM_CPPFLAGS = \
	$(DEFAULT_CPPFLAGS) \
	-I$(top_srcdir)

AM_CFLAGS = $(DEFAULT_CFLAGS)

noinst_LTLIBRARIES = libarch.la

libarch_la_SOURCES = \
	arch.h \
	arch.c \
	kexec_trampoline.h \
	kexec_trampoline.S

MAINTAINERCLEANFILES = Makefile.in
