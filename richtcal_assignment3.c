#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>

/* --------------- MACROS --------------- */
#define INPUT_LENGTH 	 2048
#define MAX_ARGS		 512

/* ----------------------------------------------------------------
Linked list for managing background processes
------------------------------------------------------------------- */
struct process
{
	pid_t pid;
	struct process* next;
};

/* ---------------------------------------------------------------
Structure for storing parsed inline commands
------------------------------------------------------------------ */
struct command_line
{
	char *argv[MAX_ARGS + 1];
	int argc;
	char *input_file;
	char *output_file;
	bool is_bg;
	bool is_empty;
};

/* ---------- Global Variables ---------- */
struct process* head = NULL;
struct process* tail = NULL;
int last_fg_child_status = 0;
int last_bg_child_status;
int exit_flag = 0;

/* ----------------------------------------------------------------
Function freeProcesses: Free the memory of all background processes
in the linked list
args~
- process:			Background process		(struct process*)
returns~
0 when complete
------------------------------------------------------------------- */
int freeProcesses ()
{
	struct process* process = head;
	struct process* next_process;

	while(process != NULL) {
		next_process = process->next;
    	free(process);
		process = next_process;
  }
	return 0;
}

/* ----------------------------------------------------------------
Function addProcess: Create a process structure using dynamic
memory allocation
args~
- pid:		Process ID		(int)
returns~
0 when complete
------------------------------------------------------------------- */
int addProcess (pid_t pid)
{
	struct process* curr_process = malloc(sizeof(struct process));
	curr_process->pid = pid;
	curr_process->next = NULL;

    if(head == NULL) {
        head = curr_process;
		tail = curr_process;
    } else {
        tail->next = curr_process;
        tail = curr_process;
    }
	return 0;
}

/* ----------------------------------------------------------------
Function removeProcess: Remove a process structure from the 
linked list
args~
- pid:		Process ID		(int)
returns~
0 when complete
------------------------------------------------------------------- */
int removeProcess (pid_t pid)
{
	struct process* process = head;
	struct process* prev_process = NULL;

	while (process != NULL) {
		if (process->pid == pid && prev_process == NULL) {
			head = process->next;
			if (head == NULL) {
				tail = NULL;
			}
			free(process);
			process = NULL;
		} else if (process->pid == pid) {
			prev_process->next = process->next;
			if (prev_process->next == NULL) {
				tail = prev_process;
			}
			free(process);
			process = NULL;
		} else {
			prev_process = process;
			process = process->next;
		}
	}
	return 0;
}

/* ----------------------------------------------------------------
Function interpretTerminationStatus: Interpret the termination
status of a process and return a string containing a status
message
args~
- wstatus:			Termination status				(int)
- msg_buffer:		String to store result			(char*)
returns~
0 if successful
------------------------------------------------------------------- */
int interpretTerminationStatus (int wstatus, char msg_buffer[32])
{
	char exit_message[22] = "exit value ";
	char signal_message[32] = "terminated by signal ";
	char convertedStatus[10];

	// if exited normally
	if (WIFEXITED(wstatus)) {
		sprintf(convertedStatus, "%d", WEXITSTATUS(wstatus));
		strcpy(msg_buffer, strcat(exit_message, convertedStatus));
	// if exited abormally
	} else { 
		sprintf(convertedStatus, "%d", WTERMSIG(wstatus));
		strcpy(msg_buffer, strcat(signal_message, convertedStatus));
	}
	return 0;
}

/* ----------------------------------------------------------------
Function checkBgs: Check bg processes to see if any have terminated
args~
None
returns~
0 when complete
------------------------------------------------------------------- */
int checkBgs () 
{
	struct process* process = head;
	char status_message[32];

	while (process != NULL) {
		pid_t bg_pid = waitpid(process->pid, &last_bg_child_status, WNOHANG);
		if (bg_pid) {
			interpretTerminationStatus(last_bg_child_status, status_message);
			printf("background pid %d is done: %s\n", bg_pid, status_message);
			fflush(stdout);
			process = process->next;
			removeProcess(bg_pid);
			continue;
		}
		process = process->next;
	}
}

/* --------------------------------------------------------------------------------------------
Function killAllProcesses: Terminate all processes
args ~
None
returns ~
0 when complete
side effects ~
exit_flag is set, terminating the shell when returned to main
----------------------------------------------------------------------------------------------- */
int killAllProcesses()
{
	while (head != NULL) {
    	kill(head->pid, SIGTERM);
		head = head->next; 
    }
	exit_flag = 1;
	return 0;
}

/* --------------------------------------------------------------------------------------------
Function changeDirectory: Changes the working directory of smallsh
args ~
- curr_command:			Parsed Inline Command			(struct command_line*)
returns ~
0 if successful, -1 if not
----------------------------------------------------------------------------------------------- */
int changeDirectory (struct command_line* curr_command)
{
	if (curr_command->argc > 1) {
		if (chdir(curr_command->argv[1]) == -1) {
			perror("Error");
		} 
	} else {
		chdir(getenv("HOME"));
	}
	return 0;
}

/* --------------------------------------------------------------------------------------------
Function printStatus: Prints out exit status or terminating signal of last ran fg process
args ~
None
returns ~
0 if successful
----------------------------------------------------------------------------------------------- */
int printStatus ()
{
	char status_message[32];
	interpretTerminationStatus(last_fg_child_status, status_message);
	printf("%s\n", status_message);
	fflush(stdout);
	return 0;
}

/* --------------------------------------------------------------------------------------------
Function handleBuiltInCommands: Determine if command is a built in. If it is, then
handle accordingly
args ~
- curr_command:			Parsed Inline Command			(struct command_line*)
returns ~
1 if command is a built in function, 0 otherwise
side effects ~
exit_flag is set if exit command is received, terminating the shell when returned to main
----------------------------------------------------------------------------------------------- */
int handleBuiltInCommands(struct command_line* curr_command)
{
	char *built_in_functions[] = {"exit", "cd", "status"};
	int i;
    for (i = 0; i < 3; i++) {
        if (!strcmp(built_in_functions[i], curr_command->argv[0])) {
            switch (i) {
				case 0: // exit
					killAllProcesses();	
					break;
				case 1: // change directory
					changeDirectory(curr_command);
					break;
				case 2: // print last fg exit status/terminating signal
					printStatus();
					break;
			}
			return 1;
        }
    }
    return 0;
}

/* --------------------------------------------------------------------------------------------
Function createArgArray: Create an array containing arguments that is readable for
execv functions. The input array is obtained from the curr_command member argv
args ~
- curr_command:			Parsed inline command				(struct command_line*)
- execvp_args:			Array to transfer arguments into	(char**)
----------------------------------------------------------------------------------------------- */
int createArgArray(struct command_line* curr_command, char* execvp_args[curr_command->argc])
{
	for (int i = 0; i < curr_command->argc; i++) {
		execvp_args[i] = strdup(curr_command->argv[i]);
	}
	execvp_args[curr_command->argc] = NULL; // set last value of array to null
	return 0;
}

/* --------------------------------------------------------------------------------------------
Function freeArgArray: Free memory allocated for arg array
args ~
- execvp_args:			Array to free		(char**)
----------------------------------------------------------------------------------------------- */
int freeArgArray(char** execvp_args, int arr_count)
{
	for (int i = 0; i < arr_count; i++) {
		free(execvp_args[i]);
	}
	return 0;
}

/* --------------------------------------------------------------------------------------------
Function redirectInput: Redirects stdin into an input file
args ~
- curr_command:			Parsed Inline Command			(struct command_line*)
----------------------------------------------------------------------------------------------- */
int redirectInput(struct command_line* curr_command) {
	int source_fd;
	int redirect_input_fd;

	if (curr_command->input_file != NULL) {
		source_fd = open(curr_command->input_file, O_RDONLY, 0644);
		if (source_fd == -1) { 
			perror("source open()");
			if (curr_command->is_bg) {
				last_bg_child_status = 1;
			} else {
				last_fg_child_status = 1;
			}
		}
		redirect_input_fd = dup2(source_fd, 0); // point stdin to input file
		if (redirect_input_fd == -1) { 
			perror("source dup2()");
			if (curr_command->is_bg) {
				last_bg_child_status = 1;
			} else {
				last_fg_child_status = 1;
			}
		}
	} else {
		source_fd = open("/dev/null", O_RDONLY, 0644);
		if (source_fd == -1) { 
			perror("source open()");
			last_bg_child_status = 1;
		}
		redirect_input_fd = dup2(source_fd, 0); // point stdin to input file
		if (redirect_input_fd == -1) { 
			perror("source dup2()");
			last_bg_child_status = 1;
		}
	} 
	return 0;
}

/* --------------------------------------------------------------------------------------------
Function redirectOutput: Redirects stdout into an output file
args ~
- curr_command:			Parsed Inline Command			(struct command_line*)
----------------------------------------------------------------------------------------------- */
int redirectOutput(struct command_line* curr_command) {
	int dest_fd;
	int redirect_output_fd;

	if (curr_command->output_file != NULL) {
		dest_fd = open(curr_command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (dest_fd == -1) { 
			perror("source open()");
			if (curr_command->is_bg) {
				last_bg_child_status = 1;
			} else {
				last_fg_child_status = 1;
			}
		}
		redirect_output_fd = dup2(dest_fd, 1); // point stdout to output file
		if (redirect_output_fd == -1) { 
			perror("source dup2()");
			if (curr_command->is_bg) {
				last_bg_child_status = 1;
			} else {
				last_fg_child_status = 1;
			}
		} 
	} else {
		dest_fd = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (dest_fd == -1) { 
			perror("source open()");
			last_bg_child_status = 1;
		}
		redirect_output_fd = dup2(dest_fd, 1); // point stdout to output file
		if (redirect_output_fd == -1) { 
			perror("source dup2()");
			last_bg_child_status = 1;
		}
	} 
	return 0;
}

/* --------------------------------------------------------------------------------------------
Function handleCommands: Determine if command is valid. If it is, then
handle accordingly. This function is not for built in commands
args ~
- curr_command:			Parsed Inline Command			(struct command_line*)
----------------------------------------------------------------------------------------------- */
int handleCommands(struct command_line* curr_command)
{
	char* execvp_args[curr_command->argc];
	createArgArray(curr_command, execvp_args);
    pid_t spawn_pid = fork();

	switch (spawn_pid) {
		case -1: // error forking
			perror("fork()");
			exit(EXIT_FAILURE);
			break;

		case 0:	// child process executes this branch
			/* 
			* redirect stdin if there is an input file, and if there isn't,
			* redirect the stdin to "/dev/null" if bg process
			*/ 
			if (curr_command->input_file != NULL || curr_command->is_bg) {
				redirectInput(curr_command);
			}

			/* 
			* redirect stdout if there is an output file, and if there isn't,
			* redirect the stdout to "/dev/null" if bg process
			*/ 
			if (curr_command->output_file != NULL || curr_command->is_bg) {
				redirectOutput(curr_command);
			}

			// execute new process
			execvp(curr_command->argv[0], execvp_args);
			perror("execvp");	// this line onwards is executed if execvp fails
			if (curr_command->is_bg) {
				last_bg_child_status = 1;
			} else {
				last_fg_child_status = 1;
			}
			exit(EXIT_FAILURE);
			break;

		default: // parent process executes this branch
			if (curr_command->is_bg) {
				printf("background pid is %d\n", spawn_pid);
				addProcess(spawn_pid);
				spawn_pid = waitpid(spawn_pid, &last_bg_child_status, WNOHANG);
			} else {
				spawn_pid = waitpid(spawn_pid, &last_fg_child_status, 0);
			}
			break;
	}

	freeArgArray(execvp_args, curr_command->argc);
	return 0;
}

/* --------------------------------------------------------------------------------------------
Function parse_input: Parse inline commands inputted into shell
args ~
None
returns ~
- curr_command:		Parsed Inline Command	(struct command_line*)
----------------------------------------------------------------------------------------------- */
struct command_line *parse_input()
{
	char input[INPUT_LENGTH];
	struct command_line *curr_command = (struct command_line *) calloc(1, sizeof(struct command_line));
	curr_command->input_file = NULL;
	curr_command->output_file = NULL;
	curr_command->is_bg = false;
	curr_command->is_empty = true;

	// retrieve input from terminal
	printf(": ");
	fflush(stdout);
	fgets(input, INPUT_LENGTH, stdin);

	// parse input
	char *token = strtok(input, " \n");
	if (token) {
		curr_command->is_empty = false;
	}
	while(token) {
		// parse input while ignoring special characters
		if(!strcmp(token,"<")) { // if redirect input later on
			curr_command->input_file = strdup(strtok(NULL," \n")); 
		} else if(!strcmp(token,">")) { // if redirect output later on
			curr_command->output_file = strdup(strtok(NULL," \n")); 
		} else if(!strcmp(token,"&")) { // if process will run in the background
			curr_command->is_bg = true; 
		} else {
			curr_command->argv[curr_command->argc++] = strdup(token);
		}
		token=strtok(NULL," \n");
	}

	return curr_command;
}

int main()
{
	struct command_line *curr_command;

	while(!exit_flag)
	{
		// check background processes
		checkBgs();

		curr_command = parse_input();

		if (curr_command->is_empty) {
			continue;
		}

		if (!strcmp(curr_command->argv[0], "#") || curr_command->argv[0][0] == '#') {
			continue;
		}

		if (handleBuiltInCommands(curr_command)) {
			continue;
		}

		handleCommands(curr_command);
	}
	return EXIT_SUCCESS;
}
