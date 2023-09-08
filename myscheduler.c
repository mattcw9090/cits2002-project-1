#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//  CITS2002 Project 1 2023
//  Student1:   22974046   Matthew Chew
//  Student2:   STUDENT-NUMBER2   Darryl Max


//  CONSTANTS
#define MAX_DEVICES                     4
#define MAX_DEVICE_NAME                 20
#define MAX_COMMANDS                    10
#define MAX_COMMAND_NAME                20
#define MAX_SYSCALLS_PER_PROCESS        40
#define MAX_RUNNING_PROCESSES           50

//  NOTE THAT DEVICE DATA-TRANSFER-RATES ARE MEASURED IN BYTES/SECOND,
//  THAT ALL TIMES ARE MEASURED IN MICROSECONDS (usecs),
//  AND THAT THE TOTAL-PROCESS-COMPLETION-TIME WILL NOT EXCEED 2000 SECONDS
//  (SO YOU CAN SAFELY USE 'STANDARD' 32-BIT ints TO STORE TIMES).

#define DEFAULT_TIME_QUANTUM            100

#define TIME_CONTEXT_SWITCH             5
#define TIME_CORE_STATE_TRANSITIONS     10
#define TIME_ACQUIRE_BUS                20

#define CHAR_COMMENT                    '#'

//  ----------------------------------------------------------------------

// Time Quantum
int time_quantum = DEFAULT_TIME_QUANTUM;

// Device Struct
typedef struct {
    char name[MAX_DEVICE_NAME];
    int readSpeed;
    int writeSpeed;
    int priority;
} Device;

// Operation Struct
typedef struct {
    char name[10]; // operation name: "sleep", "exit", "read", "write", "spawn", "wait"
    char device_name[MAX_DEVICE_NAME]; // only for "read" or "write"
    int time; // time in usecs to trigger the operation
    int duration; // only for "sleep"
    int bytes; // only for "read" or "write"
    char child_process_name[MAX_COMMAND_NAME]; // only for "spawn"
} Operation;

// Process Struct
typedef struct {
    int id;
    char name[MAX_COMMAND_NAME];
    char processState[10];
    Operation operations[MAX_SYSCALLS_PER_PROCESS];
    int operation_count;
} Process;

// Array of devices
Device devices_array[MAX_DEVICES];

// Array of Processes
Process processes[MAX_RUNNING_PROCESSES];

// Ready Queue
Process ready_queue[MAX_RUNNING_PROCESSES];

// Helper Functions
int compare_devices_read_speed (const void *a, const void *b) {
    const Device *deviceA = (const Device *)a;
    const Device *deviceB = (const Device *)b;
    return deviceB->readSpeed - deviceA->readSpeed;
}

// Functions used by main()
void read_sysconfig(char argv0[], char filename[]) {

    FILE *file = fopen(filename, "r");

    if (file == NULL) {
        printf("Cannot open file '%s'\n", filename);
        exit(EXIT_FAILURE);
    }

    char line[256];
    int device_count = 0;

    while (fgets(line, sizeof(line), file)) {

        // Remove newline character, if present
        if (line[strlen(line) - 1] == '\n') {
            line[strlen(line) - 1] = '\0';
        }

        // Ignore comment lines
        if (line[0] == CHAR_COMMENT) {
            continue;
        }

        // Handle 'device' lines
        if (strncmp(line, "device", 6) == 0) {
            sscanf(line, "device %s %iBps %iBps", devices_array[device_count].name, &devices_array[device_count].readSpeed, &devices_array[device_count].writeSpeed);
            device_count++;
        }

        // Handle 'timequantum' line
        if (strncmp(line, "timequantum", 11) == 0) {
            sscanf(line, "timequantum %i", &time_quantum);
        }
    }

    fclose(file);

    // Sort devices by read speed
    qsort(devices_array, device_count, sizeof(Device), compare_devices_read_speed);

    // Assign priorities based on sorted order
    for (int i = 0; i < device_count; ++i) {
        devices_array[i].priority = i + 1;  // Priority starts from 1 for the highest read speed
    }
}

void read_commands(char argv0[], char filename[]) {

    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        printf("Cannot open file '%s'\n", filename);
        exit(EXIT_FAILURE);
    }

    char line[256];
    int process_count = 0;

    while (fgets(line, sizeof(line), file)) {

        // Remove newline character, if present
        if (line[strlen(line) - 1] == '\n') {
            line[strlen(line) - 1] = '\0';
        }

        // Ignore comment lines
        if (line[0] == CHAR_COMMENT) {
            continue;
        }

        // New process, register its definition
        if (line[0] != '\t') {
            strncpy(processes[process_count].name, line, MAX_COMMAND_NAME);
            processes[process_count].operation_count = 0;
            process_count++;
        }
        // Operation for the latest process
        else {
            Process *current_process = &processes[process_count - 1];
            Operation *next_operation_slot = &current_process->operations[current_process->operation_count];

            // Initial scan to identify the operation name
            int initialTime;
            char operationName[10];
            sscanf(line, "\t%iusecs %s", &initialTime, operationName);

            if (strcmp(operationName, "sleep") == 0) {
                sscanf(line, "\t%iusecs %s %iusecs", &next_operation_slot->time, next_operation_slot->name, &next_operation_slot->duration);
            }
            else if (strcmp(operationName, "exit") == 0) {
                sscanf(line, "\t%iusecs %s", &next_operation_slot->time, next_operation_slot->name);
            }
            else if (strcmp(operationName, "read") == 0 || strcmp(operationName, "write") == 0) {
                sscanf(line, "\t%iusecs %s %s %iB", &next_operation_slot->time, next_operation_slot->name, next_operation_slot->device_name, &next_operation_slot->bytes);
            }
            else if (strcmp(operationName, "spawn") == 0) {
                sscanf(line, "\t%iusecs %s %s", &next_operation_slot->time, next_operation_slot->name, next_operation_slot->child_process_name);
            }
            else if (strcmp(operationName, "wait") == 0) {
                sscanf(line, "\t%iusecs %s", &next_operation_slot->time, next_operation_slot->name);
            }
            current_process->operation_count++;
        }
    }
    fclose(file);

    // TEST CODE: Replace this with your actual implementation
    for (int i = 0; i < process_count; ++i) {
        printf("Process name: %s\n", processes[i].name);
            for (int j = 0; j < processes[i].operation_count; ++j) {
                printf("\tOperation: %s, Time: %i, Device: %s, Amount: %i, Duration: %i, Child Process: %s\n",
                       processes[i].operations[j].name,
                       processes[i].operations[j].time,
                       processes[i].operations[j].device_name,
                       processes[i].operations[j].bytes,
                       processes[i].operations[j].duration,
                       processes[i].operations[j].child_process_name);
            }
    }
}


//  ----------------------------------------------------------------------

void execute_commands(void)
{
}

//  ----------------------------------------------------------------------

int main(int argc, char *argv[]) {
//  ENSURE THAT WE HAVE THE CORRECT NUMBER OF COMMAND-LINE ARGUMENTS
    if(argc != 3) {
        printf("Usage: %s sysconfig-file command-file\n", argv[0]);
        exit(EXIT_FAILURE);
    }

//  READ THE SYSTEM CONFIGURATION FILE
    read_sysconfig(argv[0], argv[1]);

//  READ THE COMMAND FILE
    read_commands(argv[0], argv[2]);

//  EXECUTE COMMANDS, STARTING AT FIRST IN command-file, UNTIL NONE REMAIN
    execute_commands();

//  PRINT THE PROGRAM'S RESULTS
    printf("measurements  %i  %i\n", 0, 0);

    exit(EXIT_SUCCESS);
}