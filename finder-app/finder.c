#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <dirent.h>

#define MAX_FILENAME_LENGTH 1024

// Function to check if a file contains the given string
int file_contains_string(const char *filename, const char *search_str) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        syslog(LOG_ERR, "Error opening file %s", filename);
        return 0; // Error opening file
    }

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, search_str)) {
            fclose(file);
            return 1; // String found
        }
    }

    fclose(file);
    return 0; // String not found
}

int main(int argc, char *argv[]) {
    // Check if correct arguments are provided
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <directory> <string>\n", argv[0]);
        return 1;
    }

    const char *directory = argv[1];
    const char *search_str = argv[2];

    // Open the directory
    DIR *dir = opendir(directory);
    if (dir == NULL) {
        syslog(LOG_ERR, "Error opening directory %s", directory);
        return 1; // Error opening directory
    }

    struct dirent *entry;
    int match_count = 0;

    // Traverse all files in the directory
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. directories
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char filepath[MAX_FILENAME_LENGTH];
        snprintf(filepath, sizeof(filepath), "%s/%s", directory, entry->d_name);

        if (file_contains_string(filepath, search_str)) {
            match_count++;
        }
    }

    // Close the directory
    closedir(dir);

    // Log the result using syslog
    syslog(LOG_DEBUG, "Found %d files containing string '%s' in directory '%s'", match_count, search_str, directory);

    // Print result to standard output
    printf("The number of files are %d and the number of matching lines are %d\n", match_count, match_count);

    return 0;
}
