#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments: expected 2, got %d", argc - 1);
        printf("Usage: %s <file> <string>\n", argv[0]);
        return 1;
    }

    const char *filepath = argv[1];
    const char *writestr = argv[2];

    openlog("writer", 0, LOG_USER);

    FILE *file = fopen(filepath, "w");
    if (file == NULL) {
        syslog(LOG_ERR, "Error opening file %s: %s", filepath, strerror(errno));
        closelog();
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", writestr, filepath);
    fprintf(file, "%s", writestr);
    fclose(file);
    closelog();
    
    return 0;
}
