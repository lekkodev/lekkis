
#include "server.h"

#ifdef __APPLE__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/event.h>
#include <sys/uio.h>
#include <pthread.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>

#include "vm.h"
#include "assembler.h"
#include "counter.h"

#define PORT 12345
#define BACKLOG 256
#define BUFFER_SIZE 1024
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

typedef struct thread_arg {
    int server_fd;
    int thread_id;
    HashTable **hash_tables;
} thread_arg;

void pin_to_performance_core(int core_id) {
    thread_port_t thread = pthread_mach_thread_np(pthread_self());
    thread_affinity_policy_data_t policy = { .affinity_tag = core_id + 1 }; // Performance cores
    thread_policy_set(thread, THREAD_AFFINITY_POLICY, (thread_policy_t)&policy, THREAD_AFFINITY_POLICY_COUNT);
}
__thread char read_buffer[BUFFER_SIZE];
__thread char output_buffer[BUFFER_SIZE] = {0};
__thread struct iovec iov[2];
__thread size_t bytes_read, output_size;

void handle_client_event(int client_fd, thread_arg *arg) {
    // Initialize vectorized I/O buffers
    iov[0].iov_base = read_buffer;
    iov[0].iov_len = BUFFER_SIZE;

    // Read data from the client
    bytes_read = readv(client_fd, iov, 1);
    if (bytes_read <= 0) {
        if (bytes_read == 0) {
            printf("Client disconnected.\n");
        } else {
            perror("readv");
        }
        close(client_fd);
        return;
    }

    // Log the received data
    //printf("Received %zd bytes from client: %.*s\n", bytes_read, (int)bytes_read, read_buffer);

    runFunction(program, (uint8_t *)output_buffer, &output_size, arguments, func_name);

    const uint8_t* key = "abc123def876qqabc123def876qq";
    Bucket* bucket = hash_table_find_or_create(arg->hash_tables[arg->thread_id], key, 1234);
    bucket->counters[12]++;
    
    uint64_t total = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        total += hash_table_find_or_create(arg->hash_tables[i], key, 1234)->counters[12];
    }
    //printf("%lld\n", total);
    
    // Log the response
    //printf("Sending %zd bytes to client: %.*s\n", output_size, (int)output_size, output_buffer);

    // Write the response back to the client
    iov[0].iov_base = output_buffer;
    iov[0].iov_len = output_size;
    if (writev(client_fd, iov, 1) < 0) {
        perror("writev");
        close(client_fd);
        return;
    }
}




void *worker_thread(void *arg) {
    struct thread_arg *args = (struct thread_arg *)arg;
    int server_fd = args->server_fd;
    int thread_id = args->thread_id;

    // Pin thread to a specific core (optional)
    pin_to_performance_core(thread_id);

    // Create a kqueue instance for this thread
    int kq = kqueue();
    if (kq < 0) {
        perror("kqueue");
        return NULL;
    }

    // Register the server_fd for read events
    struct kevent ev_set;
    EV_SET(&ev_set, server_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq, &ev_set, 1, NULL, 0, NULL) < 0) {
        perror("kevent");
        return NULL;
    }

    // Event loop
    while (1) {
        struct kevent events[MAX_EVENTS];
        int num_events = kevent(kq, NULL, 0, events, MAX_EVENTS, NULL);

        if (num_events < 0) {
            perror("kevent");
            continue;
        }

        for (int i = 0; i < num_events; i++) {
            if (events[i].ident == server_fd) {
                // Accept a new client connection
                int client_fd = accept(server_fd, NULL, NULL);
                if (client_fd < 0) {
                    perror("accept");
                    continue;
                }

                // Make the client socket non-blocking
                int flags = fcntl(client_fd, F_GETFL, 0);
                fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

                // Register the client_fd for read events
                EV_SET(&ev_set, client_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
                kevent(kq, &ev_set, 1, NULL, 0, NULL);
                printf("New client connected on thread %d.\n", thread_id);
            } else if (events[i].filter == EVFILT_READ) {
                // Handle client request
                handle_client_event((int)events[i].ident, args);
            }
        }
    }

    close(kq);
    return NULL;
}

int run_server(void) {
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

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int optval = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    HashTable* hash_tables[NUM_THREADS] = {};
    
    // Create worker threads
    pthread_t threads[NUM_THREADS];
    struct thread_arg args[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        hash_tables[i] = hash_table_init(1024);
        args[i].server_fd = server_fd;
        args[i].thread_id = i;
        args[i].hash_tables = hash_tables;
        pthread_create(&threads[i], NULL, worker_thread, &args[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    close(server_fd);
    return 0;
}


#endif
