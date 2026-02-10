#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <errno.h>
#include <cjson/cJSON.h>
#include <time.h>
#include <strings.h>

#define DEVICE_PATH "/dev/syscall_monitor"

// ioctl commands
#define IOCTL_SET_MODE _IOW('s', 1, int)
#define IOCTL_SET_SYSCALL _IOW('s', 2, int)
#define IOCTL_SET_PID _IOW('s', 3, int)

// Modes
#define MODE_OFF 0
#define MODE_LOG 1
#define MODE_BLOCK 2

// Syscall types
#define SYSCALL_OPEN 0
#define SYSCALL_READ 1
#define SYSCALL_WRITE 2

int device_fd = -1;

// FSM structure
typedef struct {
    char **states;
    int num_states;
    int current_state;
} FSM;

// Function prototypes
int open_device();
void close_device();
int set_mode(int mode);
int set_syscall(const char* syscall_name);
int set_pid(int pid);
FSM* load_fsm(const char* filename);
void free_fsm(FSM* fsm);
void run_fsm(FSM* fsm);
int syscall_name_to_type(const char* name);
const char* syscall_type_to_name(int type);

int open_device() {
    device_fd = open(DEVICE_PATH, O_RDWR);
    if (device_fd < 0) {
        perror("Failed to open device");
        printf("Make sure the kernel module is loaded: sudo insmod syscall_monitor.ko\n");
        return -1;
    }
    return 0;
}

void close_device() {
    if (device_fd >= 0) {
        close(device_fd);
        device_fd = -1;
    }
}

// Set mode via ioctl
int set_mode(int mode) {
    const char* mode_str[] = {"OFF", "LOG", "BLOCK"};
    
    if (ioctl(device_fd, IOCTL_SET_MODE, &mode) < 0) {
        perror("Failed to set mode");
        return -1;
    }
    
    printf("[INFO] Mode changed to: %s\n", mode_str[mode]);
    return 0;
}

// Convert syscall name to type
int syscall_name_to_type(const char* name) {
    if (strcmp(name, "open") == 0) return SYSCALL_OPEN;
    if (strcmp(name, "read") == 0) return SYSCALL_READ;
    if (strcmp(name, "write") == 0) return SYSCALL_WRITE;
    return -1;
}

// Convert syscall type to name
const char* syscall_type_to_name(int type) {
    switch(type) {
        case SYSCALL_OPEN: return "open";
        case SYSCALL_READ: return "read";
        case SYSCALL_WRITE: return "write";
        default: return "unknown";
    }
}

// Set syscall to monitor
int set_syscall(const char* syscall_name) {
    int syscall_type = syscall_name_to_type(syscall_name);
    
    if (syscall_type < 0) {
        printf("[ERROR] Invalid syscall name: %s (must be: open, read, or write)\n", syscall_name);
        return -1;
    }
    
    if (ioctl(device_fd, IOCTL_SET_SYSCALL, &syscall_type) < 0) {
        perror("Failed to set syscall");
        return -1;
    }
    
    printf("[INFO] Target syscall set to: %s\n", syscall_name);
    return 0;
}

// Set PID
int set_pid(int pid) {
    if (ioctl(device_fd, IOCTL_SET_PID, &pid) < 0) {
        perror("Failed to set PID");
        return -1;
    }
    
    printf("[INFO] Target PID set to: %d\n", pid);
    return 0;
}

// Load FSM from jSON file
FSM* load_fsm(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        perror("Failed to open FSM file");
        return NULL;
    }
    
    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char* content = malloc(length + 1);
    fread(content, 1, length, fp);
    content[length] = '\0';
    fclose(fp);
    
    cJSON* json = cJSON_Parse(content);
    free(content);
    
    if (!json) {
        printf("[ERROR] Failed to parse JSON: %s\n", cJSON_GetErrorPtr());
        return NULL;
    }
    
    cJSON* states_json = cJSON_GetObjectItem(json, "states");
    if (!cJSON_IsArray(states_json)) {
        printf("[ERROR] 'states' must be an array\n");
        cJSON_Delete(json);
        return NULL;
    }
    
    int num_states = cJSON_GetArraySize(states_json);
    if (num_states == 0) {
        printf("[ERROR] FSM must have at least one state\n");
        cJSON_Delete(json);
        return NULL;
    }
    
    FSM* fsm = malloc(sizeof(FSM));
    fsm->num_states = num_states;
    fsm->current_state = 0;
    fsm->states = malloc(sizeof(char*) * num_states);
    
    for (int i = 0; i < num_states; i++) {
        cJSON* state = cJSON_GetArrayItem(states_json, i);
        if (!cJSON_IsString(state)) {
            printf("[ERROR] State %d is not a string\n", i);
            free_fsm(fsm);
            cJSON_Delete(json);
            return NULL;
        }
        
        const char* state_name = cJSON_GetStringValue(state);
        if (syscall_name_to_type(state_name) < 0) {
            printf("[ERROR] Invalid syscall in state %d: %s\n", i, state_name);
            free_fsm(fsm);
            cJSON_Delete(json);
            return NULL;
        }
        
        fsm->states[i] = strdup(state_name);
    }
    
    cJSON_Delete(json);
    
    printf("[FSM] Loaded FSM with %d states: ", num_states);
    for (int i = 0; i < num_states; i++) {
        printf("%s", fsm->states[i]);
        if (i < num_states - 1) printf(" -> ");
    }
    printf(" (loops back)\n");
    
    return fsm;
}

void free_fsm(FSM* fsm) {
    if (!fsm) return;
    
    for (int i = 0; i < fsm->num_states; i++) {
        free(fsm->states[i]);
    }
    free(fsm->states);
    free(fsm);
}

// observed syscall
int check_syscall_observed(const char* syscall_name) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), 
             "sudo dmesg | tail -20 | grep -q 'SYSCALL_MONITOR.*called %s'", 
             syscall_name);
    
    int result = system(cmd);
    return (result == 0);  // Returns 1 if found, 0 if not
}


void run_fsm(FSM* fsm) {
    printf("\n[FSM] Starting FSM execution\n");
    printf("[FSM] Press Ctrl+C to stop\n\n");
    
    system("sudo dmesg -C");
    
    while (1) {
        const char* current_syscall = fsm->states[fsm->current_state];
        
        printf("[FSM] Current State: %d/%d - Monitoring: %s\n", 
               fsm->current_state + 1, fsm->num_states, current_syscall);
        
        if (set_syscall(current_syscall) < 0) {
            printf("[ERROR] Failed to set syscall\n");
            return;
        }
        
        printf("[FSM] Waiting for %s() syscall...\n", current_syscall);
        
        int observed = 0;
        while (!observed) {
            sleep(1);  
            observed = check_syscall_observed(current_syscall);
        }
        
        printf("[FSM] ✓ Observed %s()! Transitioning to next state...\n\n", current_syscall);
        
        system("sudo dmesg -C");
        
        fsm->current_state = (fsm->current_state + 1) % fsm->num_states;
        
        sleep(1);
    }
}

// Print usage
void print_usage(const char* prog_name) {
    printf("Usage: %s [OPTIONS]\n\n", prog_name);
    printf("Options:\n");
    printf("  --off              Set module to OFF mode\n");
    printf("  --log              Set module to LOG mode\n");
    printf("  --block            Set module to BLOCK mode\n");
    printf("  --syscall <name>   Set syscall to monitor (open, read, write)\n");
    printf("  --pid <pid>        Set PID to monitor/block\n");
    printf("  --file <json>      Run FSM from JSON file (requires --log)\n");
    printf("  --help             Display this help\n\n");
    printf("Examples:\n");
    printf("  %s --log --syscall open\n", prog_name);
    printf("  %s --log --file fsm_example1.json\n", prog_name);
    printf("  %s --off\n\n", prog_name);
}

int main(int argc, char *argv[]) {
    int opt;
    int mode = -1;
    char* syscall_name = NULL;
    int pid = -2;
    char* fsm_file = NULL;
    
    static struct option long_options[] = {
        {"off",     no_argument,       0, 'o'},
        {"log",     no_argument,       0, 'l'},
        {"block",   no_argument,       0, 'b'},
        {"syscall", required_argument, 0, 's'},
        {"pid",     required_argument, 0, 'p'},
        {"file",    required_argument, 0, 'f'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    while (1) {
        int option_index = 0;
        opt = getopt_long(argc, argv, "olbs:p:f:h", long_options, &option_index);
        
        if (opt == -1) break;
        
        switch (opt) {
            case 'o': mode = MODE_OFF; break;
            case 'l': mode = MODE_LOG; break;
            case 'b': mode = MODE_BLOCK; break;
            case 's': syscall_name = optarg; break;
            case 'p': pid = atoi(optarg); break;
            case 'f': fsm_file = optarg; break;
            case 'h':
            default:
                print_usage(argv[0]);
                return (opt == 'h') ? 0 : 1;
        }
    }
    
    if (argc == 1) {
        print_usage(argv[0]);
        return 1;
    }
    
    printf("[INFO] Opening device: %s\n", DEVICE_PATH);
    if (open_device() < 0) {
        return 1;
    }
    
    // If FSM file provided, run FSM mode
    if (fsm_file != NULL) {
        if (mode != MODE_LOG) {
            printf("[ERROR] --file can only be used with --log mode\n");
            close_device();
            return 1;
        }
        
        if (set_mode(MODE_LOG) < 0) {
            close_device();
            return 1;
        }
        
        FSM* fsm = load_fsm(fsm_file);
        if (!fsm) {
            close_device();
            return 1;
        }
        
        run_fsm(fsm);
        
        free_fsm(fsm);
        close_device();
        return 0;
    }
    
    // Normal mode (no FSM)
    if (mode != -1) {
        if (set_mode(mode) < 0) {
            close_device();
            return 1;
        }
    }
    
    if (syscall_name != NULL) {
        if (set_syscall(syscall_name) < 0) {
            close_device();
            return 1;
        }
    }
    
    if (pid != -2) {
        if (set_pid(pid) < 0) {
            close_device();
            return 1;
        }
    }
    
    close_device();
    printf("[INFO] Commands executed successfully\n");
    
    return 0;
}
