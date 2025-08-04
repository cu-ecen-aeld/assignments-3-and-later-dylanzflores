#include "systemcalls.h"
#include <stdlib.h>     // for system(), exit()
#include <stdbool.h>    // for bool, true, false
#include <stdarg.h>     // for va_list, va_start, va_end
#include <sys/types.h>  // for pid_t
#include <sys/wait.h>   // for waitpid(), WIFEXITED, etc.
#include <unistd.h>     // for fork(), execv(), dup2()
#include <fcntl.h>      // for open() and O_* flags
#include <stdio.h>      // for perror()

bool do_system(const char *cmd)
{
	if(cmd == NULL){
		return false;
	}
	int ret = system(cmd);
	if(ret == -1){
		return false;
	}
	else if(WIFEXITED(ret) && WEXITSTATUS(ret) == 0){
		return true;
	}
	
	return false;
}

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    // Absolute path check
    if(command[0][0] != '/') {
        fprintf(stderr, "Error: command path must be absolute: %s\n", command[0]);
        va_end(args);
        return false;
    }

    va_end(args);

	pid_t pid = fork();
	if(pid < 0) {
		perror("Fork process failed..");
		return false;
	}
	else if(pid == 0){
		execv(command[0], command);
		perror("execv failed..");
		exit(EXIT_FAILURE);
	}
	else{
		int status = 0;
		if(waitpid(pid, &status, 0) == -1){
			perror("waitpid failed");
			return false;
		}
		
		if(WIFEXITED(status) && WEXITSTATUS(status) == 0){
			return true;
		}
		else{
			return false;
		}
	}
}

bool do_exec_redirect(const char *outputfile, int count, ...)
{
	if(count < 1 || outputfile == NULL){
		return false;
	}
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    // Absolute path check
    if(command[0][0] != '/') {
        fprintf(stderr, "Error: command path must be absolute: %s\n", command[0]);
        va_end(args);
        return false;
    }

    va_end(args);

	pid_t pid = fork();
	if(pid < 0){
		perror("Fork process failed..");
		return false;
	}
	else if(pid == 0){
		int fd = open(outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0) {
          		perror("open failed");
           		exit(EXIT_FAILURE);
       		}

        	// Redirect stdout to the file
        	if (dup2(fd, STDOUT_FILENO) < 0) {
           		 perror("dup2 failed");
          	  	 close(fd);
            		exit(EXIT_FAILURE);
       		}
       		close(fd);
       		execv(command[0], command);
       		perror("execv failed");
       		exit(EXIT_FAILURE);
       	}
	else{
		int status;
		if(waitpid(pid, &status, 0) == -1){
			perror("Wait failed..");
			return false;
		}
		return WIFEXITED(status) && WEXITSTATUS(status) == 0;
	}
}

