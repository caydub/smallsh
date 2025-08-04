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
Linked list for managing processes
------------------------------------------------------------------- */
struct process
{
	int pid;
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
};

/* ---------- Global Variables ---------- */
struct process* head = NULL;
int child_status;
int exit_flag = 0;

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
		free(head);
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
			return -1;
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
	char *built_in_functions[] = {"exit", "cd", "status", "#"};
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
Function handleCommands: Determine if command is valid. If it is, then
handle accordingly. This function is not for built in commands
args ~
- curr_command:			Parsed Inline Command			(struct command_line*)
----------------------------------------------------------------------------------------------- */
int handleCommands(struct command_line* curr_command)
{
	char* execvp_args[curr_command->argc]; // remember to free this array
	createArgArray(curr_command, execvp_args);
    pid_t spawn_pid = fork();

	switch (spawn_pid) {
		case -1: // error forking
			perror("fork()");
			exit(EXIT_FAILURE);
			break;

		case 0:	// child process executes this branch
			// redirect stdin if there is an input file
			if (curr_command->input_file != NULL) {
				int source_fd = open(curr_command->input_file, O_RDONLY, 0644);
				if (source_fd == -1) { 
    				perror("source open()");
					child_status = 1;
  				}
				int redirect_input_fd = dup2(source_fd, 0); // point stdin to input file
				if (redirect_input_fd == -1) { 
    				perror("source dup2()");
					child_status = 1;
  				}
			}

			// redirect stdout if there is an output file
			if (curr_command->output_file != NULL) {
				int dest_fd = open(curr_command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (dest_fd == -1) { 
    				perror("dest open()"); 
    				child_status = 1;
  				}
				int redirect_output_fd = dup2(dest_fd, 1); // point stdout to output file
				if (redirect_output_fd == -1) { 
    				perror("dest dup2()"); 
    				child_status = 1; 
  				}
			}

			// execute new process
			execvp(curr_command->argv[0], execvp_args);
			perror("execvp");	// this line is executed if execvp fails
			child_status = 1;
			exit(EXIT_FAILURE);
			break;

		default: // parent process executes this branch
			spawn_pid = waitpid(spawn_pid, &child_status, 0);
			break;
	}

	freeArgArray(execvp_args, curr_command->argc);
	return 0;
}

/* --------------------------------------------------------------------------------------------
Function command_line: Parse inline commands inputted into shell
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

	// Get input
	printf(": ");
	fflush(stdout);
	fgets(input, INPUT_LENGTH, stdin);

	// Tokenize the input
	char *token = strtok(input, " \n");
	while(token){
		if(!strcmp(token,"<")){
			curr_command->input_file = strdup(strtok(NULL," \n"));
		} else if(!strcmp(token,">")){
			curr_command->output_file = strdup(strtok(NULL," \n"));
		} else if(!strcmp(token,"&")){
			curr_command->is_bg = true;
		} else{
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
		curr_command = parse_input();

		if (!strcmp(curr_command->argv[0], "#") || handleBuiltInCommands(curr_command)) {
			continue;
		}

		handleCommands(curr_command);
	}
	return EXIT_SUCCESS;
}
