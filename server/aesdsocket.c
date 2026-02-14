#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define PORT 9000    // the port users will be connecting to
#define BACKLOG 10   // how many pending connections queue will hold


int main()
{
   //Open syslog
   openlog("aesdsocket", LOG_PID, LOG_USER);

   //creating socket file descriptor
   int sockfd = socket(AF_INET, SOCK_STREAM, 0);
   if (sockfd < 0)
   {
      syslog(LOG_ERR, "Socket failed");
      return -1;
   }

   int opt = 1;
   if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
   {
      syslog(LOG_ERR, "setsockopt failed");
      return -1;
   }

   // configuring server address and PORT
   struct sockaddr_in server_addr;
   memset(&server_addr, 0, sizeof(server_addr));
   server_addr.sin_family = AF_INET;
   server_addr.sin_addr.s_addr = INADDR_ANY;
   server_addr.sin_port = htons(PORT); //byte order conversion from host byte order(little endian) to network byte order(big endian)

   //bind sockfd to PORT
   if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
   {
      syslog(LOG_ERR, "socket bind failed");
      return -1;
   }

   //listen to upto BACKLOG connections
   if (listen(sockfd, BACKLOG) < 0)
   {
      syslog(LOG_ERR, "listen failed");
      return -1;
   }

   struct sockaddr_in client_addr;
   socklen_t addr_len = sizeof(client_addr);
   //for each accepted conection create a new fd(clientfd)
   int clientfd = accept(sockfd,(struct sockaddr *)&client_addr, &addr_len);
   if (clientfd < 0)
   {
      syslog(LOG_ERR, "accept failed");
      return -1;
   }

   //log IP addr of the connected client
   char client_ip[INET_ADDRSTRLEN];
   inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
   syslog(LOG_INFO, "Accepted connection from %s", client_ip);



   syslog(LOG_DEBUG, "No errors, exiting");
   // Clean up syslog
   closelog();
   return 0;
}
