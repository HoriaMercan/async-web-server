// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/eventfd.h>
#include <libaio.h>
#include <errno.h>

#include "aws.h"
#include "utils/util.h"
#include "utils/debug.h"
#include "utils/sock_util.h"
#include "utils/w_epoll.h"

#define HTTP_REQ_MSG "HTTP/1.0\r\n"
#define OK_MSG "HTTP/ 200 OK\r\n\r\n"
#define NOT_FOUND_MSG "HTTP/ 404 NOT FOUND\r\n\r\n"

/* server socket file descriptor */
static int listenfd;

/* epoll file descriptor */
static int epollfd;

static io_context_t ctx;

static int aws_on_path_cb(http_parser *p, const char *buf, size_t len)
{
	struct connection *conn = (struct connection *) p->data;

	memcpy(conn->request_path, buf, len);
	conn->request_path[len] = '\0';
	conn->have_path = 1;

	return 0;
}

static void connection_prepare_send_reply_header(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the reply header. */

	char buff[64];
	int buff_size;
	int bytes_read = 0;
	int rc;

	sprintf(buff, "%s", OK_MSG);
	buff_size = strlen(buff);

	do {
		rc = send(conn->sockfd, buff + bytes_read,
				  buff_size - bytes_read, 0);
		bytes_read += rc;
	} while (rc > 0);
	DIE(rc < 0, "write failed");
}

static void connection_prepare_send_404(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the 404 header. */
	conn->state = STATE_404_SENT;

	char buff[64];
	int buff_size;
	int bytes_read = 0;
	int rc;

	sprintf(buff, "%s", NOT_FOUND_MSG);
	buff_size = strlen(buff);

	do {
		rc = send(conn->sockfd, buff + bytes_read,
				  buff_size - bytes_read, 0);
		bytes_read += rc;
	} while (rc > 0);
	DIE(rc < 0, "write failed");
}

static enum resource_type connection_get_resource_type(struct connection *conn)
{
	/* TODO: Get resource type depending on request path/filename. Filename should
	 * point to the static or dynamic folder.
	 */
	if (strstr(conn->request_path, "static"))
		return RESOURCE_TYPE_STATIC;

	if (strstr(conn->request_path, "dynamic"))
		return RESOURCE_TYPE_DYNAMIC;

	return RESOURCE_TYPE_NONE;
}

/*Done*/
struct connection *connection_create(int socket_fd)
{
	/* TODO: Initialize connection structure on given socket. */
	struct connection *conn = malloc(sizeof(*conn));

	DIE(conn == NULL, "malloc");

	conn->sockfd = socket_fd;
	memset(conn->recv_buffer, 0, BUFSIZ);
	memset(conn->send_buffer, 0, BUFSIZ);
	conn->recv_len = 0;
	conn->eventfd = -1;
	conn->send_len = 0;
	memset(&conn->recv_buffer, 0, BUFSIZ);
	conn->state = STATE_INITIAL;

	return conn;
}

unsigned long __min(unsigned long a, unsigned long b)
{
	return a < b ? a : b;
}

void connection_start_async_io(struct connection *conn)
{
	/* TODO: Start asynchronous operation (read from file).
	 * Use io_submit(2) & friends for reading data asynchronously.
	 */
	int rc;

	if (conn->eventfd == -1) {
		conn->eventfd = eventfd(0, 0);
		rc = w_epoll_add_ptr_in(epollfd, conn->eventfd, conn);
		DIE(rc < 0, "could not add event fd to epoll");
	}
	conn->send_len = __min(conn->file_size - conn->send_pos, BUFSIZ);

	io_prep_pread(&conn->iocb, conn->fd, conn->send_buffer,
				  conn->send_len, conn->send_pos);

	io_set_eventfd(&conn->iocb, conn->eventfd);

	conn->piocb[0] = &conn->iocb;
	rc = io_submit(ctx, 1, conn->piocb);

	if (rc < 0) {
		dprintf("retcode = %d\t errno = %d\t%s\n", rc, errno, strerror(errno));
		io_destroy(ctx);
		rc = io_setup(DEFAULT_LISTEN_BACKLOG, &ctx);

		if (rc < 0)
			errno = -rc;
		DIE(rc < 0, "setup failed after reinit");
	}
	DIE(rc < 0, "io_submit");
}

void connection_remove(struct connection *conn)
{
	/* TODO: Remove connection handler. */
	tcp_close_connection(conn->sockfd);
	free(conn);
}

/* Done */
void handle_new_connection(void)
{
	/* TODO: Handle a new connection request on the server socket. */
	int socket_fd;
	socklen_t addrlen = sizeof(SSA);
	struct sockaddr_in addr;
	struct connection *conn;
	int status;

	/* TODO: Accept new connection. */
	socket_fd = accept(listenfd, (SSA *) &addr, &addrlen);
	DIE(socket_fd < 0, "accept");
	/* TODO: Set socket to be non-blocking. */
	status = fcntl(socket_fd, F_SETFL, fcntl(socket_fd, F_GETFL, 0) | O_NONBLOCK);
	DIE(status < 0, "fcntl on status");
	/* TODO: Instantiate new connection handler. */
	conn = connection_create(socket_fd);
	/* TODO: Add socket to epoll. */
	w_epoll_add_ptr_in(epollfd, socket_fd, conn);
	/* TODO: Initialize HTTP_REQUEST parser. */
	http_parser_init(&conn->request_parser, HTTP_REQUEST);
}

void receive_data(struct connection *conn)
{
	/* TODO: Receive message on socket.
	 * Store message in recv_buffer in struct connection.
	 */

	unsigned long recv_size;
	int rc;
	char peer_address[64];

	rc = get_peer_address(conn->sockfd, peer_address, 64);
	DIE(rc < 0, "get peer address failed");
	dlog(LOG_INFO, "Got data from %s\n", peer_address);

	recv_size = recvfrom(conn->sockfd, conn->recv_buffer, BUFSIZ, 0, NULL, NULL);
	DIE(recv_size < 0, "recvfrom failed");

	conn->recv_len = recv_size;
	conn->state = STATE_RECEIVING_DATA;
}

/* DONE */
int connection_open_file(struct connection *conn)
{
	/* TODO: Open file and update connection fields. */


	conn->fd = open(conn->request_path, O_RDWR);

	return conn->fd;
}

int connection_send_data(struct connection *conn)
{
	/* May be used as a helper function. */
	/* TODO: Send as much data as possible from the connection send buffer.
	 * Returns the number of bytes sent or -1 if an error occurred
	 */

	int rc;
	int bytes_sent = 0;

	do {
		rc = send(conn->sockfd, conn->send_buffer + bytes_sent,
				  conn->send_len - bytes_sent, 0);
		bytes_sent += rc;
	} while (rc > 0);

	conn->send_pos += bytes_sent;
	conn->send_len = 0;

	if (conn->send_pos >= conn->file_size) {
		conn->state = STATE_DATA_SENT;
		rc = w_epoll_remove_ptr(epollfd, conn->eventfd, conn);
		DIE(rc < 0, "could not remove eventfd-conn from epoll");
		return 1;
	}

	connection_start_async_io(conn);
	return 0;
}

void connection_complete_async_io(struct connection *conn)
{
	/* TODO: Complete asynchronous operation; operation returns successfully.
	 * Prepare socket for sending.
	 */
	unsigned long long ops = 0;
	int rc;

	rc = read(conn->eventfd, &ops, sizeof(ops));
	DIE(rc < 0, "read from eventfd");

	if (ops > 0)
		connection_send_data(conn);
}

int parse_header(struct connection *conn)
{
	/* TODO: Parse the HTTP header and extract the file path. */
	/* Use mostly null settings except for on_path callback. */
	http_parser_settings settings_on_path = {
	.on_message_begin = 0,
	.on_header_field = 0,
	.on_header_value = 0,
	.on_path = aws_on_path_cb,
	.on_url = 0,
	.on_fragment = 0,
	.on_query_string = 0,
	.on_body = 0,
	.on_headers_complete = 0,
	.on_message_complete = 0
	};

	conn->request_parser.data = conn;
	http_parser_execute(&conn->request_parser, &settings_on_path,
						conn->recv_buffer, conn->recv_len);

	return 0;
}

enum connection_state connection_send_static(struct connection *conn)
{
	/* TODO: Send static data using sendfile(2). */
	return STATE_NO_STATE;
}

int connection_send_dynamic(struct connection *conn)
{
	/* TODO: Read data asynchronously.
	 * Returns 0 on success and -1 on error.
	 */
	if (conn->state != STATE_ASYNC_ONGOING && conn->state != STATE_DATA_SENT) {
		conn->state = STATE_ASYNC_ONGOING;

		connection_start_async_io(conn);
		return 0;
	}

	if (conn->state == STATE_ASYNC_ONGOING)
		connection_complete_async_io(conn);
	return 0;
}


void handle_input(struct connection *conn)
{
	/* TODO: Handle input information: may be a new message or notification of
	 * completion of an asynchronous I/O operation.
	 */
	int rc;

	conn->state = STATE_RECEIVING_DATA;

	dprintf("%s\n", conn->recv_buffer);
	rc = w_epoll_update_ptr_out(epollfd, conn->sockfd, conn);
	DIE(rc < 0, "epoll update");

	receive_data(conn);
	conn->state = STATE_REQUEST_RECEIVED;



//	switch (conn->state) {
//		default:
//			printf("shouldn't get here %d\n", conn->state);
//	}
}

void handle_output(struct connection *conn)
{
	/* TODO: Handle output information: may be a new valid requests or notification of
	 * completion of an asynchronous I/O operation or invalid requests.
	 */

	if (conn->state == STATE_INITIAL || conn->state == STATE_REQUEST_RECEIVED) {
		parse_header(conn);

		sprintf(conn->filename, "%s%s",
				AWS_DOCUMENT_ROOT, conn->request_path + 1);
		memcpy(conn->request_path, conn->filename, BUFSIZ);
		conn->state = STATE_SENDING_DATA;

//		if (connection_get_resource_type(conn) == RESOURCE_TYPE_NONE)
		connection_open_file(conn);

		if (conn->fd < 0) {
			dprintf("File does not exist: %s\n", conn->request_path);
			connection_prepare_send_404(conn);
			w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
			conn->state = STATE_DATA_SENT;
			return;
		}

		connection_prepare_send_reply_header(conn);
		struct stat s;

		fstat(conn->fd, &s);
		conn->file_size = s.st_size;
	}

	enum resource_type rt = connection_get_resource_type(conn);
	int bytes_sent;
	off_t offset = 0;

	switch (rt) {
	case RESOURCE_TYPE_STATIC: {
		do {
			bytes_sent = sendfile(conn->sockfd, conn->fd, &offset, conn->file_size - offset);
		} while (bytes_sent > 0);

		conn->state = STATE_DATA_SENT;
		break;
	}
	case RESOURCE_TYPE_DYNAMIC: {
		connection_send_dynamic(conn);
		break;
	}
	default:
		dprintf("should not get here\n");
	}

	if (conn->state == STATE_DATA_SENT) {
		close(conn->fd);

		if (conn->eventfd > 0)
			close(conn->eventfd);
		w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
	}
}

void handle_client(uint32_t event, struct connection *conn)
{
	/* TODO: Handle new client. There can be input and output connections.
	 * Take care of what happened at the end of a connection.
	 */

	if (event & EPOLLIN && conn->state == STATE_INITIAL) {
		handle_input(conn);
		return;
	}

	if (event & EPOLLOUT)
		handle_output(conn);

	if (conn->state == STATE_DATA_SENT)
		connection_remove(conn);
}

int main(void)
{
	int rc;

	/* TODO: Initialize asynchronous operations. */
	rc = io_setup(1000, &ctx);
	DIE(rc < 0, "io_setup");
	/* TODO: Initialize multiplexing. */
	epollfd = w_epoll_create();
	/* TODO: Create server socket. */
	listenfd = tcp_create_listener(AWS_LISTEN_PORT, DEFAULT_LISTEN_BACKLOG);
	/* TODO: Add server socket to epoll object*/
	w_epoll_add_fd_in(epollfd, listenfd);
	/* Uncomment the following line for debugging. */
	dlog(LOG_INFO, "Server waiting for connections on port %d\n", AWS_LISTEN_PORT);

	/* server main loop */
	while (1) {
		struct epoll_event rev;

		/* TODO: Wait for events. */
		w_epoll_wait_infinite(epollfd, &rev);
		/* TODO: Switch event types; consider
		 *   - new connection requests (on server socket)
		 *   - socket communication (on connection sockets)
		 */

		if (rev.data.fd == listenfd) {
			if (rev.events & EPOLLIN)
				handle_new_connection();
		} else {
			handle_client(rev.events, rev.data.ptr);
		}
	}

	io_destroy(ctx);
	return 0;
}
