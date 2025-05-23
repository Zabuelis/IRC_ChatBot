#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <connection_handler.h>
#include <communication_handler.h>

void authentication(int client_fd);

int establish_connection(){
    int status, client_fd;
    struct sockaddr_in serv_addr;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    while(1){
        if((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
            perror("\n Socket creation error \n");
            return -1;
        }

        if(inet_pton(AF_INET, SERVER, &serv_addr.sin_addr) <= 0){
            perror("Address not supported");
            return -1;
        }

        status = connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
        if(status == 0){
            printf("Connection established\n");
            authentication(client_fd);
            printf("Connected to the server\n");
            if(handle_communications(client_fd) == -1){
                perror ("Process creation failed");
                break;
            } else {
                close(client_fd);
                return 1;
            }
            
        } else {
            perror("Connection lost... Retrying...\n");
            close(client_fd);
            sleep(5);
        }
    }
    printf("Shutting down... ");
    close(client_fd);
    return 1;
}

void authentication(int client_fd){
    dprintf(client_fd, "NICK %s\r\n", NICK);
    sleep(2);
    dprintf(client_fd, "USER %s 0 * :%s\r\n", NICK, NICK);
    sleep(2);
}