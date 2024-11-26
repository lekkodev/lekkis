#include "server.h"

#ifdef __linux__

#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "vm.h"
#include "assembler.h"
#include "counter.h"

#define PORT 12345
#define BACKLOG 256
#define BUFFER_SIZE 4096 * 8
#define MAX_EVENTS 128
#define NUM_THREADS 8

uint8_t *program;
uint8_t func_name[] = {0x03, 0x00, 'f', 'o', 'o'};
uint8_t arguments[] = {
    0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 'M', 'e', 'e', 'p'
};

__thread char read_buffer[BUFFER_SIZE];
__thread char output_buffer[BUFFER_SIZE] = {0};
__thread struct iovec iov[2];
__thread size_t bytes_read, output_size;

typedef struct thread_info {
    int thread_id;
    int server_fd;
    struct io_uring ring;
    char *fixed_buffers;
    HashTable **hash_tables;
} thread_info;
/* THIS WORKS - the stuff below is supposed to be faster, but doens't work */

void* worker_thread(void* arg) {
    struct thread_info* tinfo = (struct thread_info*) arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&tinfo->ring);
        if (!sqe) {
            fprintf(stderr, "Failed to get submission queue entry for accept\n");
            continue;
        }
        io_uring_prep_accept(sqe, tinfo->server_fd, (struct sockaddr*)&client_addr, &client_len, 0);
        if (io_uring_submit(&tinfo->ring) < 0) {
            perror("io_uring_submit (accept)");
            continue;
        }
        struct io_uring_cqe *cqe;
        if (io_uring_wait_cqe(&tinfo->ring, &cqe) < 0) {
            perror("io_uring_wait_cqe (accept)");
            continue;
        }
        int client_fd = cqe->res;
        io_uring_cqe_seen(&tinfo->ring, cqe);
        if (client_fd < 0) {
            fprintf(stderr, "Accept failed: %s\n", strerror(-client_fd));
            continue;
        }
        while (1) {
            sqe = io_uring_get_sqe(&tinfo->ring);
            if (!sqe) {
                fprintf(stderr, "Failed to get submission queue entry for read\n");
                break;
            }
            io_uring_prep_read_fixed(sqe, client_fd, tinfo->fixed_buffers, BUFFER_SIZE, 0, 0);
            if (io_uring_submit(&tinfo->ring) < 0) {
                perror("io_uring_submit (read)");
                break;
            }
            if (io_uring_wait_cqe(&tinfo->ring, &cqe) < 0) {
                perror("io_uring_wait_cqe (read)");
                break;
            }
            int bytes_read = cqe->res;
            io_uring_cqe_seen(&tinfo->ring, cqe);
            if (bytes_read <= 0) {
                if (bytes_read < 0) {
                    fprintf(stderr, "Read error: %s\n", strerror(-bytes_read));
                }
                break;
            }
            runFunction(program, (uint8_t *)tinfo->fixed_buffers, &output_size, arguments, func_name);

            const uint8_t* key = "abc123def876qqabc123def876qq";
            Bucket* bucket = hash_table_find_or_create(tinfo->hash_tables[tinfo->thread_id], key, 1234);
            bucket->counters[12]++;

            uint64_t total = 0;
            for (int i = 0; i < NUM_THREADS; i++) {
                total += hash_table_find_or_create(tinfo->hash_tables[i], key, 1234)->counters[12];
            }
            sqe = io_uring_get_sqe(&tinfo->ring);
            if (!sqe) {
                fprintf(stderr, "Failed to get submission queue entry for write\n");
                break;
            }
            io_uring_prep_write_fixed(sqe, client_fd, tinfo->fixed_buffers, output_size, 0, 0);
            if (io_uring_submit(&tinfo->ring) < 0) {
                perror("io_uring_submit (write)");
                break;
            }
            if (io_uring_wait_cqe(&tinfo->ring, &cqe) < 0) {
                perror("io_uring_wait_cqe (write)");
                break;
            }
            io_uring_cqe_seen(&tinfo->ring, cqe);
        }
        close(client_fd);
    }

    return NULL;
}


#define BATCH_SIZE 2           // Number of operations to batch per submission/completion cycle
#define MAX_CLIENTS 1024        // Maximum clients per thread
#define ACCEPT_OP 1             // Identifier for accept operations
#define READ_OP 2               // Identifier for read operations
#define WRITE_OP 3              // Identifier for write operations
/*
void* worker_thread(void* arg) {
    struct thread_info* tinfo = (struct thread_info*) arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Allocate client states
    struct client_state {
        int fd;
        size_t buffer_offset;
    } *clients = calloc(MAX_CLIENTS, sizeof(struct client_state));

    // Initialize all client states to invalid
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
    }

    struct io_uring_cqe* cqe_batch[BATCH_SIZE]; // Batch CQE storage

    // Pre-fill a multishot accept SQE
    struct io_uring_sqe* sqe = io_uring_get_sqe(&tinfo->ring);
    if (sqe) {
        io_uring_prep_multishot_accept(sqe, tinfo->server_fd, (struct sockaddr*)&client_addr, &client_len, 0);
        sqe->user_data = ACCEPT_OP;
    }

    if (io_uring_submit(&tinfo->ring) < 0) {
        perror("io_uring_submit (accept)");
        free(clients);
        return NULL;
    }

    printf("Worker thread %d initialized and waiting for events.\n", tinfo->thread_id);

    while (1) {
        // Process CQEs in batches
        int num_cqes = io_uring_peek_batch_cqe(&tinfo->ring, cqe_batch, BATCH_SIZE);
        if (num_cqes) printf("Num CQEs: %d\n", num_cqes);
        for (int i = 0; i < num_cqes; i++) {
            struct io_uring_cqe* cqe = cqe_batch[i];
            if (cqe->res < 0) { // Handle errors
                fprintf(stderr, "IO operation failed: %s\n", strerror(-cqe->res));
                continue;
            }

            if (cqe->user_data == ACCEPT_OP) {
                int client_fd = cqe->res;
                if (client_fd >= MAX_CLIENTS) {
                    fprintf(stderr, "Exceeded max clients limit\n");
                    close(client_fd);
                    continue;
                }
                printf("Accepted new client: %d\n", client_fd);

                int flags = fcntl(client_fd, F_GETFL, 0);
                if (flags < 0 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                    perror("fcntl: setting non-blocking mode");
                    close(client_fd);
                    continue;
                }

                struct client_state* client = &clients[client_fd];
                client->fd = client_fd;
                client->buffer_offset = 0;

                // Prepare a fixed-buffer read operation for the new client
                sqe = io_uring_get_sqe(&tinfo->ring);
                if (sqe) {
                    printf("Submitting READ_OP for client_fd: %d\n", client_fd);
                    io_uring_prep_read_fixed(
                        sqe,
                        client_fd,
                        tinfo->fixed_buffers + client->buffer_offset,
                        BUFFER_SIZE,
                        0,  // Offset
                        0   // Fixed buffer index
                    );
                    sqe->user_data = ((uint64_t)READ_OP << 32) | client_fd;
                    io_uring_submit(&tinfo->ring);
                }
            } else if ((cqe->user_data >> 32) == READ_OP) {
                int client_fd = cqe->user_data & 0xFFFFFFFF;
                struct client_state* client = &clients[client_fd];

                printf("READ_OP result: %d for client_fd: %d\n", cqe->res, client_fd);
                if (cqe->res <= 0) { // Client disconnected or error
                    fprintf(stderr, "Client disconnected or read error on client_fd: %d, result: %d\n", client_fd, cqe->res);
                    close(client->fd);
                    client->fd = -1;
                    continue;
                }

                // Process the received data
                uint8_t* input_buffer = (uint8_t*)(tinfo->fixed_buffers + client->buffer_offset);

                printf("Calling runFunction for client_fd: %d\n", client_fd);
                runFunction(
                    program,       // User-provided program or context
                    input_buffer,  // Input data
                    &output_size,  // Output size
                    arguments,     // Function arguments
                    func_name      // Function name
                );


                // Prepare a write operation to send the response
                sqe = io_uring_get_sqe(&tinfo->ring);
                if (sqe) {
                    printf("Submitting WRITE_OP for client_fd: %d\n", client_fd);
                    io_uring_prep_write_fixed(
                        sqe,
                        client_fd,
                        tinfo->fixed_buffers + client->buffer_offset,
                        output_size,
                        0,  // Offset
                        0   // Fixed buffer index
                    );
                    sqe->user_data = ((uint64_t)WRITE_OP << 32) | client_fd;
                    io_uring_submit(&tinfo->ring);
                }
            } else if ((cqe->user_data >> 32) == WRITE_OP) {
                int client_fd = cqe->user_data & 0xFFFFFFFF;
                struct client_state* client = &clients[client_fd];

                printf("WRITE_OP completed for client_fd: %d\n", client_fd);

                // Prepare the next read operation
                sqe = io_uring_get_sqe(&tinfo->ring);
                if (sqe) {
                    printf("Submitting next READ_OP for client_fd: %d\n", client_fd);
                    io_uring_prep_read_fixed(
                        sqe,
                        client_fd,
                        tinfo->fixed_buffers + client->buffer_offset,
                        BUFFER_SIZE,
                        0,  // Offset
                        0   // Fixed buffer index
                    );
                    sqe->user_data = ((uint64_t)READ_OP << 32) | client_fd;
                    io_uring_submit(&tinfo->ring);
                }
            }
        }

        // Advance CQEs after processing
        io_uring_cq_advance(&tinfo->ring, num_cqes);
    }

    free(clients);
    return NULL;
}
*/

// Define debug print macros
#ifdef DEBUG
    #define DEBUG_PRINT(...) printf(__VA_ARGS__)
    #define DEBUG_ERR_PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
    #define DEBUG_PRINT(...)
    #define DEBUG_ERR_PRINT(...)
#endif
/*
void* worker_thread(void* arg) {
    struct thread_info* tinfo = (struct thread_info*) arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Allocate client states
    struct client_state {
        int fd;
        size_t buffer_offset;
    } *clients = calloc(MAX_CLIENTS, sizeof(struct client_state));

    // Initialize all client states to invalid
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
    }

    struct io_uring_cqe* cqe_batch[BATCH_SIZE]; // Batch CQE storage

    // Pre-fill a multishot accept SQE
    struct io_uring_sqe* sqe = io_uring_get_sqe(&tinfo->ring);
    if (sqe) {
        io_uring_prep_multishot_accept(sqe, tinfo->server_fd, (struct sockaddr*)&client_addr, &client_len, 0);
        sqe->user_data = ACCEPT_OP;
    }

    if (io_uring_submit(&tinfo->ring) < 0) {
        perror("io_uring_submit (accept)");
        free(clients);
        return NULL;
    }

    DEBUG_PRINT("Worker thread %d initialized and waiting for events.\n", tinfo->thread_id);

    while (1) {
        // Process CQEs in batches
        int num_cqes = io_uring_peek_batch_cqe(&tinfo->ring, cqe_batch, BATCH_SIZE);
        if (num_cqes) DEBUG_PRINT("Num CQEs: %d\n", num_cqes);
        for (int i = 0; i < num_cqes; i++) {
            struct io_uring_cqe* cqe = cqe_batch[i];
            if (cqe->res < 0) { // Handle errors
                DEBUG_ERR_PRINT("IO operation failed: %s\n", strerror(-cqe->res));
                continue;
            }

            if (cqe->user_data == ACCEPT_OP) {
                int client_fd = cqe->res;
                if (client_fd >= MAX_CLIENTS) {
                    DEBUG_ERR_PRINT("Exceeded max clients limit\n");
                    close(client_fd);
                    continue;
                }
                DEBUG_PRINT("Accepted new client: %d\n", client_fd);

                int flags = fcntl(client_fd, F_GETFL, 0);
                if (flags < 0 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                    perror("fcntl: setting non-blocking mode");
                    close(client_fd);
                    continue;
                }

                struct client_state* client = &clients[client_fd];
                client->fd = client_fd;
                client->buffer_offset = 0;

                // Prepare a fixed-buffer read operation for the new client
                sqe = io_uring_get_sqe(&tinfo->ring);
                if (sqe) {
                    DEBUG_PRINT("Submitting READ_OP for client_fd: %d\n", client_fd);
                    io_uring_prep_read_fixed(
                        sqe,
                        client_fd,
                        tinfo->fixed_buffers + client->buffer_offset,
                        BUFFER_SIZE,
                        0,  // Offset
                        0   // Fixed buffer index
                    );
                    sqe->user_data = ((uint64_t)READ_OP << 32) | client_fd;
                    io_uring_submit(&tinfo->ring);
                }
            } else if ((cqe->user_data >> 32) == READ_OP) {
                int client_fd = cqe->user_data & 0xFFFFFFFF;
                struct client_state* client = &clients[client_fd];

                DEBUG_PRINT("READ_OP result: %d for client_fd: %d\n", cqe->res, client_fd);
                if (cqe->res <= 0) { // Client disconnected or error
                    DEBUG_ERR_PRINT("Client disconnected or read error on client_fd: %d, result: %d\n", client_fd, cqe->res);
                    close(client->fd);
                    client->fd = -1;
                    continue;
                }

                // Process the received data
                uint8_t* input_buffer = (uint8_t*)(tinfo->fixed_buffers + client->buffer_offset);

                DEBUG_PRINT("Calling runFunction for client_fd: %d\n", client_fd);
                runFunction(
                    program,       // User-provided program or context
                    input_buffer,  // Input data
                    &output_size,  // Output size
                    arguments,     // Function arguments
                    func_name      // Function name
                );


                // Prepare a write operation to send the response
                sqe = io_uring_get_sqe(&tinfo->ring);
                if (sqe) {
                    DEBUG_PRINT("Submitting WRITE_OP for client_fd: %d\n", client_fd);
                    io_uring_prep_write_fixed(
                        sqe,
                        client_fd,
                        tinfo->fixed_buffers + client->buffer_offset,
                        output_size,
                        0,  // Offset
                        0   // Fixed buffer index
                    );
                    sqe->user_data = ((uint64_t)WRITE_OP << 32) | client_fd;
                    io_uring_submit(&tinfo->ring);
                }
            } else if ((cqe->user_data >> 32) == WRITE_OP) {
                int client_fd = cqe->user_data & 0xFFFFFFFF;
                struct client_state* client = &clients[client_fd];

                DEBUG_PRINT("WRITE_OP completed for client_fd: %d\n", client_fd);

                // Prepare the next read operation
                sqe = io_uring_get_sqe(&tinfo->ring);
                if (sqe) {
                    DEBUG_PRINT("Submitting next READ_OP for client_fd: %d\n", client_fd);
                    io_uring_prep_read_fixed(
                        sqe,
                        client_fd,
                        tinfo->fixed_buffers + client->buffer_offset,
                        BUFFER_SIZE,
                        0,  // Offset
                        0   // Fixed buffer index
                    );
                    sqe->user_data = ((uint64_t)READ_OP << 32) | client_fd;
                    io_uring_submit(&tinfo->ring);
                }
            }
        }

        // Advance CQEs after processing
        io_uring_cq_advance(&tinfo->ring, num_cqes);
    }

    free(clients);
    return NULL;
}
*/





int create_server_socket() {
    int sockfd;
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1) {
        perror("setsockopt TCP_NODELAY");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

void* create_circular_buffer(size_t buffer_size) {
    size_t page_size = sysconf(_SC_PAGESIZE);
    if (buffer_size % page_size != 0) {
        fprintf(stderr, "Buffer size must be a multiple of page size (%zu bytes).\n", page_size);
        exit(EXIT_FAILURE);
    }
    void *addr = mmap(NULL, buffer_size * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        perror("mmap (initial mapping)");
        exit(EXIT_FAILURE);
    }
    if ((uintptr_t)addr % 512 != 0) {
        fprintf(stderr, "Buffer address is not 512-byte aligned.\n");
        munmap(addr, buffer_size * 2);
        exit(EXIT_FAILURE);
    }
    void *buffer = mmap(addr, buffer_size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buffer == MAP_FAILED) {
        perror("mmap (buffer mapping)");
        munmap(addr, buffer_size * 2);
        exit(EXIT_FAILURE);
    }
    void *wrap_around = mmap((char *)addr + buffer_size, buffer_size, PROT_READ | PROT_WRITE,
                             MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (wrap_around == MAP_FAILED) {
        perror("mmap (wrap-around mapping)");
        munmap(addr, buffer_size * 2);
        exit(EXIT_FAILURE);
    }
    return buffer;
}


int run_server(void) {
    pthread_t threads[NUM_THREADS];
    struct thread_info tinfo[NUM_THREADS];
    int i, server_fd;

    const char *test_input =
        "\tfunction foo\n"
        "foo:\n"
        "\tbequ 3 \"Meep\"\n"
        "\tcjmp FOUR\n"
        "\tloada 2\n"
        "\timuli 7\n"
        "\tout\n"
        "\tret\n"
        "FOUR:\n"
        "\tloadi 4\n"
        "\tbout \"Fu\"\n"
        "\tret\n";

    char *input = strdup(test_input);
    size_t program_size = 0;

    program = assemble(input, strlen(input), &program_size);
    printf("Program is %ld bytes\n", program_size);

    HashTable* hash_tables[NUM_THREADS] = {};

    // Create the initial server socket
    server_fd = create_server_socket();

    // Initialize threads with their own io_uring instance and socket fd
    for (i = 0; i < NUM_THREADS; i++) {
        hash_tables[i] = hash_table_init(1024);
        tinfo[i].thread_id = i;
        tinfo[i].server_fd = server_fd;
        tinfo[i].hash_tables = hash_tables;
        tinfo[i].fixed_buffers = create_circular_buffer(BUFFER_SIZE);

        /*
        struct io_uring_params params = {0};
        params.flags = IORING_SETUP_SQPOLL;       // Enable polling
        params.sq_thread_idle = 2000;            // Polling thread sleeps after 2 seconds of inactivity

        if (io_uring_queue_init_params(32, &tinfo[i].ring, &params) < 0) {
          perror("io_uring_queue_init_params");
          exit(EXIT_FAILURE);
        }

        // Check if polling is enabled
        if (params.flags & IORING_SETUP_SQPOLL) {
          printf("SQPOLL enabled for thread %d.\n", i);
        } else {
          printf("SQPOLL not enabled for thread %d.\n", i);
        }
*/
        if (io_uring_queue_init(32, &tinfo[i].ring, 0) < 0) {
          perror("io_uring_queue_init");
          exit(EXIT_FAILURE);
        }

        // Register the fixed buffer
        struct iovec iov = {
            .iov_base = tinfo[i].fixed_buffers,
            .iov_len = BUFFER_SIZE,
        };
        if (io_uring_register_buffers(&tinfo[i].ring, &iov, 1) < 0) {
            perror("io_uring_register_buffers");
            exit(EXIT_FAILURE);
        }

        // Create the thread
        if (pthread_create(&threads[i], NULL, worker_thread, &tinfo[i]) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
        io_uring_unregister_buffers(&tinfo[i].ring);
        io_uring_queue_exit(&tinfo[i].ring);
        free(tinfo[i].fixed_buffers);
    }
    close(server_fd);
    return 0;
}

#endif
