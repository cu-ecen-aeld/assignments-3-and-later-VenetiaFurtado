#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "systemcalls.h"

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
 */
bool do_system(const char *cmd)
{
    /*
     * TODO  add your code here
     *  Call the system() function with the command set in the cmd
     *   and return a boolean true if the system() call completed with success
     *   or false() if it returned a failure
     */
    if (cmd == NULL)
    {
        return false;
    }

    /*This code has been referenced from Linux System Programming (Chapter 5 - Waiting for Terminated Child processes)*/
    int status = system(cmd);

    // check if invocation of system() call fails
    if (status == -1)
    {
        perror("system");
        return false;
    }

    // check if system() call completed successfully
    // WIFEXITED(status): returns true if the child terminated normally
    // WEXITSTATUS(status): returns the exit status of the child. This macro is
    //  employed only if WIFEXITED returned true.
    if (WIFEXITED(status))
    {
        int exit_status = WEXITSTATUS(status);
        printf("Process exited normally with exit code %d\n", exit_status);

        if (exit_status == 0)
        {
            return true;
        }
    }

    printf("Command did not exit normally\n");
    return false;
}

/**
 * @param count -The numbers of variables passed to the function. The variables are command to execute.
 *   followed by arguments to pass to the command
 *   Since exec() does not perform path expansion, the command to execute needs
 *   to be an absolute path.
 * @param ... - A list of 1 or more arguments after the @param count argument.
 *   The first is always the full path to the command to execute with execv()
 *   The remaining arguments are a list of arguments to pass to the command in execv()
 * @return true if the command @param ... with arguments @param arguments were executed successfully
 *   using the execv() call, false if an error occurred, either in invocation of the
 *   fork, waitpid, or execv() command, or if a non-zero return value was returned
 *   by the command issued in @param arguments with the specified arguments.
 */

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char *command[count + 1];
    int i;
    for (i = 0; i < count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

    /*
     * TODO:
     *   Execute a system command by calling fork, execv(),
     *   and wait instead of system (see LSP page 161).
     *   Use the command[0] as the full path to the command to execute
     *   (first argument to execv), and use the remaining arguments
     *   as second argument to the execv() command.
     *
     */
    // The code below has been referenced from LSP Chapter 5:
    // Code snippets under "fork() system call" and  "Launching and Waiting for a new process"
    pid_t child_pid = fork();

    if (child_pid == -1)
    {
        // fork failed
        va_end(args);
        return false;
    }

    // child process
    if (child_pid == 0)
    {
        execv(command[0], command);

        // If execv returns, it failed
        exit(EXIT_FAILURE);
    }

    // Parent process
    int status;

    if (waitpid(child_pid, &status, 0) == -1)
    {
        // waitpid failed
        va_end(args);
        return false;
    }

    // Check if child exited normally and with status 0
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        va_end(args);
        return true;
    }

    va_end(args);
    return false;
}

/**
 * @param outputfile - The full path to the file to write with command output.
 *   This file will be closed at completion of the function call.
 * All other parameters, see do_exec above
 */
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char *command[count + 1];
    int i;
    for (i = 0; i < count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

    /*
     * TODO
     *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
     *   redirect standard out to a file specified by outputfile.
     *   The rest of the behaviour is same as do_exec()
     *
     */
    // The code below has been referenced from LSP Chapter 5:
    // Code snippets under "fork() system call" and  "Launching and Waiting for a new process"
    pid_t child_pid = fork();

    if (child_pid == -1)
    {
        // fork failed
        va_end(args);
        return false;
    }

    // child process
    if (child_pid == 0)
    {
        // Open the output file
        //Source: https://stackoverflow.com/questions/13784269/redirection-inside-call-to-execvp-not-working/13784315#13784315
        int fd = open(outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) 
        {
            exit(EXIT_FAILURE);
        }

        // Redirect stdout to the file
        //Reference: https://chatgpt.com/share/697c2e23-4dd4-8001-a514-8c3a61cdb0e4
        if (dup2(fd, STDOUT_FILENO) < 0) 
        {
            close(fd);
            exit(EXIT_FAILURE);
        }

        // Close duplicated fd
        close(fd);

        execv(command[0], command);

        // If execv returns, it failed
        exit(EXIT_FAILURE);
    }

    // Parent process
    int status;

    if (waitpid(child_pid, &status, 0) == -1)
    {
        // waitpid failed
        va_end(args);
        return false;
    }

    // Check if child exited normally and with status 0
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        va_end(args);
        return true;
    }

    va_end(args);

    return false;
}
