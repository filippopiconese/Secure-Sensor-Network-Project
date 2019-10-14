#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

#define BUF_SIZE 11

struct sockaddr_in6 i6sock;

int main() {
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    char buf[BUF_SIZE];

    //assign port number, family and address to the structure
    in_port_t port = 7777;
    sa_family_t family = AF_INET6;

    i6sock.sin6_port = htons(port);
    i6sock.sin6_family = family;
    i6sock.sin6_addr = in6addr_any;

    socklen_t addressLength = sizeof(struct sockaddr *);
    int stat = bind(sock, (struct sockaddr*) &i6sock, sizeof(i6sock));
    if(stat == -1) {
        printf("Error binding socket. Closing the server!\n");
        return -1;
    }
    printf("UDP server is running on port %d\n", port);
    while(1) {
        stat = recvfrom(sock, buf, BUF_SIZE, 0, (struct sockaddr*) &i6sock, (socklen_t *)&addressLength);
        
        printf("Received message: %s\n", buf);

        // char* response = "Border router reply";
        // if(sendto(sock, response, strlen(response), 1, (struct sockaddr*) &i6sock, (socklen_t)addressLength) != -1) {
        //     printf("Sending back a response\n");
        // }
    }
    return 0;
}
