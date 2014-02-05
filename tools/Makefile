TOOLS := can_log_raw isoblued
TEST := sc_mod_test can_stress isoblue_dummy
PREFIX := /usr
CPPFLAGS := -Wall -Wextra -O3 \
	-I../socketcan-isobus/patched \
	-I../socketcan-isobus \
	$(CFLAGS)

# Ubuntu keeps MySQL in the wrong place...
CPPFLAGS += -I/usr/include/mysql

all : $(TOOLS) $(TEST)
.PHONY : all

include ../buildnum.mk

isoblued isoblue_dummy : LDLIBS += -lbluetooth
isoblued : LDLIBS += -lmysqlclient
isoblued : ring_buf.o mesg_db_mysql.o

mesg_db_mysql.o : mesg_db_mysql.c mesg_db.h

ring_buf.o : ring_buf.c ring_buf.h

install : $(TOOLS:%=install_%)

install_% : %
	install $* $(PREFIX)/bin/$*

.PHONY : clean
clean ::
	-$(RM) $(TOOLS) $(TEST) *.o
