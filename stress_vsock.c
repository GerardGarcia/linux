/*
 * Reports received packets per each connected
 * socket every a specified number of packets received.
 * */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <pthread.h>
#include <assert.h>
#include "include/uapi/linux/vm_sockets.h"

#define LISTEN_PORT 1234
#define LISTEN_CID 4

#define NUM_CLIENTS 100
#define BUF_LEN 512

enum state_machine {
	TEST_1_SERVER,
	TEST_1_CLIENT
};

int setup_recv_socket(int cid, int port)
{
	int s = socket(AF_VSOCK, SOCK_STREAM, 0);
	if (s < 0) {
		perror("socket()");
		return -1;
	}

	struct sockaddr_vm sa_listen = {
		.svm_family = AF_VSOCK,
		.svm_cid = cid,
	};
	sa_listen.svm_port = port;

	if (bind(s, (struct sockaddr *)&sa_listen,
		 sizeof(sa_listen)) != 0) {
		perror("bind()");
		close(s);
		return -1;
	}

	if (listen(s, NUM_CLIENTS) != 0) {
		perror("listen()");
		close(s);
		return -1;
	}

	return s;
}

int setup_send_socket(int cid, int port)
{
	int s = socket(AF_VSOCK, SOCK_STREAM, 0);
	if (s < 0){
		perror("s()");
		return -1;
	}

	struct sockaddr_vm sa_server = {
		.svm_family = AF_VSOCK,
		.svm_cid = cid,
	};
	sa_server.svm_port = port;

	if (connect(s, (struct sockaddr *)&sa_server,
		    sizeof(sa_server)) != 0) {
		perror("connect()");
		close(s);
		return -1;
	}

	return s;
}

void *recv_data_thread(void *arg)
{
	int s = *((int *) arg);
	char *buf = (char *) malloc(BUF_LEN);
	int r = 0;
	int *total_recv = (int *) malloc(sizeof(int));

	*total_recv = 0;
	while(*total_recv < BUF_LEN) {
		r = recv(s, buf, BUF_LEN, 0);
		if (r < 0) {
			if (errno == EAGAIN){
				continue;
			} else {
				perror("recv()");
				break;
			}
		}

		*total_recv += r;
	}

	close(s);
	free(arg);

	return (void *) total_recv;
}

void *send_data_thread(void *arg)
{
	int s = *((int *) arg);
	char *buf = (char*) malloc(BUF_LEN);
	int sent = 0;
	int *total_sent = (int*) malloc(sizeof(int));

	int fd = open("/dev/urandom", O_RDONLY);
	read(fd, buf, sizeof(buf));
	close(fd);

	*total_sent = 0;
	while(*total_sent < BUF_LEN) {
		sent = send(s, buf + *total_sent, BUF_LEN - *total_sent, 0);
		if (sent < 0) {
			if (errno == EAGAIN) {
				continue;
			} else {
				perror("send()");
				break;
			}
		}

		*total_sent += sent;
	}

	close(s);
	free(arg);

	return (void *) total_sent;
}

int main(int argc, char *argv[])
{
	enum state_machine state;
	int server_cid;

	int opt;
	while ((opt = getopt(argc, argv, "sca:")) != -1) {
		switch(opt) {
		case 's':
			state = TEST_1_SERVER;
			break;
		case 'c':
			state = TEST_1_CLIENT;
			break;
		case 'a':
			server_cid = strtol(optarg, NULL, 10);
			break;
		default:
			break;
		}
	}

	switch(state) {
	case TEST_1_SERVER:
		{
			printf("TEST: Receive data from multiple connections.\n");

			int listen_fd = setup_recv_socket(VMADDR_CID_ANY, LISTEN_PORT);
			assert(listen_fd > 0 && "Error creating listening socket");

			fd_set readfds;
			FD_ZERO(&readfds);
			FD_SET(listen_fd, &readfds);

			int r, num_clients = 0;
			pthread_t clients[NUM_CLIENTS];

			struct timeval tv = {
				.tv_sec = 60,
				.tv_usec = 0
			};

			printf("Waiting for clients to connect\n");

			while ((r = select(listen_fd + 1, &readfds, NULL,
					   NULL, &tv)) > 0) {
				if (r == 0) {
					break;
				} else if (r < 0) {
					perror("select()");
					break;
				}

				int *client_fd = (int *) malloc(sizeof(int));
				if (FD_ISSET(listen_fd, &readfds)) {
					int *client_fd = (int *) malloc(sizeof(int));
					*client_fd = accept(listen_fd, NULL,
							    NULL);

					if (pthread_create(&clients[num_clients], NULL,
							   recv_data_thread, client_fd) < 0) {
						perror("pthread_create()");
					}

					printf("Connection %d established correctly\n",
					       num_clients);
					++num_clients;
				}

				if (num_clients == NUM_CLIENTS)
					break;

				FD_SET(listen_fd, &readfds);
			}

			assert(num_clients == NUM_CLIENTS && "Not all clients connected");
			printf("All %d connectins established correctly\n", NUM_CLIENTS);

			for (int i = 0; i < num_clients; ++i) {
				int *data_received;
				pthread_join(clients[i], (void **) &data_received);
				assert(*data_received == BUF_LEN &&
				       "Not all data received");
				free(data_received);
			}

			printf("Received %dB from each connection\n", BUF_LEN);
			printf("Test finished correctly\n");

			close(listen_fd);

			break;
		}
	case TEST_1_CLIENT:
		{
			printf("TEST: Sent data with multiple connections.\n");

			int num_clients = 0;
			pthread_t clients[NUM_CLIENTS];

			for (int i = 0; i < NUM_CLIENTS; ++i) {
				int *send_fd = (int*) malloc(sizeof(int));
				*send_fd = setup_send_socket(server_cid, LISTEN_PORT);

				assert(*send_fd > 0 && "Error creating sending socket");

				if (pthread_create(&clients[num_clients], NULL,
						   send_data_thread, send_fd) < 0) {
					perror("pthread_create()");
				}

				printf("Connection %d established correctly\n", num_clients);
				++num_clients;
			}

			assert(num_clients == NUM_CLIENTS && "Not all clients connected");
			printf("All %d connections established correctly\n", NUM_CLIENTS);

			for (int i = 0; i < num_clients; ++i) {
				int *data_sent;
				pthread_join(clients[i], (void **) &data_sent);
				assert(*data_sent == BUF_LEN &&
				       "Not all data sent");
				free(data_sent);
			}

			printf("Sent %dB with each connection\n", BUF_LEN);
			printf("Test finished correctly\n");

			break;
		}
	}

	return 0;
}
