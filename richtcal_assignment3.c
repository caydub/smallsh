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
int fg_only_mode = 0; // tracks whether shell is in foreground-only mode

/* ----------------------------------------------------------------
Function freeProcesses: Free the memory of all background processes
in the linked list
args~
None
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
status of a process and print a status message
args~
- wstatus:			Termination status				(int)
returns~
0 if successful
------------------------------------------------------------------- */
int interpretTerminationStatus (int wstatus)
{
	// if exited normally
	if (WIFEXITED(wstatus)) {
		printf("exit value %d\n", WEXITSTATUS(wstatus));
		fflush(stdout);
	// if exited abnormally
	} else {
		printf("terminated by signal %d\n", WTERMSIG(wstatus));
		fflush(stdout);
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
	while (process != NULL) {
		pid_t bg_pid = waitpid(process->pid, &last_bg_child_status, WNOHANG);
		if (bg_pid > 0) {
			printf("background pid %d is done: ", bg_pid);
			interpretTerminationStatus(last_bg_child_status);
			fflush(stdout);
			process = process->next;
			removeProcess(bg_pid);
			continue;
		}
		process = process->next;
	}
	return 0;
}

/* ----------------------------------------------------------------
Function killAllProcesses: Terminate all processes
args~
None
returns~
0 when complete
side effects~
exit_flag is set, terminating the shell when returned to main
------------------------------------------------------------------- */
int killAllProcesses()
{
	struct process* temp;
	while (head != NULL) {
    	kill(head->pid, SIGTERM);
		temp = head;
		head = head->next;
		free(temp); // free each node as we go
    }
	exit_flag = 1;
	return 0;
}

/* ----------------------------------------------------------------
Function handle_SIGTSTP: Signal handler for SIGTSTP. Toggles
foreground-only mode on and off.
args~
- signo:		Signal number		(int)
returns~
Nothing.
------------------------------------------------------------------- */
void handle_SIGTSTP(int signo)
{
	// use write() instead of printf() - printf is not reentrant
	if (fg_only_mode == 0) {
		write(STDOUT_FILENO, "\nEntering foreground-only mode (& is now ignored)\n: ", 52);
		fg_only_mode = 1;
	} else {
		write(STDOUT_FILENO, "\nExiting foreground-only mode\n: ", 32);
		fg_only_mode = 0;
	}
}

/* ----------------------------------------------------------------
Function changeDirectory: Changes the working directory of smallsh
args~
- curr_command:			Parsed Inline Command			(struct command_line*)
returns~
0 if successful, -1 if not
------------------------------------------------------------------- */
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

/* ----------------------------------------------------------------
Function printStatus: Prints out exit status or terminating signal
of last ran fg process
args~
None
returns~
0 if successful
------------------------------------------------------------------- */
int printStatus ()
{
	interpretTerminationStatus(last_fg_child_status);
	fflush(stdout);
	return 0;
}

/* ----------------------------------------------------------------
Function handleBuiltInCommands: Determine if command is a built in.
If it is, then handle accordingly
args~
- curr_command:			Parsed Inline Command			(struct command_line*)
returns~
1 if command is a built in function, 0 otherwise
side effects~
exit_flag is set if exit command is received, terminating the shell
when returned to main
------------------------------------------------------------------- */
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

/* ----------------------------------------------------------------
Function createArgArray: Create an array containing arguments that
is readable for execv functions. The input array is obtained from
the curr_command member argv
args~
- curr_command:			Parsed inline command				(struct command_line*)
- execvp_args:			Array to transfer arguments into	(char**)
------------------------------------------------------------------- */
int createArgArray(struct command_line* curr_command, char* execvp_args[MAX_ARGS + 1])
{
	for (int i = 0; i < curr_command->argc; i++) {
		execvp_args[i] = strdup(curr_command->argv[i]);
	}
	execvp_args[curr_command->argc] = NULL; // set last value of array to null
	return 0;
}

/* ----------------------------------------------------------------
Function freeArgArray: Free memory allocated for arg array
args~
- execvp_args:			Array to free		(char**)
- arr_count:			Number of elements	(int)
------------------------------------------------------------------- */
int freeArgArray(char** execvp_args, int arr_count)
{
	for (int i = 0; i < arr_count; i++) {
		free(execvp_args[i]);
	}
	return 0;
}

/* ----------------------------------------------------------------
Function freeCommand: Free all memory associated with a parsed
command structure
args~
- curr_command:			Parsed Inline Command			(struct command_line*)
returns~
0 when complete
------------------------------------------------------------------- */
int freeCommand(struct command_line* curr_command)
{
	for (int i = 0; i < curr_command->argc; i++) {
		free(curr_command->argv[i]);
	}
	if (curr_command->input_file != NULL) {
		free(curr_command->input_file);
	}
	if (curr_command->output_file != NULL) {
		free(curr_command->output_file);
	}
	free(curr_command);
	return 0;
}

/* ----------------------------------------------------------------
Function redirectInput: Redirects stdin into an input file
args~
- curr_command:			Parsed Inline Command			(struct command_line*)
returns~
0 if successful, -1 if not
------------------------------------------------------------------- */
int redirectInput(struct command_line* curr_command) {
	int source_fd;
	int redirect_input_fd;

	if (curr_command->input_file != NULL) {
		source_fd = open(curr_command->input_file, O_RDONLY, 0644);
		if (source_fd == -1) { 
			perror("source open()");
			last_fg_child_status = 1;
			return -1;
		}
		redirect_input_fd = dup2(source_fd, 0); // point stdin to input file
		close(source_fd); // close original fd, stdin now handles it
		if (redirect_input_fd == -1) { 
			perror("source dup2()");
			last_fg_child_status = 1;
			return -1;
		}
	} else {
		source_fd = open("/dev/null", O_RDONLY, 0644);
		if (source_fd == -1) { 
			perror("source open()");
			return -1;
		}
		redirect_input_fd = dup2(source_fd, 0); // point stdin to /dev/null
		close(source_fd); // close original fd
		if (redirect_input_fd == -1) { 
			perror("source dup2()");
			return -1;
		}
	} 
	return 0;
}

/* ----------------------------------------------------------------
Function redirectOutput: Redirects stdout into an output file
args~
- curr_command:			Parsed Inline Command			(struct command_line*)
returns~
0 if successful, -1 if not
------------------------------------------------------------------- */
int redirectOutput(struct command_line* curr_command) {
	int dest_fd;
	int redirect_output_fd;

	if (curr_command->output_file != NULL) {
		dest_fd = open(curr_command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (dest_fd == -1) { 
			perror("source open()");
			last_fg_child_status = 1;
			return -1;
		}
		redirect_output_fd = dup2(dest_fd, 1); // point stdout to output file
		close(dest_fd); // close original fd, stdout now handles it
		if (redirect_output_fd == -1) { 
			perror("source dup2()");
			last_fg_child_status = 1;
			return -1;
		} 
	} else {
		dest_fd = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (dest_fd == -1) { 
			perror("source open()");
			return -1;
		}
		redirect_output_fd = dup2(dest_fd, 1); // point stdout to /dev/null
		close(dest_fd); // close original fd
		if (redirect_output_fd == -1) { 
			perror("source dup2()");
			return -1;
		}
	} 
	return 0;
}

/* ----------------------------------------------------------------
Function handleCommands: Determine if command is valid. If it is,
then handle accordingly. This function is not for built in commands
args~
- curr_command:			Parsed Inline Command			(struct command_line*)
------------------------------------------------------------------- */
int handleCommands(struct command_line* curr_command)
{
	char* execvp_args[MAX_ARGS + 1]; // fixed size array instead of VLA
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
				if (redirectInput(curr_command) == -1) {
					exit(EXIT_FAILURE);
				}
			}

			/* 
			* redirect stdout if there is an output file, and if there isn't,
			* redirect the stdout to "/dev/null" if bg process
			*/ 
			if (curr_command->output_file != NULL || curr_command->is_bg) {
				if (redirectOutput(curr_command) == -1) {
					exit(EXIT_FAILURE);
				}
			}

			// restore default SIGINT for foreground children
			struct sigaction default_SIGINT = {0};
				default_SIGINT.sa_handler = SIG_DFL;
				default_SIGINT.sa_flags = 0;

			// restore default SIGTSTP for all children
			struct sigaction default_SIGTSTP = {0};
				default_SIGTSTP.sa_handler = SIG_IGN;
				default_SIGTSTP.sa_flags = 0;

			sigaction(SIGTSTP, &default_SIGTSTP, NULL);

			if (!curr_command->is_bg) {
				sigaction(SIGINT, &default_SIGINT, NULL);
			}
			
			// execute new process
			execvp(curr_command->argv[0], execvp_args);
			perror("execvp");	// this line onwards is executed if execvp fails
			exit(EXIT_FAILURE);
			break;

		default: // parent process executes this branch
			if (curr_command->is_bg) {
				addProcess(spawn_pid);
				printf("background pid is %d\n", spawn_pid);
				fflush(stdout);
				waitpid(spawn_pid, &last_bg_child_status, WNOHANG);
			} else {
				// parent waits for foreground child
				waitpid(spawn_pid, &last_fg_child_status, 0);
				// if foreground child was killed by signal, print signal number
				if (WIFSIGNALED(last_fg_child_status)) {
					printf("terminated by signal %d\n", WTERMSIG(last_fg_child_status));
					fflush(stdout);
				}
			}
			break;
	}

	freeArgArray(execvp_args, curr_command->argc);
	return 0;
}

/* ----------------------------------------------------------------
Function expandPID: Replaces all occurrences of $$ in a string
with the shell's process ID
args~
- input:		Input string to expand		(char*)
- pid:			Shell process ID			(pid_t)
returns~
Newly allocated expanded string.
------------------------------------------------------------------- */
char* expandPID(char* input, pid_t pid)
{
	char pid_str[16];
	snprintf(pid_str, sizeof(pid_str), "%d", pid);
	int pid_len = strlen(pid_str);

	// count occurrences of $$ to determine new string length
	int count = 0;
	for (int i = 0; input[i] != '\0'; i++) {
		if (input[i] == '$' && input[i + 1] == '$') {
			count++;
			i++; // skip second $
		}
	}

	// allocate new string with enough space for all expansions
	int new_len = strlen(input) + count * (pid_len - 2) + 1;
	char* result = malloc(new_len);
	memset(result, '\0', new_len);

	// build expanded string
	int j = 0;
	for (int i = 0; input[i] != '\0'; i++) {
		if (input[i] == '$' && input[i + 1] == '$') {
			// replace $$ with pid
			strcat(result, pid_str);
			j += pid_len;
			i++; // skip second $
		} else {
			result[j++] = input[i];
		}
	}
	return result;
}

/* ----------------------------------------------------------------
Function parse_input: Parse inline commands inputted into shell
args~
- shell_pid:		Shell process ID		(pid_t)
returns~
- curr_command:		Parsed Inline Command	(struct command_line*)
------------------------------------------------------------------- */
struct command_line *parse_input(pid_t shell_pid)
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
		// expand $$ to shell pid before storing any token
		char* expanded = expandPID(token, shell_pid);

		// parse input while ignoring special characters
		if(!strcmp(token,"<")) { // if redirect input later on
			char* next = strtok(NULL," \n");
			curr_command->input_file = expandPID(next, shell_pid);
		} else if(!strcmp(token,">")) { // if redirect output later on
			char* next = strtok(NULL," \n");
			curr_command->output_file = expandPID(next, shell_pid);
		} else if(!strcmp(token,"&")) { // if process will run in the background
			curr_command->is_bg = true;
			free(expanded);
		} else {
			curr_command->argv[curr_command->argc++] = expanded;
			expanded = NULL; // ownership transferred, don't double free
		}

		if (expanded != NULL) {
			free(expanded);
		}

		token=strtok(NULL," \n");
	}

	return curr_command;
}

int main()
{
	pid_t shell_pid = getpid(); // get shell pid for $$ expansion
	struct command_line *curr_command;

	// set up SIGINT handler - shell ignores it
	struct sigaction ignore_SIGINT = {0};
		ignore_SIGINT.sa_handler = SIG_IGN;
		ignore_SIGINT.sa_flags = 0;
	sigaction(SIGINT, &ignore_SIGINT, NULL);

	// set up SIGTSTP handler - toggles foreground-only mode
	struct sigaction SIGTSTP_action = {0};
		SIGTSTP_action.sa_handler = handle_SIGTSTP;
		SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	while(!exit_flag)
	{
		// check background processes
		checkBgs();

		curr_command = parse_input(shell_pid);

		if (curr_command->is_empty) {
			freeCommand(curr_command);
			continue;
		}

		if (!strcmp(curr_command->argv[0], "#") || curr_command->argv[0][0] == '#') {
			freeCommand(curr_command);
			continue;
		}

		// if in foreground-only mode, ignore & and run in foreground
		if (fg_only_mode && curr_command->is_bg) {
			curr_command->is_bg = false;
		}

		if (handleBuiltInCommands(curr_command)) {
			freeCommand(curr_command);
			continue;
		}

		handleCommands(curr_command);
		freeCommand(curr_command);
	}
	return EXIT_SUCCESS;
}