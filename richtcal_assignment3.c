#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

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
int last_status;

/* --------------------------------------------------------------------------------------------
Function killAllProcesses: Terminate all processes
args ~
None
returns ~
0 when complete
----------------------------------------------------------------------------------------------- */
int killAllProcesses()
{
	while (head != NULL) {
    	kill(head->pid, SIGTERM);
		free(head);
		head = head->next; 
    }
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
0 if successful, -1 if not
----------------------------------------------------------------------------------------------- */
int printStatus ()
{
}

/* --------------------------------------------------------------------------------------------
Function handleBuiltInFunction: Handle inline commands that are possibly built in functions
args ~
- builtInFunctions:		Array Containing Built in Functions			(char **)
- curr_command:			Parsed Inline Command						(struct command_line*)
returns ~
0 if successful, 1 if not
----------------------------------------------------------------------------------------------- */
int handleBuiltInFunction(char** builtInFunctions, struct command_line* curr_command)
{
	int i;
    for (i = 0; i < 3; i++) {
        if (!strcmp(builtInFunctions[i], curr_command->argv[0])) {
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
        }
    }
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
	char *built_in_functions[] = {"exit", "cd", "status"};
	struct command_line *curr_command;

	while(true)
	{
		curr_command = parse_input();
	}
	return EXIT_SUCCESS;
}
