/**
* Assignment 2: File Operations and Cross Compiler
* ECEN 5713 | Spring 2026
* Author: Venetia Furtado
* References:
* 1. Linux System Programming by Robert Love Chapter 2
* 2. Linux man pages - https://man7.org/linux/man-pages/index.html
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>   //File control options for eg: O_WRONLY, O_CREAT,O_TRUNC
#include <unistd.h>  //write(), close()
#include <syslog.h>
#include <errno.h>

int main(int argc, char *argv[])
{
   //Open syslog
   openlog("writer", LOG_PID, LOG_USER);

   //Argument check
   if (argc != 3) 
   {
      syslog(LOG_ERR, "Invalid number of arguments");
      closelog();
      exit(1);
   }

   const char *writefile = argv[1];
   const char *writestr  = argv[2];

   //Log debug message
   syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

   //Open file; does not create directories
   int fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   if (fd == -1) 
   {
      syslog(LOG_ERR, "Failed to open file %s: %s",writefile, strerror(errno));
      closelog();
      exit(1);
   }

   // Write to file
   ssize_t bytes_written = write(fd, writestr, strlen(writestr));
   if (bytes_written == -1 || bytes_written != (ssize_t)strlen(writestr)) 
   {
      syslog(LOG_ERR, "Failed to write to file %s: %s",writefile, strerror(errno));
      close(fd);
      closelog();
      exit(1);
   }

   // Close file
   if (close(fd) == -1) 
   {
      syslog(LOG_ERR, "Failed to close file %s: %s",writefile, strerror(errno));
      closelog();
      exit(1);
   }

   // Clean up syslog
   closelog();
   return 0;
}
