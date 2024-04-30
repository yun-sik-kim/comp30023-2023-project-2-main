#include "rpc.h"
#include <stdlib.h>

/* Added header files */
#include <stdio.h> 
#include <string.h> 
#include <unistd.h> 
#include <errno.h> 
#include <netdb.h>
#include <arpa/inet.h> 
#include <sys/types.h> 
#include <sys/wait.h> 
#include <signal.h>
#include <stdint.h>

#define MAX_FUNCTIONS 20
#define FIND_COMMAND 0
#define CALL_COMMAND 1
#define NO_FUNCTION -1
#define MAX_SERIALIZED_LEN 100016
#define MAX_FUNC_NAME 1000

typedef uint64_t fixed_size_t; /* fix the size_t size */

int create_listening_socket(char* service);
rpc_data* make_rpc_data(int data1, size_t data2_len, void* data2);
unsigned char* serialize_rpc_data(fixed_size_t command_flag, fixed_size_t func_fd, rpc_data* data);
rpc_data* deserialize_rpc_data(unsigned char* serialized_data, fixed_size_t* out_command_flag, fixed_size_t* out_func_fd);
int write_all(int sockfd, unsigned char* buf, size_t len);
int read_all(int sockfd, unsigned char* buf, size_t len);
uint64_t htonll(uint64_t value);
uint64_t ntohll(uint64_t value);


struct rpc_handle {
    fixed_size_t func_fd;
    char* name;
    rpc_handler rpc_handler;
};

struct rpc_server {
    int port; /* for reability. use htons() ntohs() */
    int sockfd;
    int new_fd;
    struct sockaddr_in6 client_addr; /* cleint sockets address information */
    socklen_t client_addr_size;
    struct rpc_handle function_list[MAX_FUNCTIONS];
    int num_functions;
};

struct rpc_client {
    char ip_addr[INET6_ADDRSTRLEN]; /* for reability. */
    int port;                       /* for reability. use htons() ntohs() */
    int sockfd, n, s;
    struct addrinfo hints, *servinfo, *rp;
	unsigned char buffer[MAX_SERIALIZED_LEN];
};

rpc_server *rpc_init_server(int port) {
    /* check if input port number is valid number */
    if (port < 1 || port > 65535) {
        return NULL;
    } 

    char port_str[6] = {0}; /* Maximum length of a port number string (5 + \0) */
    rpc_server* server = NULL;

    /* allocate memory for server struct */
    /* !!! NOTE server will NOT be free() !!! */
    server = (rpc_server*) malloc(sizeof(rpc_server));
    if (server == NULL) {
        perror("Failed to allocate memory for the server");
        return NULL;
    }
    memset(server, 0, sizeof(rpc_server));
    
    /* init server struct */
    server->port = port;
    sprintf(port_str, "%d", port);
    server->sockfd = create_listening_socket(port_str);
    if (server->sockfd == -1) {
        return NULL;
    }
    server->new_fd = 0;
    server->num_functions = 0;

    return server;
}

/* this function assumes handler exists in server, else server would not be compiled */
int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
    /* check if any arguemnts are NULL */
    if (!srv || !name || !handler) {
        return -1;
    }
    /* check if number of functions saved excceed MAX_FUNCTIONS */
    if (srv->num_functions > MAX_FUNCTIONS) {
        return -1;
    }

    /* if funtion with same name exists, delete old and name latest one to that name */
    for (int i = 0; i < srv->num_functions; i++) {
        if (strcmp(srv->function_list[i].name, name) == 0) {
            /* Replace the existing function with the new one */
            srv->function_list[i].rpc_handler = handler;
            return 0; /* Success */ 
        }
    }

    /* If the function does not exist, add it to the list */ 
    srv->function_list[srv->num_functions].func_fd = srv->num_functions;
    /* !!! NOTE server will NOT be free() !!! */
    srv->function_list[srv->num_functions].name = malloc(sizeof(char) * (strlen(name) + 1));
    memset(srv->function_list[srv->num_functions].name, 0, sizeof(char) * (strlen(name) + 1));
    strcpy(srv->function_list[srv->num_functions].name, name);  
    srv->function_list[srv->num_functions].rpc_handler = handler;

    srv->num_functions++;

    return 0; /* Success */ 
}

void rpc_serve_all(rpc_server *srv) {
    while (1) {
        // Listen on socket - means we're ready to accept connections,
        // incoming connection requests will be queued, man 3 listen
        if (listen(srv->sockfd, 5) < 0) {
            perror("listen");
            exit(EXIT_FAILURE);
        } 
        // Accept a connection - blocks(server) until a connection is ready to be accepted
        // Get back a new file descriptor to communicate on
        srv->client_addr_size = sizeof(srv->client_addr);
        srv->new_fd = accept(srv->sockfd, (struct sockaddr*)&srv->client_addr, &srv->client_addr_size);
        if (srv->new_fd < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        } 

        /* continue the conversation */
        while (1) {
            /* for receive*/
            rpc_data* client_rpc_data = NULL;
            unsigned char* recv_buffer = NULL;
            /* for reply */
            rpc_data* data = NULL;
            unsigned char* serialized_data = NULL;

            int connection_state = 0;

            recv_buffer = (unsigned char*)malloc(MAX_SERIALIZED_LEN);
            memset(recv_buffer, 0, MAX_SERIALIZED_LEN);

            /* first, read serialized message from client */
            connection_state = read_all(srv->new_fd, recv_buffer, MAX_SERIALIZED_LEN);
            if (connection_state == 0) {
                break;
            }

            if (connection_state == -1) {
                printf("Request client to send data again!\n");
                /* ask client to send data again */
            }

            /* convert to rpc_data */
            fixed_size_t read_command;
            fixed_size_t read_func_fd;
            client_rpc_data = deserialize_rpc_data(recv_buffer, &read_command, &read_func_fd);
            if (client_rpc_data == NULL) {
                fprintf(stderr, "problem while deserializing\n");
            }

            /* check whether rpc_call() or rpc_find() */
            if (read_command == FIND_COMMAND) {
                /* if: name is found, write back func_file descriptor */
                for (int i = 0; i < srv->num_functions; i++) {
                    if (strcmp(srv->function_list[i].name, (char*)client_rpc_data->data2) == 0) {
                        fixed_size_t unify_func_fd = (fixed_size_t)srv->function_list[i].func_fd;

                        /* write back serialized_data with func_fd number, with dummy RPC_DATA */
                        data = make_rpc_data(0, 0, NULL);
                        serialized_data = serialize_rpc_data(FIND_COMMAND, unify_func_fd, data);
                        if (serialized_data == NULL) {
                            fprintf(stderr, "error in malloc() serialzed_data\n");
                        }   

                        /* write message to server */ 
                        write_all(srv->new_fd, serialized_data, MAX_SERIALIZED_LEN);

                        goto get_ready_to_read;
                    }
                }
                /* else if: no functions found, write back NO_FUNCTION */
                /* just write back func_fd number, nothing else. make dummy RPC_DATA */
                data = make_rpc_data(0, 0, NULL);
                serialized_data = serialize_rpc_data(FIND_COMMAND, NO_FUNCTION, data);
                if (serialized_data == NULL) {
                    fprintf(stderr, "error in serialzed_data\n");
                } 
                /* write message to server */ 
                write_all(srv->new_fd, serialized_data, MAX_SERIALIZED_LEN);

                goto get_ready_to_read;


            } else if (read_command == CALL_COMMAND) {
                /* call the requested function from server struct */
                for (int i = 0; i < srv->num_functions; i++) {
                    if (srv->function_list[i].func_fd == read_func_fd) {
                        /* find the function pointer from the rpc_server-> function_list 
                        and input client_rpc_data 
                        and recevie output in rpc_data format */
                        rpc_handler func = srv->function_list[i].rpc_handler;

                        rpc_data* result = func(client_rpc_data);
                        
                        /* check rpc_data is NULL*/
                        if (!result) {
                            rpc_data* dummy_data = NULL;
                            dummy_data = make_rpc_data(0, 0, NULL);

                            serialized_data = serialize_rpc_data(CALL_COMMAND, NO_FUNCTION, dummy_data);
                            if (serialized_data == NULL) {
                                fprintf(stderr, "error in serialzed_data\n");
                            } 
                            rpc_data_free(dummy_data);
                            goto send_result;
                        }
                        /* check inconsistent data2 & data2_len */
                        if ( (result->data2_len > 0) && (result->data2 == NULL) ) {
                            rpc_data* dummy_data = NULL;
                            dummy_data = make_rpc_data(0, 0, NULL);

                            serialized_data = serialize_rpc_data(CALL_COMMAND, NO_FUNCTION, dummy_data);
                            if (serialized_data == NULL) {
                                fprintf(stderr, "error in serialzed_data\n");
                            } 
                            rpc_data_free(dummy_data);
                            goto send_result;
                        }
                        if ( (result->data2_len == 0) && (result->data2 != NULL) ) {
                            rpc_data* dummy_data = NULL;
                            dummy_data = make_rpc_data(0, 0, NULL);

                            serialized_data = serialize_rpc_data(CALL_COMMAND, NO_FUNCTION, dummy_data);
                            if (serialized_data == NULL) {
                                fprintf(stderr, "error in serialzed_data\n");
                            } 
                            rpc_data_free(dummy_data);
                            goto send_result;
                        }

                        data = make_rpc_data(result->data1, result->data2_len, result->data2);
                        serialized_data = serialize_rpc_data(CALL_COMMAND, read_func_fd, data);
                        if (serialized_data == NULL) {
                            fprintf(stderr, "error in serialzed_data\n");
                        } 

                        send_result:
                            write_all(srv->new_fd, serialized_data, MAX_SERIALIZED_LEN);

                            goto get_ready_to_read;;
                    }
                }
                /* else no functions found, write back NO_FUNCTION */
                /* just write back func_fd number, nothing else. make dummy RPC_DATA */
                data = make_rpc_data(0, 0, NULL);
                serialized_data = serialize_rpc_data(FIND_COMMAND, NO_FUNCTION, data);
                if (serialized_data == NULL) {
                    fprintf(stderr, "error in serialzed_data\n");
                }   
                write_all(srv->new_fd, serialized_data, MAX_SERIALIZED_LEN);

                goto get_ready_to_read;;
            } 

            get_ready_to_read:
                /* Free all malloc() data */
                if (recv_buffer != NULL) {
                    free(recv_buffer);
                    recv_buffer = NULL;
                }
                if (client_rpc_data != NULL) {
                    rpc_data_free(client_rpc_data);
                }
                if (data != NULL) {
                    rpc_data_free(data);
                }
                if (serialized_data != NULL) {
                    free(serialized_data);
                    serialized_data = NULL;
                }
        }
    }

}

/* used some code from practical */
rpc_client *rpc_init_client(char *addr, int port) {

    char port_str[6]; /* Maximum length of a port number string (5 + \0) */
    rpc_client* client = NULL;
    /* check if input port number is valid number */
    if (port < 1 || port > 65535) {
        return NULL;
    } 

    /* allocate memory for client struct*/
    /* !!! NOTE: free client using rpc_close_client() !!! */
    client = (rpc_client*) malloc(sizeof(rpc_client));
    if (client == NULL) {
        perror("Failed to allocate memory for the client");
        return NULL;
    }
    memset(client, 0, sizeof(rpc_client));

    /* init client struct */
    strncpy(client->ip_addr, addr, sizeof(client->ip_addr)-1);
    client->port = port;
    sprintf(port_str, "%d", port);

	/* create address */ 
	memset(&client->hints, 0, sizeof client->hints);
	client->hints.ai_family = AF_INET6;
	client->hints.ai_socktype = SOCK_STREAM;

	/* gethostbyname(3) and getservbyname(3) functions into a single interface */ 
	client->s = getaddrinfo(addr, port_str, &client->hints, &client->servinfo);
	if (client->s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(client->s));
		exit(EXIT_FAILURE);
	}

	for (client->rp = client->servinfo; client->rp != NULL; client->rp = client->rp->ai_next) {
        client->sockfd = socket(client->rp->ai_family, client->rp->ai_socktype, client->rp->ai_protocol);
		if (client->sockfd == -1) {
            continue;
        }
		if (connect(client->sockfd, client->rp->ai_addr, client->rp->ai_addrlen) != -1) {
			break; // success
        }
		close(client->sockfd);
	}
	if (client->rp == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(client->servinfo);

    return client;
}

rpc_handle *rpc_find(rpc_client *cl, char *name) {
    /* Check if any arguemnts are NULL or or false */
    if (!cl || !name) {
        return NULL;
    }

    /* for send */
    rpc_data* send_data = NULL;
    unsigned char* serialized_data = NULL;
    /* for receive */
    rpc_data* recv_data = NULL;
    unsigned char* recv_buffer = NULL;

    recv_buffer = (unsigned char*)malloc(MAX_SERIALIZED_LEN);
    memset(recv_buffer, 0, MAX_SERIALIZED_LEN);
    
    /* create rpc_data */
    send_data = make_rpc_data(0, strlen(name) + 1, name); /* +1 for null character */
    /* serialize rpc_data */
    serialized_data = serialize_rpc_data(FIND_COMMAND, NO_FUNCTION, send_data);
    if (serialized_data == NULL) {
        free(recv_buffer);
        recv_buffer = NULL;
        return NULL;
    }
    
    /* write message to server */ 
    write_all(cl->sockfd, serialized_data, MAX_SERIALIZED_LEN);

    /* read message from server */
    read_all(cl->sockfd, recv_buffer, MAX_SERIALIZED_LEN);

    fixed_size_t read_command;
    fixed_size_t read_func_fd;
    recv_data = deserialize_rpc_data(recv_buffer, &read_command, &read_func_fd);
    
    /* if nothing found, return NULL */
    if(read_func_fd == NO_FUNCTION) {
        if (recv_buffer != NULL) {
        free(recv_buffer);
        recv_buffer = NULL;
        }
        if (send_data != NULL) {
            rpc_data_free(send_data);
        }
        if (serialized_data != NULL) {
            free(serialized_data);
            serialized_data = NULL;
        }
        if (recv_data != NULL) {
            rpc_data_free(recv_data);
        }
        return NULL;
    }

    rpc_handle* handle = NULL;
    handle = (rpc_handle*)malloc(sizeof(rpc_handle));
    if (handle == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    memset(handle, 0, sizeof(rpc_handle));

    /* just need func_fd for client */
    handle->func_fd = read_func_fd;
    handle->name = NULL;
    handle->rpc_handler = NULL;


    /* Free data */
    if (recv_buffer != NULL) {
        free(recv_buffer);
        recv_buffer = NULL;
    }
    if (send_data != NULL) {
        rpc_data_free(send_data);
    }
    if (serialized_data != NULL) {
        free(serialized_data);
        serialized_data = NULL;
    }
    if (recv_data != NULL) {
        rpc_data_free(recv_data);
    }
    
    return handle;
}

rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    /* Check if any arguemnts are NULL or or false */
    if (!cl || !h || !payload) {
        return NULL;
    }

    /* for send */
    unsigned char* serialized_data = NULL;
    /* for receive */
    rpc_data* recv_data = NULL;
    unsigned char* recv_buffer = NULL;

    recv_buffer = (unsigned char*)malloc(MAX_SERIALIZED_LEN);
    if (recv_buffer == NULL) {
        return NULL;
    }
    memset(recv_buffer, 0, MAX_SERIALIZED_LEN);

    /* convert rpc_data to serialised data & set CALL_COMMAND */
    serialized_data = serialize_rpc_data(CALL_COMMAND, h->func_fd, payload);
    if (serialized_data == NULL) {
        return NULL;
    }

    /* write message to server */ 
    write_all(cl->sockfd, serialized_data, MAX_SERIALIZED_LEN);

    /* read message from server */
    read_all(cl->sockfd, recv_buffer, MAX_SERIALIZED_LEN);

    fixed_size_t read_command;
    fixed_size_t read_func_fd;
    recv_data = deserialize_rpc_data(recv_buffer, &read_command, &read_func_fd);

    /* if nothing found, return NULL */
    if(read_func_fd == NO_FUNCTION) {
        return NULL;
    }

    // rpc_data_free(recv_data);
    free(recv_buffer);
    recv_buffer = NULL;
    free(serialized_data);
    serialized_data = NULL;

    return recv_data;
}

void rpc_close_client(rpc_client *cl) {
    if (cl->sockfd != -1) {
        close(cl->sockfd);
        cl->sockfd = -1;
    }

    if (cl == NULL) {
        return;
    }

    free(cl);
}

void rpc_data_free(rpc_data *data) {
    if (data->data2 != NULL) {
        free(data->data2);
        data->data2 = NULL;
    }
    if (data != NULL) {
        free(data);
        data = NULL;
    }
    return;
}

/* code from practical */
/* !!! CHECK MEMORY LEAK !!!  */
int create_listening_socket(char* service) {
	int re, s, sockfd;
	struct addrinfo hints, *res;

	// Create address we're going to listen on (with given port number)
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;       // IPv6
	hints.ai_socktype = SOCK_STREAM; // Connection-mode byte streams
	hints.ai_flags = AI_PASSIVE;     // for bind, listen, accept
	// node (NULL means any interface), service (port), hints, res
	s = getaddrinfo(NULL, service, &hints, &res);
	if (s != 0) {
		// fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		// exit(EXIT_FAILURE);
        return -1;
	}

	// Create socket
	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sockfd < 0) {
		// perror("socket");
		// exit(EXIT_FAILURE);
        return -1;
	}

	// Reuse port if possible
	re = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &re, sizeof(int)) < 0) {
		perror("setsockopt");
		//exit(EXIT_FAILURE);
        return -1;
	}
	// Bind address to the socket
	if (bind(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
		// perror("bind");
		// exit(EXIT_FAILURE);
        return -1;
	}
	freeaddrinfo(res);

	return sockfd;
}


int write_all(int sockfd, unsigned char* buf, size_t len) {
    ssize_t total_sent = 0;
    ssize_t sent = 0;

    while (total_sent < len) {
        sent = write(sockfd, buf + total_sent, len - total_sent);
        if (sent == -1) {
            fprintf(stderr, "write error\n");
            return -1;
        } else if (sent == 0){
            return 0;
        }
        total_sent += sent;
    }

    return 1;
}

int read_all(int sockfd, unsigned char* buf, size_t len) {
    ssize_t total_received = 0;
    ssize_t received;

    while (total_received < len) {
        received = read(sockfd, buf + total_received, len - total_received);
        if (received == -1) {
            fprintf(stderr, "write error while writeing from server\n");
            return -1;
        } else if (received == 0) {
            return 0;
        }
        total_received += received;
    }

    return 1;
}

rpc_data* make_rpc_data(int data1, size_t data2_len, void* data2) {
    rpc_data* data = NULL;
    // Allocate memory for rpc_data
    data = (rpc_data*)malloc(MAX_SERIALIZED_LEN);
    if(data == NULL) {
        return NULL;
    }
    memset(data, 0, MAX_SERIALIZED_LEN);

    data->data1 = data1;

    // Allocate memory for data2
    data->data2_len = data2_len;
    if (data->data2_len == 0) {
        data->data2 = NULL;

        return data;
    } else {
        data->data2 = malloc(data2_len);
        if(data->data2 == NULL) {
            free(data);
            return NULL;
        }
        memset(data->data2, 0, data2_len);
        memcpy(data->data2, data2, data->data2_len);
    }

    return data;
}

/* the protocol will be consist of 8byte(COMMAND_FLAG) + 8byte(FUNC_FILE_DESCRIPTOR) + 1000byte(RPC_DATA) */
unsigned char* serialize_rpc_data(fixed_size_t command_flag, fixed_size_t func_fd, rpc_data* data) {
    if (!data) {
        return NULL;
    }
    /* check inconsistent data2 & data2_len */
    if ( (data->data2_len > 0) && (data->data2 == NULL) ) {
        return NULL;
    }
    if ( (data->data2_len == 0) && (data->data2 != NULL) ) {
        return NULL;
    }

    unsigned char* serialized_data = NULL;
    fixed_size_t pos = 0;

    /* create serialise container */
    serialized_data = (unsigned char*)malloc(MAX_SERIALIZED_LEN); /* !!!F need to free this !!! */
    if (serialized_data == NULL) {
        return NULL; // malloc failed
    }
    memset(serialized_data, 0, MAX_SERIALIZED_LEN);

    /* serialize command flag */
    memcpy(serialized_data + pos, &command_flag, sizeof(command_flag));
    pos += sizeof(command_flag);

    /* serialize function file descriptor */
    memcpy(serialized_data + pos, &func_fd, sizeof(func_fd));
    pos += sizeof(func_fd);

    /* serialize rpc_data  */
    /* serialize data1 */
    fixed_size_t data1 = htonll( (fixed_size_t)data->data1 );

    memcpy(serialized_data + pos, &data1, sizeof(data1));
    pos += sizeof(data1);

    /* serialize data2_len */
    fixed_size_t data2_len = htonll ( (fixed_size_t)data->data2_len );
    memcpy(serialized_data + pos, &data2_len, sizeof(data2_len));
    pos += sizeof(data2_len);

    /* serialize data2 */
    if (data->data2_len == 0) {
        return serialized_data;
    } else {
        /* copy the provided data to data2 */ 
        memcpy(serialized_data + pos, data->data2, data->data2_len);
        pos += data2_len;
        pos += data->data2_len;
    }

    return serialized_data;
}

rpc_data* deserialize_rpc_data(unsigned char* serialized_data, fixed_size_t* out_command_flag, fixed_size_t* out_func_fd) {
    if (!serialized_data || !out_command_flag || !out_func_fd) {
        return NULL;
    }
    
    rpc_data* data = NULL; 
    fixed_size_t pos = 0;

    data = (rpc_data*)malloc(MAX_SERIALIZED_LEN);
    if (data == NULL) {
        return NULL;
    }
    memset(data, 0, MAX_SERIALIZED_LEN);

    /* deserialize command flag */ 
    memcpy(out_command_flag, serialized_data + pos, sizeof(fixed_size_t));
    pos += sizeof(fixed_size_t);

    /* deserialize function file descriptor */ 
    memcpy(out_func_fd, serialized_data + pos, sizeof(fixed_size_t));
    pos += sizeof(fixed_size_t);

    /* deserialize rpc_data */ 
    /* deserialize data1 */ 
    fixed_size_t data1;
    memcpy(&data1, serialized_data + pos, sizeof(data1));
    data1 = ntohll(data1);
    data->data1 = (int)data1; // casting back to int (if int is different size from int32_t on server)
    pos += sizeof(data1);

    /* deserialize data2_len */ 
    fixed_size_t data2_len;
    memcpy(&data2_len, serialized_data + pos, sizeof(data2_len));
    data2_len = ntohll(data2_len); 
    data->data2_len = (size_t)data2_len; // casting back to size_t (if size_t is different size from uint32_t on server)
    pos += sizeof(data2_len);

    /* if data2_len is 0, set data2 as NULL */
    if (data->data2_len == 0) {
        data->data2 = NULL;
        
        return data;
    } else {
        /* allocate memory for received_data.data2 */
        data->data2 = malloc(data->data2_len);
        if(data->data2 == NULL) {
            free(data);
            return NULL;
        }
        memset(data->data2, 0, data->data2_len);
        /* copy the provided data to data2 */ 
        memcpy(data->data2, serialized_data + pos, data2_len);
        pos += data2_len;
    }

    return data;
}

uint64_t htonll(uint64_t value) {
    /* htonl(1) will return 1 in a big endian system. */
    if (htonl(1) == 1) {
        return value;
    } else {
        return ((uint64_t)htonl(value & 0xFFFFFFFF) << 32) | htonl(value >> 32);
    }
}

uint64_t ntohll(uint64_t value) {
    /* htonl(1) will return 1 in a big endian system. */
    if (htonl(1) == 1) {
        return value;
    } else {
        return ((uint64_t)ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
    }
}