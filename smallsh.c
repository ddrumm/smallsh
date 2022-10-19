#define _GNU_SOURCE
#include <stdio.h> // for standard input/output
#include <string.h> // to handle string operations
#include <unistd.h> // access to POSIX OS API
#include <sys/types.h> // data types (pid_t, size_t, ssize_t)
#include <stdlib.h> // malloc/calloc
#include <signal.h> // for signal catching
#include <fcntl.h> // for file operations
#include <sys/wait.h> // for waitpid
#include <signal.h> // for WISIGNALED

// A simple Unix shell implementation that includes some basic shell features.
// These features are: built-in cd (change directory) and status commands,
// support for running processes in foreground or background, foreground
// only mode enabled by SIGSTOP signal, input and output redirection, and
// variable expansion for one specific variable.

#define MAXLENGTH 2048
#define MAXARGS 512

/* declare a global array to store
user input typed into the command line */
static char input[MAXLENGTH];

// global variable for the last status run in foreground
int forkStatus = 0;

// global variable for SIGTSTP and running foreground only
int foregroundFlag = 1;

// global variable for a child process running in foreground
// 1 is no child running, 0 is child running
int childRunningForeground = 1;

// global variable for if SIGINT used to interrupt a child
char* sigintUsed;

// create a list to hold child Pids
// then resize it when children are added later
// use numChildren to track how many background prcoesses were created
int* childProcesses;
int numChildren = 1;

// create a list to hold childstatus
// then resize it when children are added later
// use numStatus to track how many background prcoesses were created
int* childStatuses;
int numStatus = 1;

/* struct for different elements of a command */
struct commandInput {
		char *command;
    char *args[MAXARGS];
    int inputRedirect; // 1 is true, 0 is false for input redirect
    char *inputFile;
    int outputRedirect; // 1 is true, 0 is false for output redirect
		char *outputFile;
		int background; // 1 is true, 0 is false for background command
};

char *token; // to use to process inputs with strtok

// function template for expansion of $$
char * variableExpansion(char* strToProcess);

// create a signal handler for SIGINT
//Developed with lecture Module 4, Signal Handling API
void replace_SIGINT(int signo) {
	char* output = "Terminated by signal 2\n";
	asprintf(&sigintUsed, "terminated by signal 2");
	write(STDOUT_FILENO, output, 23);
	fflush(stdout);
}

void handle_SIGTSTP(int signo) {
		if (foregroundFlag == 1) {
			char* output = "\nEntering forground-only mode (& is now ignored)\n";
			write(STDOUT_FILENO, output, 49);
			fflush(stdout);
			write(STDOUT_FILENO, ": ", 2);
			fflush(stdout);
			foregroundFlag = 0;
		} else {
			char* output = "Exiting foreground-only mode\n";
			write(STDOUT_FILENO, output, 29);
			fflush(stdout);
			write(STDOUT_FILENO, ": ", 2);
			fflush(stdout);
			foregroundFlag = 1;
		}
}

// function to expand all instances of $$ with the PID
char * variableExpansion(char* strToProcess) {
	char* tmpStr = NULL; // temp string

	// find all instances of $$, replace $$ with PID
	for (int i = 0; i < strlen(strToProcess); ++i) {
		if (strToProcess[i] == '$' && strToProcess[i+1] == '$') {
			// if first run through then expand $$
			if (tmpStr == NULL) {
				asprintf(&tmpStr, "%i", getpid());
				++i;
			// if tmpStr already has content then add PID for $$
			} else {
				asprintf(&tmpStr, "%s%i", tmpStr, getpid());
				++i;
			}
			// No $$ and first iteration so copy char
		} else if (tmpStr == NULL) {
				asprintf(&tmpStr, "%c", strToProcess[i]);
			// No $$, and not first iteration so copy char by char
		} else {
			asprintf(&tmpStr, "%s%c", tmpStr, strToProcess[i]);
		}
	}
	return tmpStr;
}

int main() {

	// Ignore SIGINT in parent
	// Developed with lecture Module 4, Signal Handling API
	struct sigaction SIGINT_action = {0}; // init SIGINT_action to be empty
	SIGINT_action.sa_handler = replace_SIGINT; // used to ignore SIGINT
	sigfillset(&SIGINT_action.sa_mask); // handle catchable signals
	SIGINT_action.sa_flags = SA_RESTART; // restart interrupted sys call
	sigaction(SIGINT, &SIGINT_action, NULL); 	// install signal sa_handler

	// use SIGTSTP for foreground only
	// Developed with lecture Module 4, Signal Handling API
	struct sigaction SIGTSTP_action = {0}; // init SIGTSTP to be empty
	SIGTSTP_action.sa_handler = handle_SIGTSTP; // used to redirect SIGTSTP
	sigfillset(&SIGTSTP_action.sa_mask); // handle catchable signals
	SIGTSTP_action.sa_flags = SA_RESTART; // restart interrupted sys call
	sigaction(SIGTSTP, &SIGTSTP_action, NULL); // install signal handler

		// reallocate size of array based on numChildren count
		childProcesses = (int *)malloc(sizeof(long int) * numChildren);

		// reallocate size of array based on numStatus count
		childStatuses = (int *)malloc(sizeof(long int) * numStatus);

	/* Never Ending Loop for prompt */
	while(1) {

		// check for background processes
		for(int i = 0; i < numChildren; i++) {
			pid_t checkPid;
			checkPid = waitpid(childProcesses[i], &childStatuses[i], WNOHANG);
			if (checkPid > 0) {
				if (WIFEXITED(childStatuses[i])) {
					printf("background pid %d is done: exit value %d\n", childProcesses[i], WEXITSTATUS(childStatuses[i]));
					fflush(stdout);
				} else if (WIFSIGNALED(childStatuses[i])) {
					printf("background pid %d is done: terminated by signal %d\n", childProcesses[i], WTERMSIG(childStatuses[i]));
					continue;
				}
			} else {
			continue;
			}
		}

		struct commandInput currInputs;

		// reset struct for next iteration
		currInputs.command = NULL;
		for (int i = 0; i < MAXARGS; ++i) {
			currInputs.args[i] = NULL;
		}
		currInputs.inputRedirect = 0;
		currInputs.inputFile = NULL;
		currInputs.outputRedirect = 0;
		currInputs.outputFile = NULL;
		currInputs.background = 0;

		int charCount = 0; // track characters input
		int argCount = 0; // track arguments input

    // reset child running in foreground
		childRunningForeground = 1;

		// check for background processes
		for(int i = 0; i < numChildren; i++) {
			pid_t checkPid;
			checkPid = waitpid(childProcesses[i], &childStatuses[i], WNOHANG);
			if (checkPid > 0) {
				if (WIFEXITED(childStatuses[i])) {
					printf("background pid %d is done: exit value %d\n", childProcesses[i], WEXITSTATUS(childStatuses[i]));
					fflush(stdout);
				} else if (WIFSIGNALED(childStatuses[i])) {
					printf("background pid %d is done: terminated by signal %d\n", childProcesses[i], WTERMSIG(childStatuses[i]));
					continue;
				}
			} else {
			continue;
			}
		}

		/* create the prompt with the stream being written to stdout*/
		fputs(": ", stdout);

		/* Read user input with a maximum size of 2048
			 using the global array to store input and the
			 stream being written to stdin*/
		fgets(input, MAXLENGTH, stdin);

		/* handle blank lines and comments
			 if the input is either a blank line or a # (comment)
			 then continue to the beginning of while loop */
		if (input[0] == '\n' || input[0] == '#') {
			continue;
		}

		// if there is a newline and it isn't a blank line
		// then convert it to a null terminator to ease
		// processing commands
		for (int i = 0; i < MAXLENGTH; ++i) {
			if (input[i] == '\n') {
				input[i] = '\0';
			}
		}

		// get first token and expand any $$
		token = strtok(input, " ");
		token = variableExpansion(token);

		currInputs.command = token; // first token is the command
		currInputs.args[argCount] = token; // ensure command is a part of args
		++argCount; // count first argument
		token = strtok(NULL, " ");
		while (token != NULL) {
			token = variableExpansion(token); // test for $$ for variable expansion
			if (*token == '<') {
				currInputs.inputRedirect = 1; // set input redirect flag
				token = strtok(NULL, " "); // if there is a input redirect
				token = variableExpansion(token); // expand $$
				currInputs.inputFile = token; // get the file
				token = strtok(NULL, " "); // move to next token
			} else if (*token == '>') {
				currInputs.outputRedirect = 1; // set output redirect flag
				token = strtok(NULL, " "); // if there is an output redirect
				token = variableExpansion(token); // expand $$
				currInputs.outputFile = token; // get the file
				token = strtok(NULL, " "); // move to next token
			} else if (*token == '&' && (strtok(NULL, " ") == NULL)) {
				currInputs.background = 1; // set background flag
				token = strtok(NULL, " ");
			} else {
				currInputs.args[argCount] = token; // get arguments and add them
				token = strtok(NULL, " ");
				++argCount;
			}
		}

		if (!strcmp(currInputs.command, "exit")) { // handle exit command
			exit(0);
		} else if (!strcmp(currInputs.command, "status")) { // status command
			if (sigintUsed == NULL) {
				printf("exit value %i\n", forkStatus);
				fflush(stdout);
			} else {
					printf("%s\n", sigintUsed);
					fflush(stdout);
			}
		} else if (!strcmp(currInputs.command, "cd")) { // cd command
				if (currInputs.args[1]) { // directory would be second argument
					if (chdir(currInputs.args[1]) == -1) {
						printf("Directory not found.\n"); // if chdir fails
						fflush(stdout);
					} // chdir succeeded
				} else {
						chdir(getenv("HOME")); // change to ~ if not directory provided
					}
		} else {
			// Code Developed using Module 4, Process API - Executing a New Program
			// use execvp with fork()
			int childStatus;

			// fork a new process
			pid_t createPid = fork();

			switch(createPid) {
				case -1:
					perror("fork()\n");
					forkStatus = 1;
					// reset sigintUsed
					sigintUsed = NULL;
					exit(1);
					break;
				case 0:
					// Code Developed using Module 4, Processes and I/O
					// if inputRedirect flag is set then do the input redirect
					if (currInputs.inputRedirect) {
						// check if file can open
						if (open(currInputs.inputFile, O_RDONLY) == -1) {
							perror(currInputs.inputFile);
							forkStatus = 1;
							// reset sigintUsed
							sigintUsed = NULL;
							exit(1); // exit with error
						}

						// redirect stdin
						int result = dup2(open(currInputs.inputFile, O_RDONLY), 0);
						if (result == -1) {
							perror("Input file invalid");
							forkStatus = 2;
							// reset sigintUsed
							sigintUsed = NULL;
							exit(2);
						}
					}

					// Code Developed using Module 4, Processes and I/O
					// if outputRedirect flag is set then do the output redirect
					if (currInputs.outputRedirect) {
						if (open(currInputs.outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644) == -1) {
							perror(currInputs.outputFile);
							forkStatus = 1;
							// reset sigintUsed
							sigintUsed = NULL;
							exit(1);
						}

						// redirect stdout
						int result = dup2(open(currInputs.outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644), 1);
						if (result == -1) {
							perror("Output file invalid");
							forkStatus = 2;
							// reset sigintUsed
							sigintUsed = NULL;
							exit(2);
							}
					}

					execvp(currInputs.command, currInputs.args);
					perror(currInputs.command);
					forkStatus = 2;
					// reset sigintUsed
					sigintUsed = NULL;
					exit(2);
					break;
				default:
				// in parent process, wait for child to terminate
				if (currInputs.background && foregroundFlag) {
					// child running, it will ignore SIGTSTP
					// childRunningForeground = 0;
					pid_t childPid = waitpid(createPid, &childStatus, WNOHANG);

					if (numChildren == 1) {
						childProcesses[numChildren-1] = createPid;
						childStatuses[numChildren-1] = childStatus;
						++numStatus;
						++numChildren;
					// else expand array and then add the child pid to it
					} else {
						childProcesses = realloc(childProcesses, sizeof(long int) * numChildren);
						childProcesses[numChildren-1] = createPid;
						childStatuses = realloc(childStatuses, sizeof(long int) * numStatus);
						childStatuses[numChildren-1] = childStatus;
						++numStatus;
						++numChildren;
					}
					printf("background pid is %d\n", createPid);
					fflush(stdout);
					continue;
				} else {
					pid_t childPid = waitpid(createPid, &childStatus, 0);

					// set the status based on how waitpid exited
					if (WIFEXITED(childStatus)) {
						forkStatus = WEXITSTATUS(childStatus);
						sigintUsed = NULL;
					}
				continue;
				}
			}
		}
	}
	return 0;
}
