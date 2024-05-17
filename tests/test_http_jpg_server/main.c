#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <linux/socket.h>
#include <pthread.h>
#include <sys/select.h>
#include <errno.h>

#define BOUNDARY "frame"

static int socket_is_connected(int sockfd) {
    char buffer;
    int result = recv(sockfd, &buffer, 1, MSG_PEEK | MSG_DONTWAIT);
    if (result == 0) {
        return 0;
    } else if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        } else {
            return 0;
        }
    }
    return 1;
}

static size_t socket_write(int socket, void *data, size_t size)
{
	if (!socket_is_connected(socket)) {
		return -1;
	}
	return write(socket, data, size);
}

static size_t socket_read(int socket, void *data, size_t size)
{
	return read(socket, data, size);
}

void send_response(int client_socket, const char *header, const char *body) {
    socket_write(client_socket, header, strlen(header));
    socket_write(client_socket, body, strlen(body));
}

void send_image_stream(int client_socket) {
    struct dirent *entry;
    DIR *dp = opendir("pics");
    if (dp == NULL) {
        perror("opendir");
        return;
    }

    char header[1024];
    sprintf(header, "HTTP/1.1 200 OK\r\n");
    sprintf(header + strlen(header), "Content-Type: multipart/x-mixed-replace; boundary=%s\r\n", BOUNDARY);
    sprintf(header + strlen(header), "\r\n");
    socket_write(client_socket, header, strlen(header));

    while (1) {
        rewinddir(dp);
        while ((entry = readdir(dp)) != NULL) {
            if (strstr(entry->d_name, ".jpeg") != NULL) {
                char image_path[1024];
                sprintf(image_path, "%s/%s", "pics", entry->d_name);

                FILE *file = fopen(image_path, "rb");
                if (file == NULL) {
                    perror("fopen");
                    continue;
                }

                fseek(file, 0, SEEK_END);
                long image_size = ftell(file);
                fseek(file, 0, SEEK_SET);

                char *image_data = malloc(image_size);
                fread(image_data, 1, image_size, file);
                fclose(file);

                sprintf(header, "--%s\r\n", BOUNDARY);
                sprintf(header + strlen(header), "Content-Type: image/jpeg\r\n");
                sprintf(header + strlen(header), "Content-Length: %ld\r\n", image_size);
                sprintf(header + strlen(header), "\r\n");
                socket_write(client_socket, header, strlen(header));
                socket_write(client_socket, image_data, image_size);
                socket_write(client_socket, "\r\n", 2);

                free(image_data);

                usleep(1000*1000);
            }
        }
    }

    closedir(dp);
}

typedef struct {
	int socket;
	int thread;
	uint8_t is_inited;
	uint8_t is_running;
	uint8_t try_exit;
	pthread_mutex_t lock;
} client_info_t;

typedef struct {
	int socket_fd;
	struct sockaddr_in address;
	int addrlen;

	pthread_mutex_t lock;
	int thread;
	uint8_t thread_is_started;
	uint8_t try_exit_thread;

	int client_cnt;
	int client_max;
	client_info_t *client;
} priv_t;

priv_t priv;

static int find_unused_idx(client_info_t *client, int client_max)
{
	for (int i = 0; i < client_max; i ++) {
		client_info_t *c = (client_info_t *)&client[i];
		if (!c->is_inited) {
			return i;
		}
	}
	return -1;
}

static int find_used_idx(client_info_t *client, int client_max)
{
	for (int i = 0; i < client_max; i ++) {
		client_info_t *c = (client_info_t *)&client[i];
		if (c->is_inited) {
			return i;
		}
	}
	return -1;
}

static void on_stream(client_info_t *client) {
	int client_socket = client->socket;
    struct dirent *entry;
    DIR *dp = opendir("pics");
    if (dp == NULL) {
        perror("opendir");
        return;
    }

    char header[1024];
    sprintf(header, "HTTP/1.1 200 OK\r\n");
    sprintf(header + strlen(header), "Content-Type: multipart/x-mixed-replace; boundary=%s\r\n", BOUNDARY);
    sprintf(header + strlen(header), "\r\n");
    socket_write(client_socket, header, strlen(header));

    while (1) {
		pthread_mutex_lock(&client->lock);
		if (client->try_exit || !socket_is_connected(client->socket)) {
			pthread_mutex_unlock(&client->lock);
			break;
		}
		pthread_mutex_unlock(&client->lock);

        rewinddir(dp);
        while ((entry = readdir(dp)) != NULL) {
            if (strstr(entry->d_name, ".jpeg") != NULL) {
                char image_path[1024];
                sprintf(image_path, "%s/%s", "pics", entry->d_name);

                FILE *file = fopen(image_path, "rb");
                if (file == NULL) {
                    perror("fopen");
                    continue;
                }

                fseek(file, 0, SEEK_END);
                long image_size = ftell(file);
                fseek(file, 0, SEEK_SET);

                char *image_data = malloc(image_size);
                fread(image_data, 1, image_size, file);
                fclose(file);

                sprintf(header, "--%s\r\n", BOUNDARY);
                sprintf(header + strlen(header), "Content-Type: image/jpeg\r\n");
                sprintf(header + strlen(header), "Content-Length: %ld\r\n", image_size);
                sprintf(header + strlen(header), "\r\n");
                socket_write(client_socket, header, strlen(header));
                socket_write(client_socket, image_data, image_size);
                socket_write(client_socket, "\r\n", 2);

                free(image_data);

                usleep(1000*1000);
            }
        }
    }

    closedir(dp);
}

int http_jpeg_server_create(char *host, int port, int client_num) {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return -1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
		close(server_fd);
        return -1;
    }

    address.sin_family = AF_INET;
	if (host == 0 || strlen(host) == 0) {
		address.sin_addr.s_addr = INADDR_ANY;
	} else {
		address.sin_addr.s_addr = inet_addr(host);
	}
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
		close(server_fd);
        return -1;
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
		close(server_fd);
        return -1;
    }

	if (0 != pthread_mutex_init(&priv.lock, NULL)) {
		printf("create lock failed!\r\n");
		close(server_fd);
		return -1;
	}

	priv.socket_fd = server_fd;
	memcpy(&priv.address, &address, sizeof(struct sockaddr_in));
	priv.addrlen = addrlen;
	priv.client_max = client_num;
	priv.client = (client_info_t *)malloc(priv.client_max * sizeof(client_info_t));
	if (priv.client == NULL) {
		printf("create client info failed!\r\n");
		return -1;
	}
	memset(priv.client, 0, priv.client_max * sizeof(client_info_t));
	priv.client_cnt = 0;
	return 0;
}

static void client_thread_handle(void *param)
{
	int res;
	int index = (int)param;
	pthread_mutex_lock(&priv.lock);
	client_info_t *client = (client_info_t *)&priv.client[index];
	pthread_mutex_t *lock = &client->lock;
	pthread_mutex_unlock(&priv.lock);

	pthread_mutex_lock(lock);
	int client_socket = client->socket;
	client->is_running = 1;
	pthread_mutex_unlock(lock);

	int max_sd;
	fd_set readfds;
	while (1) {
		pthread_mutex_lock(lock);
		if (client->try_exit || !socket_is_connected(client->socket)) {
			pthread_mutex_unlock(lock);
			break;
		}
		pthread_mutex_unlock(lock);

		FD_ZERO(&readfds);
		FD_SET(client_socket, &readfds);
		max_sd = client_socket;

        char buffer[1024] = {0};

		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(client_socket, &fds);

		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 100 * 1000;
		int ret = select(client_socket + 1, &fds, NULL, NULL, &tv);
		if (ret == -1) {
			printf("select error! ret:%d\r\n", ret);
			continue;
		} else if (ret == 0) {
			continue;		// timeout
		} else {
			res = socket_read(client_socket, buffer, 1024);
			if (res < 0) {
				printf("socket_read failed! res: %d\r\n", res);
				break;
			}
		}

        if (strstr(buffer, "GET /stream") != NULL) {
			on_stream(client);
        } else {
            const char *response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                                   "<html><body><h1>JPG Stream</h1><img src='/stream'></body></html>";
            send_response(client_socket, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n", response);
        }
	}

	pthread_mutex_lock(&priv.lock);
	if (client->is_inited) {
		close(client->socket);
		client->socket = -1;
		client->thread = -1;		// will exit
		client->is_running = 0;
		client->is_inited = 0;
		priv.client_cnt --;
		pthread_mutex_destroy(&client->lock);
	}
	pthread_mutex_unlock(&priv.lock);
	pthread_exit(NULL);
}

static void thread_handle(void *param)
{
	int res;
	priv_t *priv = (priv_t *)param;
	int server_fd, client_socket;

	pthread_mutex_lock(&priv->lock);
	server_fd = priv->socket_fd;
	pthread_mutex_unlock(&priv->lock);
	while (1) {
		pthread_mutex_lock(&priv->lock);
		if (priv->try_exit_thread) {
			pthread_mutex_unlock(&priv->lock);
			break;
		}
		pthread_mutex_unlock(&priv->lock);

		int client_socket;
        if ((client_socket = accept(server_fd, (struct sockaddr *)&priv->address, (socklen_t*)&priv->addrlen)) < 0) {
            perror("accept");
			sleep(1);
			continue;
        }

		pthread_mutex_lock(&priv->lock);
		int idx = find_unused_idx(priv->client, priv->client_max);
		if (idx < 0) {
			printf("can not create more client! curr:%d max:%d\r\n", priv->client_cnt, priv->client_max);
			continue;;
		}

		client_info_t *client = (client_info_t *)&priv->client[idx];
		client->socket = client_socket;
		client->try_exit = 0;
		if (0 != pthread_mutex_init(&client->lock, NULL)) {
			printf("create client lock failed!\r\n");
			continue;
		}

		if (0 != (res = pthread_create(&client->thread, NULL, client_thread_handle, idx))) {
			fprintf(stderr, "create client thread error:%s\n", strerror(res));
			pthread_mutex_destroy(&client->lock);
			continue;
		}

		// if (0 != (res = pthread_detach(&client->thread))) {
		// 	fprintf(stderr, "client thread detach error:%s\n", strerror(res));
		// 	pthread_mutex_destroy(&client->lock);
		// 	continue;
		// }
		client->is_inited = 1;
		priv->client_cnt ++;printf("idx:%d curr:%d max:%d\r\n", idx, priv->client_cnt, priv->client_max);
		pthread_mutex_unlock(&priv->lock);
	}
}

int http_jpeg_server_start() {
	pthread_t thread;

	pthread_mutex_lock(&priv.lock);
	if (priv.thread_is_started) {
		return 0;
	}

	priv.try_exit_thread = 0;
	if (0 != pthread_create(&thread, NULL, thread_handle, &priv)) {
		printf("create thread failed!\r\n");
		return -1;
	}

	priv.thread = thread;
	priv.thread_is_started = 1;
	pthread_mutex_unlock(&priv.lock);

	return 0;
}

int http_jpeg_server_stop() {

	pthread_mutex_lock(&priv.lock);
	if (!priv.thread_is_started) {
		return 0;
	}
	priv.try_exit_thread = 1;
	pthread_mutex_unlock(&priv.lock);

	for (int i = find_used_idx(priv.client, priv.client_max); i >= 0;) {
		client_info_t *client = (client_info_t *)&priv.client[i];
		pthread_mutex_lock(&client->lock);
		if (client->is_inited) {
			client->try_exit = 1;
		}
		pthread_mutex_unlock(&client->lock);
	}

	pthread_join(priv.thread, NULL);
	priv.thread_is_started = 0;
	return 0;
}


int http_jpeg_server_destory() {
	http_jpeg_server_stop();

	if (priv.client) {
		free(priv.client);
		priv.client = NULL;
	}

	if (priv.socket_fd > 0) {
		close(priv.socket_fd);
		priv.socket_fd = -1;
	}

	pthread_mutex_destroy(&priv.lock);

	return 0;
}

int main() {
#if 0
    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    while (1) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        char buffer[1024] = {0};
        socket_read(client_socket, buffer, 1024);

        if (strstr(buffer, "GET /stream") != NULL) {
            send_image_stream(client_socket);
        } else {
            const char *response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                                   "<html><body><h1>JPG Stream</h1><img src='/stream'></body></html>";
            send_response(client_socket, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n", response);
        }

        close(client_socket);
    }

    return 0;
#else
	int res = 0;
	if (0 != (res = http_jpeg_server_create("127.0.0.1", 8000, 10))) {
		printf("http_jpeg_server_create failed! res:%d\r\n",  res);
		return -1;
	}

	if (0 != (res = http_jpeg_server_start())) {
		printf("http_jpeg_server_start failed! res:%d\r\n",  res);
		return -1;
	}

	while (1) {
		printf("sleep 1\r\n");
		sleep(1);
		printf("curr:%d max:%d\r\n", priv.client_cnt, priv.client_max);
	}

	if (0 != (res = http_jpeg_server_stop())) {
		printf("http_jpeg_server_start failed! res:%d\r\n",  res);
		return -1;
	}

	if (0 != (res = http_jpeg_server_destory())) {
		printf("http_jpeg_server_start failed! res:%d\r\n",  res);
		return -1;
	}

	return 0;
#endif
}
