#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define PORT 9000  // the port users will be connecting to
#define BACKLOG 10 // how many pending connections queue will hold

#define BUFFER_SIZE 1024

#define FILE_PATH "/var/tmp/aesdsocketdata"

typedef struct RecvDataLinkedList
{
   char *buffer;
   unsigned int len;
   struct RecvDataLinkedList *next;
} RecvDataLinkedList;

int main()
{
   // Open syslog
   openlog("aesdsocket", LOG_PID, LOG_USER);

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

   // client accept + recv loop
   while (1)
   {
      struct sockaddr_in client_addr;
      socklen_t addr_len = sizeof(client_addr);
      // for each accepted conection create a new fd(clientfd)
      int clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_len);
      if (clientfd < 0)
      {
         syslog(LOG_ERR, "accept failed");
         return -1;
      }

      // log IP addr of the connected client
      char client_ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
      syslog(LOG_INFO, "Accepted connection from %s", client_ip);

      RecvDataLinkedList head;
      head.buffer = NULL;
      head.len = 0;
      head.next = NULL;

      RecvDataLinkedList *node = &head;
      while (1)
      {
         char buffer[BUFFER_SIZE];

         syslog(LOG_DEBUG, "Trying to recv from %s", client_ip);

         // Receive data from socket of maximum size = BUFFER_SIZE
         ssize_t bytes = recv(clientfd, buffer, BUFFER_SIZE, 0);
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
         RecvDataLinkedList newnode;
         newnode.buffer = temp;
         newnode.len = bytes;
         newnode.next = NULL;

         // add the new node to the ll and move the ll ahead
         node->next = &newnode;
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

      // if any data is received, open the file
      int writefilefd = -1;
      if (head.next != NULL)
      {
         writefilefd = open(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
         if (writefilefd < 0)
         {
            syslog(LOG_ERR, "writefile open failed");
         }
      }

      // copy received data into the file
      while (writefilefd >= 0 && head.next != NULL)
      {
         head = *head.next;
         syslog(LOG_DEBUG, "writing %d bytes to file", head.len);
         write(writefilefd, head.buffer, head.len);
         free(head.buffer);
      }

      if (writefilefd >= 0)
      {
         close(writefilefd);
      }

      int readfilefd = open(FILE_PATH, O_RDONLY);
      if (readfilefd < 0)
      {
         syslog(LOG_ERR, "read file open failed");
      }

      while (1)
      {
         char buffer[BUFFER_SIZE];
         ssize_t bytes_read = read(readfilefd, buffer, BUFFER_SIZE);
         if (bytes_read <= 0)
         {
            break;
         }

         send(clientfd, buffer, bytes_read, 0);
      }
      close(readfilefd);

      syslog(LOG_DEBUG, "No errors, closing client connection");

      close(clientfd);
      syslog(LOG_INFO, "Closed connection from %s", client_ip);


   }

   close(sockfd);

   // Clean up syslog
   closelog();

   return 0;
}
