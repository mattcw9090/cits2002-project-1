#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// CITS2002 Project 1 2023
// Student1:   22974046   Matthew Chew
// Student2:   23477648   Darryl Max

// CONSTANTS
#define MAX_LINE_LENGTH             256
#define MAX_DEVICES                 4
#define MAX_DEVICE_NAME             20
#define MAX_COMMANDS                10
#define MAX_COMMAND_NAME            20
#define MAX_SYSCALLS_PER_PROCESS    40
#define MAX_RUNNING_PROCESSES       50
#define DEFAULT_TIME_QUANTUM        100
#define TIME_CONTEXT_SWITCH         5
#define TIME_CORE_STATE_TRANSITIONS 10
#define TIME_ACQUIRE_BUS            20
#define CHAR_COMMENT                '#'

// Global Variables
int time_quantum = DEFAULT_TIME_QUANTUM;
int global_clock = 0;
int device_count = 0;
int command_count = 0;
int process_created = 0;
int process_terminated = 0;
int total_cpu_time = 0;
bool used_ids[MAX_RUNNING_PROCESSES] = { false };

// SYSCALL DATA STRUCTURE
typedef struct {
	char name[10];
	char device_name[MAX_DEVICE_NAME];
	int time_to_trigger;
	int sleep_duration;
	unsigned long long bytes;
	char child_process_name[MAX_COMMAND_NAME];
} Syscall;

// COMMAND DATA STRUCTURE
typedef struct {
	char name[MAX_COMMAND_NAME];
	int syscall_count;
	Syscall syscalls[MAX_SYSCALLS_PER_PROCESS];
} Command;

Command command_definitions[MAX_COMMANDS];

// PROCESS DATA STRUCTURE
typedef struct {
	int id;
	Command *command;
	char state[10];
	int syscall_index;
	int sleep_timer;
	int cumulative_cpu_time;
	int time_to_complete_io;
	int io_priority;
	int parent_pid;
	int num_children;
} Process;

Process processes[MAX_RUNNING_PROCESSES];

// Function to get a process by PID
Process* get_process(int pid) {
	return &processes[pid];
}


// DEVICES DATA STRUCTURE
typedef struct {
	char name[MAX_DEVICE_NAME];
	unsigned long long readSpeed;
	unsigned long long writeSpeed;
	int priority;
} Device;

Device devices_array[MAX_DEVICES];

// Function to get a device by name
Device* get_device(char *device_name) {
	for (int i = 0; i < MAX_DEVICES; ++i) {
		if (strcmp(devices_array[i].name, device_name) == 0) {
			return &devices_array[i];
		}
	}
	return NULL;
}

// READY_QUEUE DATA STRUCTURE
int ready_queue[MAX_RUNNING_PROCESSES];
int ready_queue_size = 0;

void enqueue_ready_queue(int pid) {
	if (ready_queue_size >= MAX_RUNNING_PROCESSES) {
		printf("Ready queue is full. Cannot add more processes.\n");
		return;
	}
	ready_queue[ready_queue_size++] = pid;
	strcpy(get_process(pid)->state, "READY");
}

int dequeue_ready_queue() {
	if (ready_queue_size == 0) {
		printf("Ready queue is empty. Cannot remove a process.\n");
		return -1;
	}
	int front_pid = ready_queue[0];
	for (int i = 1; i < ready_queue_size; ++i) {
		ready_queue[i - 1] = ready_queue[i];
	}
	ready_queue_size--;
	strcpy(get_process(front_pid)->state, "RUNNING");
	return front_pid;
}

// SLEEP PRIORITY QUEUE DATA STRUCTURE
int sleep_queue[MAX_RUNNING_PROCESSES];
int sleep_queue_size = 0;

void enqueue_sleep_queue(int pid) {
	if (sleep_queue_size >= MAX_RUNNING_PROCESSES) {
		printf("Sleep queue is full. Cannot add more processes.\n");
		return;
	}
	Process *process_to_insert = get_process(pid);
	int i;
	for (i = 0; i < sleep_queue_size; ++i) {
		Process *process_in_queue = get_process(sleep_queue[i]);
		if (process_in_queue->sleep_timer > process_to_insert->sleep_timer) {
			break;
		}
	}
	for (int j = sleep_queue_size; j > i; --j) {
		sleep_queue[j] = sleep_queue[j - 1];
	}
	sleep_queue[i] = pid;
	sleep_queue_size++;
	strcpy(process_to_insert->state, "SLEEPING");
}

int dequeue_sleep_queue() {
	if (sleep_queue_size == 0) {
		printf("Sleep queue is empty. Cannot remove a process.\n");
		return -1;
	}
	int front_pid = sleep_queue[0];
	for (int i = 1; i < sleep_queue_size; ++i) {
		sleep_queue[i - 1] = sleep_queue[i];
	}
	sleep_queue_size--;
	return front_pid;
}

void decrement_sleep_timer_value(int time_elapsed) {
	for (int i = 0; i < sleep_queue_size; ++i) {
		int pid = sleep_queue[i];
		Process *process_in_queue = get_process(pid);
		process_in_queue->sleep_timer -= time_elapsed;
	}
}


// I/O PRIORITY QUEUE DATA STRUCTURE
int io_queue[MAX_RUNNING_PROCESSES];
int io_queue_size = 0;

void enqueue_io_queue(int pid) {
	if (io_queue_size >= MAX_RUNNING_PROCESSES) {
		printf("I/O Priority queue is full. Cannot add more processes.\n");
		return;
	}

	if (io_queue_size == 0) {
		io_queue[0] = pid;
		io_queue_size++;
		strcpy(get_process(pid)->state, "BLOCKING");
		return;
	}

	Process *process_to_insert = get_process(pid);
	int i;
	for (i = 1; i < io_queue_size; ++i) {
		Process *process_in_queue = get_process(io_queue[i]);
		if (process_in_queue->io_priority > process_to_insert->io_priority) break;
	}

	for (int j = io_queue_size; j > i; --j) {
		io_queue[j] = io_queue[j - 1];
	}

	io_queue[i] = pid;
	io_queue_size++;
	strcpy(process_to_insert->state, "BLOCKING");
}

int dequeue_io_queue() {
	if (io_queue_size == 0) {
		printf("I/O Priority queue is empty. Cannot remove a process.\n");
		return -1;
	}

	int front_pid = io_queue[0];
	for (int i = 1; i < io_queue_size; ++i) {
		io_queue[i - 1] = io_queue[i];
	}

	io_queue_size--;
	return front_pid;
}

void decrement_io_timer_value(int time_elapsed) {
	int remaining_time = time_elapsed;
	for (int i = 0; i < io_queue_size && remaining_time > 0; ++i) {
		int pid = io_queue[i];
		Process *process_in_queue = get_process(pid);
		process_in_queue->time_to_complete_io -= remaining_time;

		if (process_in_queue->time_to_complete_io < 0) {
			remaining_time = -process_in_queue->time_to_complete_io;
		} else {
			break;
		}
	}
}


// WAITING COLLECTION DATA STRUCTURE
int wait_collection[MAX_RUNNING_PROCESSES];
int wait_collection_size = 0;

bool in_wait_collection(int pid) {
	for (int i = 0; i < wait_collection_size; ++i) {
		if (pid == wait_collection[i]) return true;
	}
	return false;
}

void add_to_wait_collection(int pid) {
	if (wait_collection_size >= MAX_RUNNING_PROCESSES) {
		fprintf(stderr, "Error: Wait collection is full. Cannot add parent_pid: %d.\n", pid);
		return;
	}

	wait_collection[wait_collection_size++] = pid;
	strcpy(get_process(pid)->state, "WAITING");
}

int remove_from_wait_collection(int pid) {
	for (int i = 0; i < wait_collection_size; ++i) {
		if (wait_collection[i] == pid) {
			for (int j = i; j < wait_collection_size - 1; ++j) {
				wait_collection[j] = wait_collection[j + 1];
			}
			wait_collection_size--;
			return pid;
		}
	}
	return -1;
}


// JOB SCHEDULING QUEUE, used to move sleeping, io and wait processes back to ready queue
int job_scheduling_queue[MAX_RUNNING_PROCESSES];
int job_scheduling_queue_size = 0;

void enqueue_job_scheduling_queue(int pid) {
	if (job_scheduling_queue_size >= MAX_RUNNING_PROCESSES) {
		printf("Job scheduling queue is full. Cannot add more processes.\n");
		return;
	}

	Process *process = get_process(pid);
	int timer_value = strcmp(process->state, "SLEEPING") == 0 ?
					  process->sleep_timer : process->time_to_complete_io;

	int i;
	for (i = 0; i < job_scheduling_queue_size; ++i) {
		Process *process_in_queue = get_process(job_scheduling_queue[i]);
		int in_queue_timer_value = strcmp(process_in_queue->state, "SLEEPING") == 0 ?
								   process_in_queue->sleep_timer : process_in_queue->time_to_complete_io;

		if (in_queue_timer_value > timer_value) break;
	}

	for (int j = job_scheduling_queue_size; j > i; --j) {
		job_scheduling_queue[j] = job_scheduling_queue[j - 1];
	}

	job_scheduling_queue[i] = pid;
	job_scheduling_queue_size++;
}

int dequeue_job_scheduling_queue() {
	if (job_scheduling_queue_size == 0) {
		printf("Job scheduling queue is empty. Cannot remove a process.\n");
		return -1;
	}

	int front_pid = job_scheduling_queue[0];
	for (int i = 1; i < job_scheduling_queue_size; ++i) {
		job_scheduling_queue[i - 1] = job_scheduling_queue[i];
	}

	job_scheduling_queue_size--;
	return front_pid;
}


// Helper Functions

int compare_devices_read_speed(const void *a, const void *b) {
	const Device *deviceA = (const Device *)a;
	const Device *deviceB = (const Device *)b;
	return deviceB->readSpeed - deviceA->readSpeed;
}

// Used when processes transition from sleep queue to ready queue
void update_sleep_and_io_timers(int elapsed_time) {
	global_clock += elapsed_time;
	decrement_sleep_timer_value(elapsed_time);
	decrement_io_timer_value(elapsed_time);

	while (sleep_queue_size > 0 && get_process(sleep_queue[0])->sleep_timer <= 0) {
		enqueue_job_scheduling_queue(sleep_queue[0]);
		dequeue_sleep_queue();
	}

	while (io_queue_size > 0 && get_process(io_queue[0])->time_to_complete_io <= 0) {
		enqueue_job_scheduling_queue(io_queue[0]);
		dequeue_io_queue();
	}
}

// Used when processes transition from io queue to ready queue
void update_sleep_timer(int elapsed_time) {
	global_clock += elapsed_time;
	decrement_sleep_timer_value(elapsed_time);

	while (sleep_queue_size > 0 && get_process(sleep_queue[0])->sleep_timer <= 0) {
		enqueue_job_scheduling_queue(sleep_queue[0]);
		dequeue_sleep_queue();
	}
}

int allocate_lowest_id() {
	for (int i = 0; i < MAX_RUNNING_PROCESSES; ++i) {
		if (!used_ids[i]) {
			used_ids[i] = true;
			return i;
		}
	}
	fprintf(stderr, "Error: No available IDs.\n");
	exit(EXIT_FAILURE);
}

void release_id(int id) {
	if (id >= 0 && id < MAX_RUNNING_PROCESSES) {
		used_ids[id] = false;
	}

	// Find all children of that pid
	for(int i = 0; i < MAX_RUNNING_PROCESSES; i++) {
		Process *potential_child = get_process(used_ids[i]);
		if(potential_child->parent_pid == id) {
			potential_child->parent_pid = -1;
		}
	}

	process_terminated++;
}

int initialize_process(char command_string[], int parent_id) {
	Command *selected_command = NULL;

	for (int i = 0; i < command_count; ++i) {
		if (strcmp(command_definitions[i].name, command_string) == 0) {
			selected_command = &command_definitions[i];
			break;
		}
	}

	if (selected_command == NULL) {
		fprintf(stderr, "Command '%s' not found.\n", command_string);
		exit(EXIT_FAILURE);
	}

	int new_id = allocate_lowest_id();
	Process new_process = { .id = new_id,
			.command = selected_command,
			.syscall_index = 0,
			.cumulative_cpu_time = 0,
			.parent_pid = parent_id,
			.num_children = 0,
			.state = "READY" };

	processes[new_id] = new_process;
	process_created++;
	return new_id;
}


// Syscall Functions

void set_sleep_timer(Process *process, int sleep_duration) {
	int remaining_sleep_time = sleep_duration - TIME_CORE_STATE_TRANSITIONS;

	if (remaining_sleep_time < 0) { // if sleep timer is negative, put into job queue to be immediately executed
		process->sleep_timer = remaining_sleep_time;
	} else if (remaining_sleep_time == 0) { // if sleep timer is exactly 0, put into sleep queue, where CPU takes 1 second to attend to it
		process->sleep_timer = 0;
	} else { // if sleep timer is positive, put into sleep queue
		process->sleep_timer = remaining_sleep_time;
	}
	process->syscall_index++;
}

void handle_io_operation(Process *process, Syscall *syscall, Device *device) {
	unsigned long long bytes = syscall->bytes * 1000000;
	unsigned long long device_speed = (strcmp(syscall->name, "read") == 0) ? device->readSpeed : device->writeSpeed;
	process->time_to_complete_io = (bytes / device_speed) + TIME_ACQUIRE_BUS;

	if (bytes % device_speed != 0) process->time_to_complete_io++;

	process->syscall_index++;
}

void handle_spawn(Process *process, Syscall *syscall) {
	int child_pid = initialize_process(syscall->child_process_name, process->id);

	process->syscall_index++;
	process->num_children++;
	enqueue_ready_queue(child_pid);
	enqueue_ready_queue(process->id);
}

void handle_wait(Process *process) {
	process->syscall_index++;
	if (process->num_children == 0) {
		enqueue_ready_queue(process->id);
	} else {
		add_to_wait_collection(process->id);
	}
}

void handle_exit(Process *process) {
	// If parent exists
	if (used_ids[process->parent_pid]) {
		Process *parent = get_process(process->parent_pid);
		parent->num_children--;
		if (in_wait_collection(parent->id) && parent->num_children == 0) {
			remove_from_wait_collection(parent->id);
			strcpy(process->state, "WAITING");
			enqueue_job_scheduling_queue(parent->id);
		}
	}
	total_cpu_time += process->cumulative_cpu_time;
	process->syscall_index++;
	release_id(process->id);
}


// CPU Process Functions

void execute_syscall(Syscall *syscall, Process *process) {
	char *syscall_name = syscall->name;

	if (strcmp(syscall_name, "sleep") == 0) {
		set_sleep_timer(process, syscall->sleep_duration);
		update_sleep_and_io_timers(TIME_CORE_STATE_TRANSITIONS);

		if (process->sleep_timer < 0) {
			strcpy(process->state, "SLEEPING");
			enqueue_job_scheduling_queue(process->id);
		} else {
			enqueue_sleep_queue(process->id);
		}

	} else if (strcmp(syscall_name, "read") == 0 || strcmp(syscall_name, "write") == 0) {
		Device *device = get_device(syscall->device_name);
		process->io_priority = device->priority;
		handle_io_operation(process, syscall, device);
		update_sleep_and_io_timers(TIME_CORE_STATE_TRANSITIONS);
		enqueue_io_queue(process->id);

	} else if (strcmp(syscall_name, "spawn") == 0) {
		handle_spawn(process, syscall);
		update_sleep_and_io_timers(TIME_CORE_STATE_TRANSITIONS);

	} else if (strcmp(syscall_name, "wait") == 0) {
		handle_wait(process);
		update_sleep_and_io_timers(TIME_CORE_STATE_TRANSITIONS);
	} else if (strcmp(syscall_name, "exit") == 0) {
		handle_exit(process);
	}
}

void execute_syscall_and_update_time(Process *process, Syscall *syscall, int elapsed_syscall_time) {
	int time_spent_in_cpu = elapsed_syscall_time - process->cumulative_cpu_time;
	process->cumulative_cpu_time = elapsed_syscall_time;
	update_sleep_and_io_timers(time_spent_in_cpu + 1);
	execute_syscall(syscall, process);
}

void handle_time_quantum_reached(Process *process) {
	int time_spent_in_cpu = time_quantum;
	process->cumulative_cpu_time += time_quantum;

	update_sleep_and_io_timers(time_spent_in_cpu);

	enqueue_ready_queue(process->id);
	update_sleep_and_io_timers(TIME_CORE_STATE_TRANSITIONS);
}

void compute_process_and_run_syscall(int pid) {
	if (pid == -1) {
		return;
	}

	Process *process = get_process(pid);
	Syscall *syscall_to_be_executed = &process->command->syscalls[process->syscall_index];
	int elapsed_syscall_time = syscall_to_be_executed->time_to_trigger;

	// Execute Syscall
	if (process->cumulative_cpu_time + time_quantum > elapsed_syscall_time) {
		execute_syscall_and_update_time(process, syscall_to_be_executed, elapsed_syscall_time);
	} else {
		// Time Quantum reached
		handle_time_quantum_reached(process);
	}
}

void perform_job(int pid) {
	if (strcmp(get_process(pid)->state, "SLEEPING") == 0) {
		update_sleep_and_io_timers(TIME_CORE_STATE_TRANSITIONS);
	} else if (strcmp(get_process(pid)->state, "BLOCKING") == 0) {
		update_sleep_timer(TIME_CORE_STATE_TRANSITIONS);
	} else if (strcmp(get_process(pid)->state, "WAITING") == 0) {
		update_sleep_and_io_timers(TIME_CORE_STATE_TRANSITIONS);
	}
	enqueue_ready_queue(pid);
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
		if (line[0] == CHAR_COMMENT || line[0] == '\n') continue;

		// Handle 'device' lines
		if (strncmp(line, "device", 6) == 0) {
			sscanf(line, "device %s %lluBps %lluBps", devices_array[device_count].name,
				   &devices_array[device_count].readSpeed, &devices_array[device_count].writeSpeed);
			device_count++;
		}

		// Handle 'timequantum' line
		if (strncmp(line, "timequantum", 11) == 0) {
			sscanf(line, "timequantum %i", &time_quantum);
		}
	}
	fclose(file);

	// Sort devices by read speed and assign priorities
	qsort(devices_array, device_count, sizeof(Device), compare_devices_read_speed);
	for (int i = 0; i < device_count; ++i) {
		devices_array[i].priority = i + 1;
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
		if (line[0] == CHAR_COMMENT || line[0] == '\n') continue;

		// New command, register its definition
		if (line[0] != '\t') {
			sscanf(line, "%s", command_definitions[command_count].name);
			command_definitions[command_count].syscall_count = 0;
			command_count++;
		} else {
			// Operation for the latest command
			Command *current_command = &command_definitions[command_count - 1];
			Syscall *next_operation = &current_command->syscalls[current_command->syscall_count];
			int initialTime;
			char operationName[10];
			sscanf(line, "\t%iusecs %s", &initialTime, operationName);

			if (strcmp(operationName, "sleep") == 0) {
				sscanf(line, "\t%iusecs %s %iusecs", &next_operation->time_to_trigger, next_operation->name, &next_operation->sleep_duration);
			} else if (strcmp(operationName, "exit") == 0) {
				sscanf(line, "\t%iusecs %s", &next_operation->time_to_trigger, next_operation->name);
			} else if (strcmp(operationName, "read") == 0 || strcmp(operationName, "write") == 0) {
				sscanf(line, "\t%iusecs %s %s %lluB", &next_operation->time_to_trigger, next_operation->name, next_operation->device_name, &next_operation->bytes);
			} else if (strcmp(operationName, "spawn") == 0) {
				sscanf(line, "\t%iusecs %s %s", &next_operation->time_to_trigger, next_operation->name, next_operation->child_process_name);
			} else if (strcmp(operationName, "wait") == 0) {
				sscanf(line, "\t%iusecs %s", &next_operation->time_to_trigger, next_operation->name);
			}
			current_command->syscall_count++;
		}
	}
	fclose(file);
}

void execute_commands(void) {
	// Initialize root process using the first command in the command file
	int rootPID = initialize_process(command_definitions[0].name, -1);

	// Add root process to ready queue
	enqueue_ready_queue(rootPID);

	// Process Ready Queue
	while (process_created != process_terminated) {
		if (ready_queue_size != 0) {

			// Process from the ready queue
			int cpuProcess = dequeue_ready_queue();
			update_sleep_and_io_timers(TIME_CONTEXT_SWITCH);
			compute_process_and_run_syscall(cpuProcess);

			// Perform jobs
			while (job_scheduling_queue_size != 0) {
				int job = dequeue_job_scheduling_queue();
				perform_job(job);
			}


		} else {
			int lowerTimerValue;
			int processPicked;
			bool hasSleepTimer = false, hasIoTimer = false;

			// Check sleep queue
			if (sleep_queue_size != 0) {
				int frontSleepPID = sleep_queue[0];
				Process *sleepProcess = get_process(frontSleepPID);
				lowerTimerValue = sleepProcess->sleep_timer;
				hasSleepTimer = true;
				processPicked = sleepProcess->id;
			}

			// Check IO queue
			if (io_queue_size != 0) {
				int frontIoPID = io_queue[0];
				Process *ioProcess = get_process(frontIoPID);

				// Find the lower timer value
				if (!hasSleepTimer || ioProcess->time_to_complete_io < lowerTimerValue) {
					lowerTimerValue = ioProcess->time_to_complete_io;
					processPicked = ioProcess->id;
				}
				hasIoTimer = true;
			}

			// Synchronize other components if required
			if (hasSleepTimer || hasIoTimer) {

				if (strcmp(get_process(processPicked)->state, "SLEEPING") == 0) {
					update_sleep_and_io_timers(lowerTimerValue + 1);
				}
				else if (strcmp(get_process(processPicked)->state, "BLOCKING") == 0) {
					update_sleep_and_io_timers(lowerTimerValue);
				}

				// Perform jobs
				while (job_scheduling_queue_size != 0) {
					int job = dequeue_job_scheduling_queue();
					perform_job(job);
				}

			}

		}
	}
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
	printf("measurements  %i %i\n", global_clock, (total_cpu_time * 100 / global_clock));
	exit(EXIT_SUCCESS);
}