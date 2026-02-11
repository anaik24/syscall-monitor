#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>

#define POLLING_INTERVAL_US 10000
#define MAX_ITERATIONS 5

// current time
double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
}

int main() {
    printf("USERSPACE REACTION TIME AFTER SYSCALL OBSERVED\n");
    
    printf("Test Configuration:\n");
    printf("  - Polling interval: %d ms\n", POLLING_INTERVAL_US / 1000);
    printf("  - Iterations: %d\n", MAX_ITERATIONS);
    printf("  - Syscall: fopen() which triggers open() and read()\n\n");
    
    double total_latency = 0.0;
    double min_latency = 999999.0;
    double max_latency = 0.0;
    
    for (int i = 1; i <= MAX_ITERATIONS; i++) {
        printf("[Iteration %d/%d]\n", i, MAX_ITERATIONS);
        
        printf("  Clearing kernel log...\n");
        system("sudo dmesg -C > /dev/null 2>&1");
        usleep(100000); // Wait 100ms
        
        // start time
        double start_time = get_time_ms();
        
        printf("  Triggering syscall (opening /etc/hostname)...\n");
        FILE* f = fopen("/etc/hostname", "r");
        if (f) {
            char buf[100];
            fread(buf, 1, sizeof(buf), f);
            fclose(f);
        }
       
        printf("  Polling dmesg for detection...\n");
        int found = 0;
        int poll_count = 0;
        while (!found) {
            int ret = system("sudo dmesg 2>/dev/null | grep -q 'SYSCALL_MONITOR'");
            if (ret == 0) {
                found = 1;
            } else {
                usleep(POLLING_INTERVAL_US);
                poll_count++;
            }
            
            if (poll_count > 500) {
                printf("  ERROR: Timeout waiting for syscall detection!\n");
                printf("  Make sure the kernel module is loaded and in LOG mode.\n");
                return 1;
            }
        }
        
        // end time
        double end_time = get_time_ms();
        double latency = end_time - start_time;
        
        printf("  Detection latency: %.2f ms (after %d polls)\n\n", latency, poll_count);
        
        total_latency += latency;
        if (latency < min_latency) min_latency = latency;
        if (latency > max_latency) max_latency = latency;
        
        if (i < MAX_ITERATIONS) {
            sleep(1);
        }
    }
    
    // Calculate and display statistics
    double avg_latency = total_latency / MAX_ITERATIONS;
    
    printf("RESULTS SUMMARY\n");
    printf("Average detection latency: %.2f ms\n", avg_latency);
    printf("Minimum detection latency: %.2f ms\n", min_latency);
    printf("Maximum detection latency: %.2f ms\n", max_latency);
    printf("\n");
    
    printf("ANALYSIS:\n");
    printf("The detection latency includes:\n");
    printf("  1. Time for kernel to log the syscall (printk)\n");
    printf("  2. Time for dmesg to read kernel ring buffer\n");
    printf("  3. Polling interval overhead (%d ms)\n", POLLING_INTERVAL_US / 1000);
    printf("\n");
    
    if (avg_latency < 50) {
        printf("CONCLUSION: Detection is reasonably fast (< 50ms average).\n");
    } else if (avg_latency < 100) {
        printf("CONCLUSION: Detection is moderate (50-100ms average).\n");
    } else {
        printf("CONCLUSION: Detection is slow (> 100ms average).\n");
        printf("Consider reducing polling interval or using event-based approach.\n");
    }
    
    return 0;
}
