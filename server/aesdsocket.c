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


#define PORT 9000  // the port users will be connecting to
#define BACKLOG 10 // how many pending connections queue will hold

#define BUFFER_SIZE 1024

#define FILE_PATH "/var/tmp/aesdsocketdata"

static volatile bool exit_requested = false;

typedef struct RecvDataLinkedList
{
   char *buffer;
   unsigned int len;
   struct RecvDataLinkedList *next;
} RecvDataLinkedList;

void signalHandler(int signo)
{
   if (signo == SIGINT || signo == SIGTERM)
   {
      syslog(LOG_INFO, "Caught signal, exiting");
      exit_requested = true;
   }
}

int createSocket()
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

   // listen to upto BACKLOG connections
   if (listen(sockfd, BACKLOG) < 0)
   {
      syslog(LOG_ERR, "listen failed");
      return -1;
   }

   return sockfd;
}

int handleClientConnection(const int sockfd)
{
   int status = 0;

   struct sockaddr_in client_addr;
   socklen_t addr_len = sizeof(client_addr);

   // for each accepted connection create a new fd(client_fd)
   int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_len);
   if(exit_requested == true)
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
   inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
   syslog(LOG_INFO, "Accepted connection from %s", client_ip);

   RecvDataLinkedList dummyhead;
   dummyhead.buffer = NULL;
   dummyhead.len = 0;
   dummyhead.next = NULL;

   RecvDataLinkedList *node = &dummyhead;
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

   // if any data is received, open the file
   int writefile_fd = -1;
   if (node != NULL)
   {
      writefile_fd = open(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
      if (writefile_fd < 0)
      {
         syslog(LOG_ERR, "writefile open failed");
         status = -1;
      }
   }

   // copy received data into the file
   while (writefile_fd >= 0 && node != NULL)
   {
      syslog(LOG_DEBUG, "writing %d bytes to file", node->len);
      size_t bytes_to_write = node->len;
      size_t bytes_written = write(writefile_fd, node->buffer, bytes_to_write);
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

   if (writefile_fd >= 0)
   {
      close(writefile_fd);
   }

   int readfilefd = open(FILE_PATH, O_RDONLY);
   if (readfilefd >= 0)
   {
      while (1)
      {
         char buffer[BUFFER_SIZE];
         ssize_t bytes_read = read(readfilefd, buffer, BUFFER_SIZE);
         if (bytes_read <= 0)
         {
            break;
         }

         send(client_fd, buffer, bytes_read, 0);
      }
      close(readfilefd);
   }
   else
   {
      syslog(LOG_ERR, "read file open failed");
      status = -1;
   }

   if (status == 0)
   {
      syslog(LOG_DEBUG, "No errors, closing client connection");
   }

   close(client_fd);
   syslog(LOG_INFO, "Closed connection from %s", client_ip);

   return status;
}

int main()
{
   // Open syslog
   openlog("aesdsocket", LOG_PID, LOG_USER);

   struct sigaction signal_action;
   memset(&signal_action, 0, sizeof(signal_action));
   signal_action.sa_handler = signalHandler;
   sigaction(SIGINT, &signal_action, NULL);
   sigaction(SIGTERM, &signal_action, NULL);

   // creating socket file descriptor
   int sock_fd = createSocket();
   if (sock_fd < 0)
   {
      return -1;
   }

   // client accept + recv loop
   while (exit_requested == false)
   {
      int client_status = handleClientConnection(sock_fd);
      if (client_status == -1)
      {
         break;
      }
   }

   close(sock_fd);

   // https://chatgpt.com/share/6990b14f-5cec-8001-afef-c634acf831d3
   if (remove(FILE_PATH) != 0)
   {
      syslog(LOG_ERR, "Error deleting file");
   }
   syslog(LOG_DEBUG, "Deleted File");


   // Clean up syslog
   closelog();

   return 0;
}
