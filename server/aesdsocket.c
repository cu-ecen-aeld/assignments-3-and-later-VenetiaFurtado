/**
 * Assignment 6: Native Socket Server Threading Support
 * ECEN 5713 | Spring 2026
 * Venetia Furtado
 */
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include "../aesd-char-driver/aesd_ioctl.h"

#define PORT 9000  // the port users will be connecting to
#define BACKLOG 10 // how many pending connections queue will hold
#define BUFFER_SIZE 1024
#define USE_AESD_CHAR_DEVICE 1
#define FOLDER_PATH "/var/tmp"
#define AESDCHAR_IOCSEEKTO_CMD "AESDCHAR_IOCSEEKTO:"
#if USE_AESD_CHAR_DEVICE
#define FILE_PATH "/dev/aesdchar"
#else
#define FILE_PATH "/var/tmp/aesdsocketdata"
#endif

pthread_mutex_t file_mutex;
static volatile bool exit_requested = false;
timer_t timerid; // create timer

typedef struct RecvDataLinkedList
{
   char *buffer;
   unsigned int len;
   struct RecvDataLinkedList *next;
} RecvDataLinkedList;

/**
 * Handles termination signals
 */
void signalHandler(int signo)
{
   if (signo == SIGINT || signo == SIGTERM)
   {
      exit_requested = true;
   }
}

/**
 * POSIX timer callback function that appends a timestamp to the file
 */
void timer_handler(union sigval sigval)
{
   time_t t = time(NULL);
   struct tm *tm_info = localtime(&t);

   char timestamp[64];
   strftime(timestamp, sizeof(timestamp), "%a, %d %b %Y %T %z", tm_info);

   char line[128];
   sprintf(line, "timestamp:%s\n", timestamp);

   pthread_mutex_lock(&file_mutex);

   // if any data is received, open the file
   int writefile_fd = -1;

   writefile_fd = open(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
   if (writefile_fd < 0)
   {
      syslog(LOG_ERR, "writefile open failed");
      pthread_mutex_unlock(&file_mutex); // unlock before returning
      return;
   }

   ssize_t bytes_written = write(writefile_fd, line, strlen(line));
   if (bytes_written < 0)
   {
      syslog(LOG_ERR, "ts not written");
      pthread_mutex_unlock(&file_mutex); // unlock before returning
      return;
   }

   syslog(LOG_DEBUG, "timestamp written to file");

   if (writefile_fd >= 0)
   {
      close(writefile_fd);
   }

   pthread_mutex_unlock(&file_mutex);
}

/**
 * Initialize and start POSIX timer
 */
void timerInit()
{
   // Configure thread notification
   struct sigevent sev = {
       .sigev_notify = SIGEV_THREAD,
       .sigev_notify_function = timer_handler,
   };

   if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) == -1)
   {
      syslog(LOG_ERR, "timer_create failed");
      return;
   }

   // Arm timer-first fire after 10s, repeat every 10s
   struct itimerspec its = {
       .it_value = {.tv_sec = 10, .tv_nsec = 0},
       .it_interval = {.tv_sec = 10, .tv_nsec = 0},
   };
   if (timer_settime(timerid, 0, &its, NULL) == -1)
   {
      syslog(LOG_ERR, "timer_settime failed");
      return;
   }

   syslog(LOG_DEBUG, "Timer started");
}

/**
 * Creates a daemon process
 * References:
 * https://www.csl.mtu.edu/cs4411.ck/www/NOTES/process/fork/create.html
 * https://chatgpt.com/share/6990b8d4-fd50-8001-b943-f17029e505a1
 * https://man7.org/tlpi/code/online/dist/daemons/become_daemon.c.html
 */
int createDaemon()
{
   pid_t pid = fork();
   if (pid < 0)
   {
      syslog(LOG_ERR, "fork failed");
      return -1;
   }
   // parent should exit after successful fork, now only child continues
   if (pid > 0)
   {
      exit(EXIT_SUCCESS);
   }

   // creates a new session
   if (setsid() == -1)
   {
      syslog(LOG_ERR, "setsid failed");
      return -1;
   }

   // Clear file mode creation mask
   umask(0);

   // change directory to FOLDER_PATH, prevents FILE_PATH getting unmounted while daemon is running.
   chdir(FOLDER_PATH);

   // close these because daemons should not log to the terminal
   close(STDIN_FILENO);
   close(STDOUT_FILENO);
   close(STDERR_FILENO);

   return 0;
}

/**
 * Creates and configures a TCP server socket.
 * References:
 * https://www.geeksforgeeks.org/c/socket-programming-cc/
 * https://beej.us/guide/bgnet/html/#a-simple-stream-server
 * Linux man-pages
 */
int createSocket(bool daemon_mode)
{
   // creating socket file descriptor
   int sockfd = socket(AF_INET, SOCK_STREAM, 0);
   if (sockfd < 0)
   {
      syslog(LOG_ERR, "Socket failed");
      return -1;
   }

   int opt = 1;
   if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
   {
      syslog(LOG_ERR, "setsockopt failed");
      return -1;
   }

   // configuring server address and PORT
   struct sockaddr_in server_addr;
   memset(&server_addr, 0, sizeof(server_addr));
   server_addr.sin_family = AF_INET;
   server_addr.sin_addr.s_addr = INADDR_ANY;
   server_addr.sin_port = htons(PORT); // byte order conversion from host byte order(little endian) to network byte order(big endian)

   // bind sockfd to PORT
   if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
   {
      syslog(LOG_ERR, "socket bind failed");
      return -1;
   }

   if (daemon_mode == true)
   {
      if (createDaemon() < 0)
      {
         return -1;
      }
   }

   // listen to upto BACKLOG connections
   if (listen(sockfd, BACKLOG) < 0)
   {
      syslog(LOG_ERR, "listen failed");
      return -1;
   }

   return sockfd;
}

/**
 * Struct containing per-client thread info
 */
typedef struct ThreadArg
{
   int client_fd;
   struct sockaddr_in client_addr;
   pthread_t thread;
   bool isDone;
   struct ThreadArg *next;
} ThreadArg;

ThreadArg dummyNode;

// usage: findCommand(head, "AESDCHAR_IOCSEEKTO:", &seekto)
bool findCommand(const RecvDataLinkedList *head, const char *command, struct aesd_seekto *seekto)
{
   int command_len = strlen(command);
   int i = 0;
   bool command_found = false;
   bool write_cmd_found = false;
   uint32_t write_cmd = 0;
   uint32_t write_cmd_offset = 0;

   if (!head || !command)
   {
      return false;
   }

   while (head != NULL)
   {
      for (int j = 0; j < head->len; j++)
      {
         if (head->buffer[j] == '\n')
         {
            break;
         }

         if (command_found == false)
         {
            if (command[i] != head->buffer[j])
            {
               return false;
            }

            i++;
            if (i == command_len)
            {
               command_found = true;
            }
         }
         else
         {
            if (head->buffer[j] == ',')
            {
               write_cmd_found = true;
            }
            else if (write_cmd_found == false)
            {
               if (head->buffer[j] < '0' || head->buffer[j] > '9')
               {
                  return false;
               }
               write_cmd = write_cmd * 10 + (head->buffer[j] - '0');
            }
            else
            {
               if (head->buffer[j] < '0' || head->buffer[j] > '9')
               {
                  return false;
               }
               write_cmd_offset = write_cmd_offset * 10 + (head->buffer[j] - '0');
            }
         }
      }
      head = head->next;
   }

   if (write_cmd_found == false)
   {
      return false;
   }

   seekto->write_cmd = write_cmd;
   seekto->write_cmd_offset = write_cmd_offset;

   return true;
}

/**
 * Thread routine to process a client socket connection. The function is executed
 * in a separate thread for each accepted client connection.
 */
void *processClientConnection(void *arg)
{
   ThreadArg *threadArg = (ThreadArg *)arg;

   int client_fd = threadArg->client_fd;
   struct sockaddr_in client_addr = threadArg->client_addr;

   int status = 0;
   RecvDataLinkedList dummyhead;
   dummyhead.buffer = NULL;
   dummyhead.len = 0;
   dummyhead.next = NULL;

   RecvDataLinkedList *node = &dummyhead;
   // log IP addr of the connected client
   char client_ip[INET_ADDRSTRLEN];
   // https://man7.org/linux/man-pages/man3/inet_ntop.3.html
   inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
   while (1)
   {
      char buffer[BUFFER_SIZE];

      syslog(LOG_DEBUG, "Trying to recv from %s", client_ip);

      // Receive data from socket of maximum size = BUFFER_SIZE
      ssize_t bytes = recv(client_fd, buffer, BUFFER_SIZE, 0);
      if (bytes <= 0)
      {
         syslog(LOG_ERR, "recv failed");
         break;
      }

      syslog(LOG_DEBUG, "recvd %ld bytes from %s", bytes, client_ip);

      // allocate new memory for received data
      char *temp = (char *)malloc(bytes);
      if (!temp)
      {
         syslog(LOG_ERR, "malloc failed");
         break;
      }

      // copy data into the new memory
      memcpy(temp, buffer, bytes);

      // create new ll node and store the data pointer
      RecvDataLinkedList *newnode = (RecvDataLinkedList *)malloc(sizeof(RecvDataLinkedList));
      newnode->buffer = temp;
      newnode->len = bytes;
      newnode->next = NULL;

      // add the new node to the ll and move the ll ahead
      node->next = newnode;
      node = node->next;

      syslog(LOG_DEBUG, "Added new node to ll");

      bool newLineFound = false;
      for (int i = 0; i < bytes; i++)
      {
         if (buffer[i] == '\n')
         {
            newLineFound = true;
            break;
         }
      }

      if (newLineFound == true)
      {
         break;
      }
   }

   node = dummyhead.next;

   pthread_mutex_lock(&file_mutex);

   // if any data is received, open the file
   int file_fd = -1;
   if (node != NULL)
   {
#if USE_AESD_CHAR_DEVICE
      file_fd = open(FILE_PATH, O_RDWR | O_APPEND);
#else
      file_fd = open(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
#endif
      if (file_fd < 0)
      {
         syslog(LOG_ERR, "writefile open failed");
         status = -1;
      }
   }

   struct aesd_seekto seekto;
   bool command_found = findCommand(node, AESDCHAR_IOCSEEKTO_CMD, &seekto);
   if (command_found == true)
   {
      if (ioctl(file_fd, AESDCHAR_IOCSEEKTO, &seekto) != 0)
      {
         syslog(LOG_ERR, "ioctl failed!");
      }
   }
   else
   {
      // copy received data into the file
      while (file_fd >= 0 && node != NULL)
      {
         syslog(LOG_DEBUG, "writing %d bytes to file", node->len);
         size_t bytes_to_write = node->len;
         size_t bytes_written = write(file_fd, node->buffer, bytes_to_write);
         if (bytes_written < bytes_to_write)
         {
            syslog(LOG_ERR, "bytes_written %ld < bytes_to_write %ld", bytes_written, bytes_to_write);
         }
         free(node->buffer);
         node->buffer = NULL;
         RecvDataLinkedList *tempnode = node;
         node = node->next;
         free(tempnode);
      }
   }

   pthread_mutex_unlock(&file_mutex);

   if (file_fd >= 0)
   {
      while (1)
      {
         char buffer[BUFFER_SIZE];
         ssize_t bytes_read = read(file_fd, buffer, BUFFER_SIZE);
         if (bytes_read <= 0)
         {
            break;
         }

         send(client_fd, buffer, bytes_read, 0);
      }
      close(file_fd);
   }

   if (status == 0)
   {
      syslog(LOG_DEBUG, "No errors, closing client connection");
   }

   close(client_fd);
   syslog(LOG_INFO, "Closed connection from %s", client_ip);

   threadArg->isDone = true;

   return NULL;
}

/**
 * Handles client connection on a TCP server socket
 * References:
 * https://chatgpt.com/share/6990c241-5394-8001-b147-d4dc07ab1402
 * Linux man-pages
 */
int acceptClientConnection(const int sockfd)
{
   int status = 0;

   struct sockaddr_in client_addr;
   socklen_t addr_len = sizeof(client_addr);

   // for each accepted connection create a new fd(client_fd)
   int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_len);
   if (exit_requested == true)
   {
      return 0;
   }
   if (client_fd < 0)
   {
      syslog(LOG_ERR, "accept failed");
      return -1;
   }

   // log IP addr of the connected client
   char client_ip[INET_ADDRSTRLEN];
   // https://man7.org/linux/man-pages/man3/inet_ntop.3.html
   inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
   syslog(LOG_INFO, "Accepted connection from %s", client_ip);

   pthread_t thread;
   ThreadArg *arg = (ThreadArg *)malloc(sizeof(ThreadArg));
   // TODO error if malloc fails
   arg->client_addr = client_addr;
   arg->client_fd = client_fd;
   arg->isDone = false;
   arg->next = NULL;

   int ret = pthread_create(&thread, NULL, processClientConnection, arg);
   if (ret != 0)
   {
      syslog(LOG_ERR, "pthread_create failed");
      return 1;
   }

   arg->thread = thread;

   // add newly created ThreadArg* arg to end of ll
   ThreadArg *var = &dummyNode;
   while (var->next != NULL)
   {
      var = var->next;
   }
   var->next = arg;

   // pthread_join all isDone=true threads
   ThreadArg *prev = &dummyNode;
   ThreadArg *current = dummyNode.next;
   while (current != NULL)
   {
      if (current->isDone == true)
      {
         prev->next = current->next;
         ThreadArg *temp = current;
         current = current->next;

         // free temp
         pthread_join(temp->thread, NULL);
         free(temp);
      }
      else
      {
         prev = current;
         current = current->next;
      }
   }

   return status;
}

/**
 * Entry point for the server application.
 */
int main(int argc, char *argv[])
{
   bool daemon_mode = false;

   // Open syslog
   openlog("aesdsocket", LOG_PID, LOG_USER);

   // check if -d argument is specified by user
   if (argc == 2 && strcmp(argv[1], "-d") == 0)
   {
      syslog(LOG_INFO, "Daemon mode selected");
      daemon_mode = true;
   }

   struct sigaction signal_action;
   memset(&signal_action, 0, sizeof(signal_action));
   signal_action.sa_handler = signalHandler;
   sigaction(SIGINT, &signal_action, NULL);
   sigaction(SIGTERM, &signal_action, NULL);

   // creating socket file descriptor
   int sock_fd = createSocket(daemon_mode);
   if (sock_fd < 0)
   {
      closelog();
      return -1;
   }

// https://chatgpt.com/share/6999e4b4-4eb8-8001-b45e-dd046a60ca70
#if !USE_AESD_CHAR_DEVICE
   timerInit();
#endif

   // client accept + recv loop
   while (exit_requested == false)
   {
      int client_status = acceptClientConnection(sock_fd);
      if (client_status == -1)
      {
         break;
      }
   }

   close(sock_fd);

// https://chatgpt.com/share/6990b14f-5cec-8001-afef-c634acf831d3
#if !USE_AESD_CHAR_DEVICE
   if (remove(FILE_PATH) != 0)
   {
      syslog(LOG_ERR, "Error deleting file");
   }
   syslog(LOG_DEBUG, "Deleted File");
#endif

#if !USE_AESD_CHAR_DEVICE
   timer_delete(timerid); // delete timer
#endif

   closelog(); // Clean up syslog

   return 0;
}
