#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// PROJECT DESCRIPTION
//  CITS2002 Project 1 2023
//  Student1:   22974046   Matthew Chew
//  Student2:   23477648   Darryl Max

//  CONSTANTS
#define MAX_LINE_LENGTH                 256
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

// Global Variables
int time_quantum = DEFAULT_TIME_QUANTUM;
int system_timer = 0; // in microseconds
int device_count = 0;
int command_count = 0;
int ready_queue_count = 0;
bool used_ids[MAX_RUNNING_PROCESSES] = {false};

// Structs
typedef struct {
    char name[MAX_DEVICE_NAME];
    unsigned long long readSpeed;
    unsigned long long writeSpeed;
    int priority;
} Device;
typedef struct {
    char name[10]; // operation name: "sleep", "exit", "read", "write", "spawn", "wait"
    char device_name[MAX_DEVICE_NAME]; // only for "read" or "write"
    int time; // time in usecs to trigger the operation
    int duration; // only for "sleep"
    unsigned long long bytes; // only for "read" or "write"
    char child_process_name[MAX_COMMAND_NAME]; // only for "spawn"
} Syscall;
typedef struct {
	char name[MAX_COMMAND_NAME];
	int syscall_count;
	Syscall syscalls[MAX_SYSCALLS_PER_PROCESS];
} Command;
typedef struct {
	int id;
	Command *command;
	char processState[10];
	int syscall_index;
	int cpu_time;
} Process;

// Arrays of Structs
Device devices_array[MAX_DEVICES];
Command command_definitions[MAX_COMMANDS];
Process ready_queue[MAX_RUNNING_PROCESSES];

// Helper Functions
int compare_devices_read_speed (const void *a, const void *b) {
    const Device *deviceA = (const Device *)a;
    const Device *deviceB = (const Device *)b;
    return deviceB->readSpeed - deviceA->readSpeed;
}
int allocate_lowest_id() {
	for (int i = 0; i < MAX_RUNNING_PROCESSES; ++i) {
		if (!used_ids[i]) {
			used_ids[i] = true;
			return i;
		}
	}
	// Handle error: no available IDs (this shouldn't happen if the code is correct)
	fprintf(stderr, "Error: No available IDs.\n");
	exit(EXIT_FAILURE);
}
void release_id(int id) {
	if (id >= 0 && id < MAX_RUNNING_PROCESSES) {
		used_ids[id] = false;
	}
}
Process initialize_process(char command_string[]) {
	// Look for the command in the existing array of commands
	Command *selected_command = NULL;
	for (int i = 0; i < command_count; ++i) {
		if (strcmp(command_definitions[i].name, command_string) == 0) {
			selected_command = &command_definitions[i];
			break;
		}
	}

	// If command is not found, handle the error
	if (selected_command == NULL) {
		fprintf(stderr, "Command '%s' not found.\n", command_string);
		exit(EXIT_FAILURE);
	}

	// Initialize a new Process structure
	Process new_process;
	new_process.id = allocate_lowest_id();
	new_process.command = selected_command;
	new_process.syscall_index = 0;
	new_process.cpu_time = 0;
	strcpy(new_process.processState, "READY");

	return new_process;
}

// Functions used by main()
void read_sysconfig(char argv0[], char filename[]) {
    FILE *file = fopen(filename, "r");

    if (file == NULL) {
        printf("Cannot open file '%s'\n", filename);
        exit(EXIT_FAILURE);
    }

    char line[MAX_LINE_LENGTH];

    while (fgets(line, sizeof(line), file)) {

        // Skip comment lines and empty lines
        if (line[0] == CHAR_COMMENT || line[0] == '\n') {
            continue;
        }

        // Handle 'device' lines
        if (strncmp(line, "device", 6) == 0) {
            sscanf(line, "device %s %lluBps %lluBps", devices_array[device_count].name, &devices_array[device_count].readSpeed, &devices_array[device_count].writeSpeed);
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

    char line[MAX_LINE_LENGTH];

    while (fgets(line, sizeof(line), file)) {

        // Skip comment lines and empty lines
        if (line[0] == CHAR_COMMENT || line[0] == '\n') {
            continue;
        }

        // New Command, register its definition
        if (line[0] != '\t') {
            sscanf(line, "%s", command_definitions[command_count].name);
			command_definitions[command_count].syscall_count = 0;
            command_count++;
        }
		// Operation for the latest process
        else {
            Command *current_command = &command_definitions[command_count - 1];
            Syscall *next_operation_slot = &current_command->syscalls[current_command->syscall_count];

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
                sscanf(line, "\t%iusecs %s %s %lluB", &next_operation_slot->time, next_operation_slot->name, next_operation_slot->device_name, &next_operation_slot->bytes);
            }
            else if (strcmp(operationName, "spawn") == 0) {
                sscanf(line, "\t%iusecs %s %s", &next_operation_slot->time, next_operation_slot->name, next_operation_slot->child_process_name);
            }
            else if (strcmp(operationName, "wait") == 0) {
                sscanf(line, "\t%iusecs %s", &next_operation_slot->time, next_operation_slot->name);
            }
			current_command->syscall_count++;
        }
    }
    fclose(file);
}
void execute_commands(void) {

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

// TEST CODE
	// #1: Print devices to console
	printf("\nLoaded devices:\n");
	for (int i = 0; i < device_count; ++i) {
		printf("\tDevice name: %s, Read speed: %llu Bps, Write speed: %llu, Priority: %i Bps\n",
			   devices_array[i].name,
			   devices_array[i].readSpeed,
			   devices_array[i].writeSpeed,
			   devices_array[i].priority);
	}
	printf("\nTime Quantum: %i\n", time_quantum);

	// #2: Print commands to the console
	for (int i = 0; i < command_count; ++i) {
		printf("\nCommand name: %s\n", command_definitions[i].name);
		for (int j = 0; j < command_definitions[i].syscall_count; ++j) {
			printf("\tOperation: %s, Time: %i, Device: %s, Bytes: %llu, Duration: %i, Child Process: %s\n",
				   command_definitions[i].syscalls[j].name,
				   command_definitions[i].syscalls[j].time,
				   command_definitions[i].syscalls[j].device_name,
				   command_definitions[i].syscalls[j].bytes,
				   command_definitions[i].syscalls[j].duration,
				   command_definitions[i].syscalls[j].child_process_name);
		}
	}

    exit(EXIT_SUCCESS);
}