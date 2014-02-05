/*
 * MySQl ISOBUS Message Database Implementation
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

#include <stdlib.h>
#include <string.h>

#include <mysql.h>

#include "mesg_db.h"


#define HOST	"localhost"
#define USER	"root"
#define PASSWD	"localpasswd"
#define MESGDB	"isoblued"

#define QUERY	"INSERT INTO isobus_messages " \
	"(pgn, data, src, dest, bus, sec, usec) " \
	"VALUES (?,?,?,?,?,?)"


struct db_info {
	MYSQL *conn;
	MYSQL_STMT *stmt;
	MYSQL_BIND params[7];
};

/* TODO: Error checking */
mesg_db *mesg_db_init(void) {
	struct db_info *db = calloc(sizeof(struct db_info), 1);

	/* Initialize DB connection */
	db->conn = mysql_init(NULL);
	mysql_real_connect(db->conn, HOST, USER, PASSWD, MESGDB, 0, NULL, 0);

	/* PGN parameter */
	db->params[0].buffer_type = MYSQL_TYPE_LONG;
	db->params[0].buffer_length = sizeof(pgn_t);
	/* data parameter */
	db->params[1].buffer_type = MYSQL_TYPE_BLOB;
	db->params[1].length = &db->params[1].buffer_length;
	/* src parameter */
	db->params[2].buffer_type = MYSQL_TYPE_TINY;
	db->params[2].buffer_length = sizeof(__u8);
	/* dest parameter */
	db->params[3].buffer_type = MYSQL_TYPE_TINY;
	db->params[3].buffer_length = sizeof(__u8);
	/* bus parameter */
	db->params[4].buffer_type = MYSQL_TYPE_TINY;
	db->params[4].buffer_length = sizeof(__u8);
	/* sec parameter */
	db->params[5].buffer_type = MYSQL_TYPE_LONGLONG;
	db->params[5].buffer_length = sizeof(((struct timeval *) 0)->tv_sec);
	/* usec parameter */
	db->params[6].buffer_type = MYSQL_TYPE_LONG;
	db->params[6].buffer_length = sizeof(((struct timeval *) 0)->tv_usec);

	/* Initialize prepared statement */
	db->stmt = mysql_stmt_init(db->conn);
	mysql_stmt_prepare(db->stmt, QUERY, strlen(QUERY));
	mysql_stmt_bind_param(db->stmt, db->params);

	return (mesg_db) db;
}

int mesg_db_insert(mesg_db *db, struct isobus_mesg *mesg, int iface, __u8 src,
		__u8 dest, struct timeval *ts) {
	struct db_info *info = (struct db_info *) db;

	/* Fill in PGN */
	info->params[0].buffer = &mesg->pgn;
	/* Fill in data */
	info->params[1].buffer = mesg->data;
	info->params[1].buffer_length = mesg->dlen;
	/* Fill in src */
	info->params[2].buffer = &src;
	/* Fill in dest */
	info->params[3].buffer = &dest;
	/* Fill in bus */
	info->params[4].buffer = &iface;
	/* Fill in sec */
	info->params[5].buffer = &ts->tv_sec;
	/* Fill in usec */
	info->params[6].buffer = &ts->tv_usec;

	return mysql_stmt_execute(info->stmt);
}

