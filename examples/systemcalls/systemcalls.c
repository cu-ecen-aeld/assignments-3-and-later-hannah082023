#include "systemcalls.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

bool do_system(const char *cmd)
{
    if (cmd == NULL) { return false; } 
    
    int result = system(cmd);
    
    if (result == -1) { return false; }
    
    return true;
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
    command[count] = command[count];

    fflush(stdout);

    pid_t pid = fork();

    if (pid == -1) {
        va_end(args);
        return false;
    } 
    else if (pid == 0) {
        execv(command[0], command);
        perror("execv fail");
        exit(EXIT_FAILURE); 
    } 
    else {
        int status;

        if (waitpid(pid, &status, 0) == -1) {
            va_end(args);
            return false;
        }

        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0) {
                va_end(args);
                return true;
            }
        }

        va_end(args);
        return false;
    }
}

bool do_exec_redirect(const char *outputfile, int count, ...)
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
    command[count] = command[count];

    int fd = open(outputfile, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (fd < 0) {
        perror("Fail to open file");
        va_end(args);
        return false;
    }

    fflush(stdout);
    pid_t pid = fork();

    if (pid == -1) {
        close(fd);
        va_end(args);
        return false;
    } 
    else if (pid == 0) {
        if (dup2(fd, 1) < 0) {
            perror("dup2 fail");
            exit(EXIT_FAILURE);
        }

        close(fd);

        execv(command[0], command);
        
        perror("execv fail");
        exit(EXIT_FAILURE);
    } 
    else {
        close(fd);

        int status;
        if (waitpid(pid, &status, 0) == -1) {
            va_end(args);
            return false;
        }

        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0) {
                va_end(args);
                return true;
            }
        }

        va_end(args);
        return false;
    }
}
