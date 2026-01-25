/**
* Assignment 2: File Operations and Cross Compiler
* ECEN 5713 | Spring 2026
* Author: Venetia Furtado
* References:
* 1. Linux System Programming by Robert Love Chapter 2
* 2. Linux man pages - https://man7.org/linux/man-pages/index.html
* 3. https://stackoverflow.com/questions/44394034/how-to-view-syslog-in-ubuntu 
* 4. https://linux.die.net/man/8/syslogd
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>   //File control options for eg: O_WRONLY, O_CREAT,O_TRUNC
#include <unistd.h>  //write(), close()
#include <syslog.h>
#include <errno.h>

#define FILE_PERMISSION 0644  // 0644 - owner can read and write, everyone else can only read


/**
 * @brief Writes a string to a file and logs operations using syslog.
 */
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

   //Open file; does not create directories
   int fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMISSION);
   if (fd == -1) 
   {
      syslog(LOG_ERR, "Failed to open file %s: %s", writefile, strerror(errno));
      closelog();
      exit(1);
   }

   //Log debug message
   syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

   // Write to file
   ssize_t writestr_len = strlen(writestr);
   ssize_t bytes_written = write(fd, writestr, writestr_len);
   if (bytes_written == -1 || bytes_written != writestr_len) 
   {
      syslog(LOG_ERR, "Failed to write to file %s: %s", writefile, strerror(errno));
      close(fd);
      closelog();
      exit(1);
   }

   // Close file
   if (close(fd) == -1) 
   {
      syslog(LOG_ERR, "Failed to close file %s: %s", writefile, strerror(errno));
      closelog();
      exit(1);
   }

   // Clean up syslog
   closelog();
   return 0;
}
