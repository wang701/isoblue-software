/*
 * ISOBlue "server" application
 *
 * Enables getting at ISOBUS messages over Bluetooth.
 *
 *
 * Author: Alex Layton <alex@layton.in>
 *
 * Copyright (C) 2013 Purdue University
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

#define ISOBLUED_VER	"restd - ISOBlue REST daemon"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

#include <argp.h>

#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <leveldb/c.h>

#include <curl/curl.h>
#include <zlib.h>
#include <pthread.h>

#include "../socketcan-isobus/patched/can.h"
#include "../socketcan-isobus/isobus.h"

#include "ring_buf.h"

/* argp goodies */
#ifdef BUILD_NUM
const char *argp_program_version = ISOBLUED_VER "\n" BUILD_NUM;
#else
const char *argp_program_version = ISOBLUED_VER;
#endif
const char *argp_program_bug_address = "<bugs@isoblue.org>";
static char args_doc[] = "BUF_FILE [IFACE...]";
static char doc[] = "Connect ISOBlue to IFACE(s), using BUF_FILE as a buffer.";
static struct argp_option options[] = {
	{NULL, 0, NULL, 0, "About", -1},
	{NULL, 0, NULL, 0, "Configuration", 0},
	{"post-url", 'u', "<url>", 0, "URL to which to POST JSON messages", 0},
	{"content-type", 'c', "<type>", 0, "Content-Type to POST", 0},
	{"authorization-token", 't', "<token>", 0, "Authorization token to use", 0},
	{"http-header", 'H', "<header>", 0, "HTTP header to use", 0},
	{"max-messages", 'm', "<count>", 0, "Maximum messages per POST", 0},
	{"retry-delay", 'r', "<secs>", 0, "Seconds to wait between reties", 0},
	{"post-delay", 'p', "<secs>", 0, "Seconds to wait between posts", 0},
	{"gzip-compress", 'g', 0, 0, "gzip compress POST body", 0},
	{"dont-keep-data", 'd', 0, 0, "Delete messages when they are POSTed", 0},
	{"buffer-order", 'b', "<order>", 0, "Use a 2^<order> MB buffer", 0},
	{ 0 }
};
struct arguments {
	char *file;
	char **ifaces;
	int nifaces;
	char *post_url;
	char *content_type;
	char *authorization_token;
	char **headers;
	size_t n_headers;
	int max_messages;
	int retry_delay;
	int post_delay;
	bool compression;
	bool dont_keep;
	int buf_order;
};
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct arguments *arguments = state->input;

	switch(key) {
	case 'u':
		arguments->post_url = arg;
		break;

	case 'c':
		arguments->content_type = arg;
		break;

	case 't':
		arguments->authorization_token = arg;
		break;

	case 'H':
		arguments->headers = realloc(arguments->headers,
				++arguments->n_headers * sizeof(*(arguments->headers)));
		arguments->headers[arguments->n_headers - 1] = arg;
		break;

	case 'm':
		arguments->max_messages = atoi(arg);
		break;

	case 'r':
		arguments->retry_delay = atoi(arg);
		break;

	case 'p':
		arguments->post_delay = atoi(arg);
		break;

	case 'g':
		arguments->compression = true;
		break;

	case 'd':
		arguments->dont_keep = true;
		break;

	case 'b':
		arguments->buf_order = atoi(arg);
		break;

	case ARGP_KEY_ARG:
		if(state->arg_num == 0)
			arguments->file = arg;
		else
			return ARGP_ERR_UNKNOWN;
		break;

	case ARGP_KEY_ARGS:
		arguments->ifaces = state->argv + state->next;
		arguments->nifaces = state->argc - state->next;
		break;

	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}
#define POST_DOC_TEXT	"With no IFACE, connect to ib_eng and ib_imp."
#define EXTRA_TEXT	"ISOBlue home page <http://www.isoblue.org>"
static char *help_filter(int key, const char *text, void *input)
{
	char *buffer = input;

	switch(key) {
	case ARGP_KEY_HELP_POST_DOC:
		return strdup(POST_DOC_TEXT);

	case ARGP_KEY_HELP_EXTRA:
		return strdup(EXTRA_TEXT);

	case ARGP_KEY_HELP_HEADER:
		buffer = malloc(strlen(text)+1);
		strcpy(buffer, text);
		return strcat(buffer, ":");

	default:
		return (char *)text;
	}
}
static struct argp argp = {
	options,
	parse_opt,
	args_doc,
	doc,
	NULL,
	help_filter,
	NULL
};

/* Leveldb stuff */
leveldb_t *db;
leveldb_options_t *db_options;
leveldb_readoptions_t *db_roptions;
leveldb_writeoptions_t *db_woptions;
typedef uint32_t db_key_t;
db_key_t db_id = 1, db_stop = 0, http_id;
const db_key_t LEVELDB_ID_KEY = 0;
static void leveldb_cmp_destroy(void *arg __attribute__ ((unused))) { }
static int leveldb_cmp_compare(void *arg __attribute__ ((unused)) ,
		const char *a, size_t alen __attribute__ ((unused)),
		const char *b, size_t blen __attribute__ ((unused))) {
	return *((db_key_t *)a) - *((db_key_t *)b);
}
static const char * leveldb_cmp_name(void *arg __attribute__ ((unused))) {
	return "isoblued.v1";
}
/* Magic numbers related to past data... */
#define PAST_THRESH	200
#define PAST_CNT	4

/* Function to wait for one or more file descriptors to be ready */
static inline int wait_func(int n_fds, fd_set *tmp_rfds, fd_set *tmp_wfds)
{
	int ret;

	if((ret = select(n_fds + 1, tmp_rfds, tmp_wfds, NULL, NULL)) < 0) {
		perror("select");

		switch(errno) {
		case EINTR:
			return 0;

		default:
			exit(EXIT_FAILURE);
		}
	}

	return ret;
}

/* Fast method to convert to hex */
static inline char nib2hex(uint_fast8_t nib)
{
	nib &= 0x0F;

	return nib >= 10 ? nib - 10 + 'a' : nib + '0';
}

pthread_mutex_t send_mutex;
pthread_cond_t send_cond;

/* Function to handle incoming ISOBUS message(s) */
static inline int read_func(int sock, int iface)
{
	/* Construct msghdr to use to recevie messages from socket */
	static struct isobus_mesg mes;
	static struct sockaddr_can addr;
	static struct iovec iov = {&mes, sizeof(mes)};
	static char cmsgb[CMSG_SPACE(sizeof(struct sockaddr_can)) +
		CMSG_SPACE(sizeof(struct timeval))];
	static struct msghdr msg = {&addr, sizeof(addr), &iov, 1,
			cmsgb, sizeof(cmsgb), 0};

	if(recvmsg(sock, &msg, MSG_DONTWAIT) <= 0) {
		perror("recvmsg");
		exit(EXIT_FAILURE);
	}

	/* Get saddr and approximate arrival time */
	struct sockaddr_can daddr = { 0 };
	struct timeval tv = { 0 };
	struct cmsghdr *cmsg;
	for(cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
			cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if(cmsg->cmsg_level == SOL_CAN_ISOBUS &&
				cmsg->cmsg_type == CAN_ISOBUS_DADDR) {
			memcpy(&daddr, CMSG_DATA(cmsg), sizeof(daddr));
		} else if(cmsg->cmsg_level == SOL_SOCKET &&
				cmsg->cmsg_type == SO_TIMESTAMP) {
			memcpy(&tv, CMSG_DATA(cmsg), sizeof(tv));
		}
	}

	static char json[1000];

	char *sp, *cp;
	cp = sp = &json[0];

	*(cp++) = '{';

	/* Print CAN interface index (1 nibble) */
	*(cp++) = '"';
	*(cp++) = 'b';
	*(cp++) = 'u';
	*(cp++) = 's';
	*(cp++) = '"';
	*(cp++) = ':';
	*(cp++) = '"';
	*(cp++) = nib2hex(iface);
	*(cp++) = '"';

	*(cp++) = ',';

	/* Print PGN (5 nibbles) */
	*(cp++) = '"';
	*(cp++) = 'p';
	*(cp++) = 'g';
	*(cp++) = 'n';
	*(cp++) = '"';
	*(cp++) = ':';
	*(cp++) = '"';
	*(cp++) = nib2hex(mes.pgn >> 16);
	*(cp++) = nib2hex(mes.pgn >> 12);
	*(cp++) = nib2hex(mes.pgn >> 8);
	*(cp++) = nib2hex(mes.pgn >> 4);
	*(cp++) = nib2hex(mes.pgn);
	*(cp++) = '"';

	*(cp++) = ',';

	/* Print destination address (2 nibbles) */
	*(cp++) = '"';
	*(cp++) = 'd';
	*(cp++) = 's';
	*(cp++) = 't';
	*(cp++) = '"';
	*(cp++) = ':';
	*(cp++) = '"';
	*(cp++) = nib2hex(daddr.can_addr.isobus.addr >> 4);
	*(cp++) = nib2hex(daddr.can_addr.isobus.addr);
	*(cp++) = '"';

	*(cp++) = ',';

	/* Print data bytes (4 nibbles length) */
	*(cp++) = '"';
	*(cp++) = 'd';
	*(cp++) = 'a';
	*(cp++) = 't';
	*(cp++) = 'a';
	*(cp++) = '"';
	*(cp++) = ':';
	*(cp++) = '"';
//	*(cp++) = nib2hex(mes.dlen >> 12);
//	*(cp++) = nib2hex(mes.dlen >> 8);
//	*(cp++) = nib2hex(mes.dlen >> 4);
//	*(cp++) = nib2hex(mes.dlen);
	int j;
	for(j = 0; j < mes.dlen; j++)
	{
		*(cp++) = nib2hex(mes.data[j] >> 4);
		*(cp++) = nib2hex(mes.data[j]);
	}
	*(cp++) = '"';

	*(cp++) = ',';

	/* Print timestamp (8 nibbles sec, 5 nibbles usec) */
	*(cp++) = '"';
	*(cp++) = 't';
	*(cp++) = 'i';
	*(cp++) = 'm';
	*(cp++) = 'e';
	*(cp++) = 's';
	*(cp++) = 't';
	*(cp++) = 'a';
	*(cp++) = 'm';
	*(cp++) = 'p';
	*(cp++) = '"';
	*(cp++) = ':';
	/*
	*(cp++) = '"';
	*(cp++) = nib2hex(tv.tv_sec >> 28);
	*(cp++) = nib2hex(tv.tv_sec >> 24);
	*(cp++) = nib2hex(tv.tv_sec >> 20);
	*(cp++) = nib2hex(tv.tv_sec >> 16);
	*(cp++) = nib2hex(tv.tv_sec >> 12);
	*(cp++) = nib2hex(tv.tv_sec >> 8);
	*(cp++) = nib2hex(tv.tv_sec >> 4);
	*(cp++) = nib2hex(tv.tv_sec);
	*(cp++) = '.';
	*(cp++) = nib2hex(tv.tv_usec >> 16);
	*(cp++) = nib2hex(tv.tv_usec >> 12);
	*(cp++) = nib2hex(tv.tv_usec >> 8);
	*(cp++) = nib2hex(tv.tv_usec >> 4);
	*(cp++) = nib2hex(tv.tv_usec);
	*(cp++) = '"';
	*/
	int i;
	bool leading = true;
	for(i = 1000000000; i > 0; i /= 10) {
		uint_fast8_t digit = (tv.tv_sec / i) % 10;

		/* Leave off leading zeros */
		if(leading && digit == 0) {
			continue;
		}

		leading = false;
		*(cp++) = digit + '0';
	}
	*(cp++) = '.';
	*(cp++) = ((tv.tv_usec / 100000) % 10) + '0';
	*(cp++) = ((tv.tv_usec / 10000) % 10) + '0';
	*(cp++) = ((tv.tv_usec / 1000) % 10) + '0';
	*(cp++) = ((tv.tv_usec / 100) % 10) + '0';
	*(cp++) = ((tv.tv_usec / 10) % 10) + '0';
	*(cp++) = ((tv.tv_usec / 1) % 10) + '0';

	*(cp++) = ',';

	/* Print source address (2 nibbles) */
	*(cp++) = '"';
	*(cp++) = 's';
	*(cp++) = 'r';
	*(cp++) = 'c';
	*(cp++) = '"';
	*(cp++) = ':';
	*(cp++) = '"';
	*(cp++) = nib2hex(addr.can_addr.isobus.addr >> 4);
	*(cp++) = nib2hex(addr.can_addr.isobus.addr);
	*(cp++) = '"';

	*(cp++) = '}';

	/* Put messaged in leveldb */
	char *db_err = NULL;
	leveldb_put(db, db_woptions, (char *)&db_id, sizeof(db_id),
			sp, cp-sp, &db_err);
	if(db_err) {
		fprintf(stderr, "Leveldb write error.\n");
		leveldb_free(db_err);
		db_err = NULL;
		return -1;
	}
	db_id++;
	leveldb_put(db, db_woptions, (char *)&LEVELDB_ID_KEY, sizeof(db_key_t),
			(char *)&db_id, sizeof(db_id), &db_err);

	pthread_mutex_lock(&send_mutex);
	pthread_cond_signal(&send_cond);
	pthread_mutex_unlock(&send_mutex);

	return 1;
}

/* Function that does all the work after initialization */
static inline void loop_func(int n_fds, fd_set read_fds, fd_set write_fds,
		int *s, int ns)
{
	while(1) {
		fd_set tmp_rfds = read_fds, tmp_wfds = write_fds;
		if(wait_func(n_fds, &tmp_rfds, &tmp_wfds) == 0) {
			continue;
		}

		/* Read ISOBUS */
		int i;
		for(i = 0; i < ns; i++) {
			if(!FD_ISSET(s[i], &tmp_rfds)) {
				continue;
			}

			if(read_func(s[i], i) < 0) {
				return;
			}
		}
	}
}

/* From:http://www.mattikariluoma.com/files/curl_post_gzip.c */
#ifndef OS_CODE
#  define OS_CODE  0x03  /* assume Unix */
#endif
#if MAX_MEM_LEVEL >= 8
#  define DEF_MEM_LEVEL 8
#else
#  define DEF_MEM_LEVEL  MAX_MEM_LEVEL
#endif
static int gz_magic[2] = {0x1f, 0x8b}; /* gzip magic header */
// see zlib's compress.c:compress() function for the original of this
// modified function
//
// dest      : destination buffer, malloc'd already
// destLen   : size of the malloc'd buffer
// source    : the uncompressed text
// sourceLen : the size of the uncompressed text
//
// this function returns an error code from the zlib library.
// upon return, dest contains the compressed output, and destLen 
// contains the size of dest.
int string_gzip (char *dest, unsigned long *destLen, char *source,
		unsigned long sourceLen)
{
	char *header;
	
	sprintf(dest, "%c%c%c%c%c%c%c%c%c%c", gz_magic[0], gz_magic[1],
			Z_DEFLATED, 0, 0, 0, 0, 0, 0, OS_CODE);
	
	header = dest;
	dest = &dest[10]; // skip ahead of the header
	*destLen -= 10; // update our available length
	
	z_stream stream;
	int err;

	stream.next_in = (Bytef *)source;
	stream.avail_in = sourceLen;
	#ifdef MAXSEG_64K
		/* Check for source > 64K on 16-bit machine: */
		if (stream.avail_in != sourceLen)
			return Z_BUF_ERROR;
	#endif
	stream.next_out = (Bytef *)dest;
	stream.avail_out = *destLen;
	if (stream.avail_out != *destLen)
		return Z_BUF_ERROR;

	stream.zalloc = Z_NULL;
	stream.zfree = Z_NULL;
	stream.opaque = Z_NULL;

	// instructs zlib not to write a zlib header
	err = deflateInit2( &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 
			-MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY );
	if (err != Z_OK)
		return err;

	err = deflate(&stream, Z_FINISH); // Z_FINISH or Z_NO_FLUSH if we have
	                                  // more input still (we don't)
	if (err != Z_STREAM_END) {
		deflateEnd(&stream);
		return err == Z_OK ? Z_BUF_ERROR : err;
	}
	*destLen = stream.total_out;

	err = deflateEnd(&stream);
	
	dest = header; // put the header back on
	*destLen += 10; // update length of our data block
	
	return err;
}

#define CONTENT_TYPE	"Content-Type"
#define AUTHORIZATION	"Authorization"
#define GZIP_CONTENT_HEADER	"Content-Encoding: gzip"
void *http_worker(void *stuff) {
	struct arguments *arguments = stuff;
	CURL *curl;
	struct curl_slist *headers = NULL;
	struct ring_buffer buf;
	leveldb_iterator_t *db_iter;
	char *content_type_header, *authorization_header;
	int post_delay, retry_delay;
	int max_messages;

	post_delay = arguments->post_delay;
	retry_delay = arguments->retry_delay > 0 ? arguments->retry_delay : 
		post_delay;

	max_messages = arguments->max_messages;

	ring_buffer_create(&buf, 20 + arguments->buf_order, arguments->file);

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, arguments->post_url);

	/* Construct Content-Type header */
	content_type_header = (char *)malloc(strlen(CONTENT_TYPE ": ") +
			strlen(arguments->content_type));
	strcpy(content_type_header, CONTENT_TYPE ": ");
	strcat(content_type_header, arguments->content_type);
	headers = curl_slist_append(headers, content_type_header);

	if(arguments->authorization_token != NULL) {
		/* Construct Authorization header */
		authorization_header = (char *)malloc(strlen(AUTHORIZATION ": ") +
				strlen(arguments->authorization_token));
		strcpy(authorization_header, AUTHORIZATION ": ");
		strcat(authorization_header, arguments->authorization_token);
		headers = curl_slist_append(headers, authorization_header);
	}

	/* Add arbitrary headers */
	unsigned int i;
	for(i = 0; i < arguments->n_headers; i++) {
		headers = curl_slist_append(headers, arguments->headers[i]);
	}


	if(arguments->compression) {
		/* Add content encoding header */
		headers = curl_slist_append(headers, GZIP_CONTENT_HEADER);
	}

	/* Add headers to request */
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	/* Be verbose */
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

	while(1) {
		char *sp, *cp;
		int messages = 0;

		pthread_mutex_lock(&send_mutex);
		pthread_cond_wait(&send_cond, &send_mutex);
		pthread_mutex_unlock(&send_mutex);

		ring_buffer_clear(&buf);
		db_iter = leveldb_create_iterator(db, db_roptions);
		leveldb_iter_seek(db_iter, (char *)&http_id, sizeof(db_key_t));
		if(leveldb_iter_valid(db_iter)) {
			sp = cp = ring_buffer_tail_address(&buf);

			*(cp++) = '[';
			ring_buffer_tail_advance(&buf, 1);
			do {
				char *val;
				size_t len;

				val = (char *)leveldb_iter_value(db_iter, &len);
				if(len >= ring_buffer_free_bytes(&buf)) {
					break;
				}
				memcpy(cp, val, len);
				cp += len;

				leveldb_iter_next(db_iter);
				*(cp++) = ',';
				ring_buffer_tail_advance(&buf, len + 1);
				http_id++;

				/* Remove sent messages from levelDb? */
				if(arguments->dont_keep) {
					char *db_err = NULL;

					leveldb_delete(db, db_woptions, (char *)&http_id,
							sizeof(http_id), &db_err);
					if(db_err) {
						fprintf(stderr, "Leveldb write error.\n");
						leveldb_free(db_err);
						db_err = NULL;
					}
				}
			} while(leveldb_iter_valid(db_iter) &&
					!(max_messages > 0 && ++messages >= max_messages));
			*(cp-1) = ']';
			ring_buffer_tail_advance(&buf, 1);

			if(arguments->compression) {
				size_t compressed_len;
				static char *compressed_data = NULL;

				//see zlib's compress.c
				compressed_len = (cp - sp) * 1.1 + 22;

				compressed_data = realloc(compressed_data,
						compressed_len * sizeof(char));
				string_gzip(compressed_data, &compressed_len, sp, cp - sp);

				curl_easy_setopt(curl, CURLOPT_POSTFIELDS, compressed_data);
				curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, compressed_len);
			} else {
				curl_easy_setopt(curl, CURLOPT_POSTFIELDS, sp);
				curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, cp - sp);
			}

			while(curl_easy_perform(curl) != CURLE_OK) {
				if(retry_delay > 0) {
					sleep(retry_delay);
				}
			}

			if(post_delay > 0) {
				sleep(post_delay);
			}
		}

		leveldb_iter_destroy(db_iter);
	}

	return NULL;
}

int main(int argc, char *argv[]) {
	fd_set read_fds, write_fds;
	int n_fds;

	int ns;
	int *s;

	/* Handle options */
	#define DEF_IFACES	((char*[]) {"ib_eng", "ib_imp"})
	struct arguments arguments = {
		"isoblue.log",
		DEF_IFACES,
		sizeof(DEF_IFACES) / sizeof(*DEF_IFACES),
		"http://www.cyrusbowman.com/data/",
		"application/json",
		NULL,
		NULL,
		0,
		0,
		0,
		0,
		false,
		false,
		0,
	};
	argp_parse(&argp, argc, argv, 0, 0, &arguments);

	s = calloc(arguments.nifaces, sizeof(*s));
	ns = arguments.nifaces;
	FD_ZERO(&read_fds);
	FD_ZERO(&write_fds);
	n_fds = 0;

	/* Initialize ISOBUS sockets */
	int i;
	for(i = 0; i < arguments.nifaces; i++) {
		struct sockaddr_can addr = { 0 };
		struct ifreq ifr;

		if((s[i] = socket(PF_CAN, SOCK_DGRAM | SOCK_NONBLOCK, CAN_ISOBUS))
				< 0) {
			perror("socket (can)");
			return EXIT_FAILURE;
		}

		/* Set interface name to argument value */
		strcpy(ifr.ifr_name, arguments.ifaces[i]);
		ioctl(s[i], SIOCGIFINDEX, &ifr);
		addr.can_family  = AF_CAN;
		addr.can_ifindex = ifr.ifr_ifindex;
		addr.can_addr.isobus.addr = ISOBUS_ANY_ADDR;

		if(bind(s[i], (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			perror("bind can");
			return EXIT_FAILURE;
		}

		const int val = 1;
		/* Record directed address of messages */
		setsockopt(s[i], SOL_CAN_ISOBUS, CAN_ISOBUS_DADDR, &val, sizeof(val));
		/* Timestamp messages */
		setsockopt(s[i], SOL_SOCKET, SO_TIMESTAMP, &val, sizeof(val));

		FD_SET(s[i], &read_fds);
		n_fds = s[i] > n_fds ? s[i] : n_fds;
	}

	/* Initialize Leveldb */
	db_options = leveldb_options_create();
	leveldb_options_set_create_if_missing(db_options, 1);
	leveldb_comparator_t *db_cmp;
	db_cmp = leveldb_comparator_create(NULL,
			leveldb_cmp_destroy, leveldb_cmp_compare, leveldb_cmp_name);
	leveldb_options_set_comparator(db_options, db_cmp);
	char *db_err = NULL;
	db = leveldb_open(db_options, "isoblued_rest_db", &db_err);
	if(db_err) {
		fprintf(stderr, "Leveldb open error.\n");
		exit(EXIT_FAILURE);
	}
	db_woptions = leveldb_writeoptions_create();
	leveldb_writeoptions_set_sync(db_woptions, false);
	db_roptions = leveldb_readoptions_create();
	db_id = 1;
	size_t read_len;
	char * read = leveldb_get(db, db_roptions, (char *)&LEVELDB_ID_KEY,
			sizeof(db_key_t), &read_len, &db_err);
	if(db_err || !read_len) {
		leveldb_put(db, db_woptions, (char *)&LEVELDB_ID_KEY, sizeof(db_key_t),
				(char *)&db_id, sizeof(db_id), &db_err);
		if(db_err) {
			fprintf(stderr, "Leveldb db init error.\n");
		} else {
			printf("Leveldb init new db.\n");
		}
	} else {
		db_id = *(db_key_t *)read;
	}
	printf("starting at db id %d.\n", db_id);
	/* Find where sending left off */
	leveldb_iterator_t *db_iter = leveldb_create_iterator(db, db_roptions);
	leveldb_iter_seek_to_first(db_iter);
	leveldb_iter_next(db_iter);
	http_id = *(db_key_t*)leveldb_iter_key(db_iter, &read_len);
	leveldb_iter_destroy(db_iter);

	/* Start HTTP thread */
	pthread_t http_thread;
	pthread_mutex_init(&send_mutex, NULL);
	pthread_cond_init(&send_cond, NULL);
	if(pthread_create(&http_thread, NULL, http_worker, &arguments)) {
		fprintf(stderr, "Error creating thread\n");
		return EXIT_FAILURE;
	}


	/* Do socket stuff */
	loop_func(n_fds, read_fds, write_fds, s, ns);

	return EXIT_SUCCESS;
}

