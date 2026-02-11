#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>

#define ITERATIONS 100000  //  syscalls num

// current time
double get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000000.0) + tv.tv_usec;
}

double run_benchmark() {
    double start = get_time_us();
    
    for (int i = 0; i < ITERATIONS; i++) {
        int fd = open("/etc/hostname", O_RDONLY);
        if (fd >= 0) {
            char buf[64];
            read(fd, buf, sizeof(buf));
            close(fd);
        }
    }
    
    double end = get_time_us();
    return (end - start) / 1000.0;
}

int main() {
    printf("OVERHEAD IMPACT ON SAMPLE PROGRAM\n");
    
    printf("Test Configuration:\n");
    printf("  - Iterations: %d syscalls\n", ITERATIONS);
    printf("  - Syscalls per iteration: open() + read() + close()\n");
    printf("  - Total syscalls: %d\n", ITERATIONS * 3);
    printf("  - File accessed: /etc/hostname\n\n");
    
    // baseline (module OFF)
    printf("1: Baseline Test (Module in OFF mode)\n");
    printf("Setting module to OFF mode...\n");
    system("cd ~/syscall-monitor/userspace && sudo ./syscall_control --off > /dev/null 2>&1");
    sleep(1);
    
    printf("Running benchmark (this may take a moment)...\n");
    double baseline_time = run_benchmark();
    printf("Baseline execution time: %.2f ms\n\n", baseline_time);
    
    // monitoring (module LOG)
    printf("2: Monitoring Test (Module in LOG mode)\n");
    printf("Setting module to LOG mode for 'read' syscall...\n");
    system("cd ~/syscall-monitor/userspace && sudo ./syscall_control --log --syscall read > /dev/null 2>&1");
    sleep(1);
    
    printf("Running benchmark (this may take a moment)...\n");
    double monitored_time = run_benchmark();
    printf("Monitored execution time: %.2f ms\n\n", monitored_time);
    
    printf("Setting module back to OFF mode...\n");
    system("cd ~/syscall-monitor/userspace && sudo ./syscall_control --off > /dev/null 2>&1");
    
    // calculate overhead
    double overhead_ms = monitored_time - baseline_time;
    double overhead_percent = (overhead_ms / baseline_time) * 100.0;
    double overhead_per_syscall_ns = (overhead_ms * 1000000.0) / (ITERATIONS * 3);
    
    // result
    printf("RESULTS SUMMARY\n");
    printf("Baseline time (OFF mode):     %.2f ms\n", baseline_time);
    printf("Monitored time (LOG mode):    %.2f ms\n", monitored_time);
    printf("Absolute overhead:            %.2f ms\n", overhead_ms);
    printf("Percentage overhead:          %.2f%%\n", overhead_percent);
    printf("Per-syscall overhead:         %.2f ns\n\n", overhead_per_syscall_ns);
    
    printf("BREAKDOWN:\n");
    printf("  Total syscalls performed: %d\n", ITERATIONS * 3);
    printf("  Syscalls monitored:       %d (read syscalls only)\n", ITERATIONS);
    printf("  Syscalls not monitored:   %d (open + close)\n\n", ITERATIONS * 2);
    
    printf("ANALYSIS:\n");
    
    if (overhead_percent < 5) {
        printf("✓ Overhead is NEGLIGIBLE (< 5%%)\n");
        printf("  The kprobe overhead is minimal for fast syscalls.\n");
    } else if (overhead_percent < 20) {
        printf("⚠ Overhead is LOW-MEDIUM (5-20%%)\n");
        printf("  Acceptable for monitoring purposes.\n");
    } else if (overhead_percent < 50) {
        printf("⚠ Overhead is MEDIUM-HIGH (20-50%%)\n");
        printf("  May impact performance-critical applications.\n");
    } else {
        printf("✗ Overhead is HIGH (> 50%%)\n");
        printf("  Significant performance impact. Consider optimizations:\n");
        printf("  - Use relay buffers instead of printk\n");
        printf("  - Implement batching to reduce logging frequency\n");
        printf("  - Add per-PID filtering to reduce system-wide overhead\n");
    }
    
    return 0;
}
