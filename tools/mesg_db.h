/*
 * ISOBUS Message Database Interface
 *
 *
 * Author: Alex Layton <alex@layton.in>
 *
 * Copyright (C) 2014 Purdue University
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef MESG_DB_H
#define MESG_DB_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <isobus.h>
#include <sys/time.h>

typedef void * mesg_db;

mesg_db *mesg_db_init(void);

int mesg_db_insert(mesg_db *db, struct isobus_mesg *mesg, int iface, __u8 src,
		__u8 dest, struct timeval *ts);

#ifdef	__cplusplus
}
#endif

#endif /* MESG_DB_H */

