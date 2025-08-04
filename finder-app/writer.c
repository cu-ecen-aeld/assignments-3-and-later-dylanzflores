#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

int main(int argc, char *argv[]){
	
		openlog("writer", LOG_PID | LOG_CONS, LOG_USER);
		if(argc < 3){
			syslog(LOG_ERR, "Too few arguments for program...%d number of arguments detected\n", argc - 1);
			printf("Error: Too few arguments...\n");
			printf("Input ./writer <file> <string_input>\n");
			closelog();
			return 1;
		}
		
		else if(argc > 3){
			syslog(LOG_ERR, "Too many arguments for program...%d number of arguments detected\n", argc - 1);
			printf("Error: Too many arguments...\n");
			printf("Input ./writer <file> <string_input>\n");
			closelog();
			return 1;
		}
		char* fileName = argv[1]; // file
		char* text = argv[2]; // IO to write data
		
		FILE *file = fopen(fileName, "w"); // open file
		if (file == NULL){
			syslog(LOG_ERR, "Error Opening File %s\n", fileName);
			closelog();
			return 1;
		}
		
		syslog(LOG_DEBUG, "Writing %s to the file %s \n", text, fileName);
		
		if(fputs(text, file) == EOF){
			syslog(LOG_ERR, "Error Writing to File %s\n", fileName);
			fclose(file);
			closelog();
			return 1;
		}
		
		fclose(file);
		syslog(LOG_DEBUG, "Writing to %s was a success\n", fileName);
		closelog();
		return 0;
}
