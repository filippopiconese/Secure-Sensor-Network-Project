#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

#define BUF_SIZE 100

struct sockaddr_in6 i6sock;

int main()
{
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    int bytes_received = 0;
    char buf[BUF_SIZE];

    //assign port number, family and address to the structure
    in_port_t port = 7777;
    sa_family_t family = AF_INET6;

    i6sock.sin6_port = htons(port);
    i6sock.sin6_family = family;
    i6sock.sin6_addr = in6addr_any;

    socklen_t addressLength = sizeof(struct sockaddr *);

    if (bind(sock, (struct sockaddr *)&i6sock, sizeof(i6sock)) < 0)
    {
        printf("Error binding socket. Closing the server!\n");
        return -1;
    }

    printf("UDP server is running on port %d\n", port);
    while (1)
    {
        bytes_received = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&i6sock, (socklen_t *)&addressLength);

        printf("\nData received: '%.*s'", bytes_received, buf);
    }

    return 0;
}
