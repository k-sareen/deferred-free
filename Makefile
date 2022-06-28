CFLAGS=-Wall -O3
QL_CFLAGS=-shared -fPIC
QL_LIBS=-pthread -ldl
CPPFLAGS=-I./include
LIBFLAGS=-L./out
SRCDIR=./src
TESTDIR=./test
ODIR=./out

all: ql-shared pthread-malloc fork-malloc
.PHONY: all

ql-shared: $(SRCDIR)/ql.c | $(ODIR)/
	$(CC) $(CFLAGS) $(CPPFLAGS) $(QL_CFLAGS) -o ${ODIR}/libql.so $< $(QL_LIBS)
.PHONY: ql-shared

pthread-malloc: $(TESTDIR)/pthread-malloc.c | $(ODIR)/
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LIBFLAGS) -o ${ODIR}/$@ $< -pthread -lql -Wl,-rpath=`pwd`/$(ODIR)
.PHONY: pthread-malloc

fork-malloc: $(TESTDIR)/fork-malloc.c | $(ODIR)/
	$(CC) $(CFLAGS) $(CPPFLAGS) -o ${ODIR}/$@ $<
.PHONY: fork-malloc

$(ODIR)/:
	mkdir -p $(ODIR)

clean:
	rm -rf ${ODIR}/libql.so ${ODIR}/pthread-malloc ${ODIR}/fork-malloc
.PHONY: clean

