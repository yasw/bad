noinst_LTLIBRARIES = libmeth1.la

libmeth1_la_SOURCES = \
	client.hh client.cc \
	cluster.hh cluster.cc \
	meth1_memory.hh meth1_memory.cc \
	meth1_merge.hh \
	node.hh node.cc \
	priority_queue.hh \
	rec_loader.hh rec_loader.cc \
	remote_file.hh

libmeth1_la_CPPFLAGS = \
	-I$(srcdir)/.. \
	-I$(srcdir)/../libutil \
	-I$(srcdir)/../libsort

libmeth1_la_LIBADD = \
	../libutil/libutil.la \
	../libsort/libsort.la
