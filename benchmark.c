#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define SERVER_IP "127.0.0.1"   // Replace with your server's IP
#define SERVER_PORT 12345       // Replace with your server's port
#define NUM_THREADS 16           // Number of worker threads
#define REQUESTS_PER_THREAD 100000 // Number of requests per thread
#define MESSAGE "Ping"

typedef struct {
    int thread_id;
    long success_count;
} thread_data_t;

// Function to measure the time difference
double time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

void *benchmark_thread(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    data->success_count = 0;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        pthread_exit(NULL);
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        pthread_exit(NULL);
    }

    for (int i = 0; i < REQUESTS_PER_THREAD; i++) {
        send(sockfd, MESSAGE, strlen(MESSAGE), 0);
        char buffer[1024];
        recv(sockfd, buffer, sizeof(buffer), 0);
        data->success_count++;
    }

    close(sockfd);
    return NULL;
}

/*
int main() {
    pthread_t threads[NUM_THREADS];
    thread_data_t thread_data[NUM_THREADS];
    struct timespec start, end;

    // Start timer
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].thread_id = i;
        if (pthread_create(&threads[i], NULL, benchmark_thread, &thread_data[i]) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    // Wait for threads to finish
    long total_requests = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
        total_requests += thread_data[i].success_count;
    }

    // Stop timer
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_time = time_diff(start, end);

    printf("Total requests: %ld\n", total_requests);
    printf("Elapsed time: %.2f seconds\n", elapsed_time);
    printf("Requests per second: %.2f\n", total_requests / elapsed_time);

    return 0;
}
*/
