#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <signal.h>
#include <connection_handler.h>
#include <communication_handler.h>

int i = 5;

void authentication(int client_fd);
void catch_signal(int sig_num){
    printf("Exit signal caught\n");
    i = 0;
}

int establish_connection(){
    int status, client_fd;
    struct sockaddr_in serv_addr;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    int timer = 5;

    while(i > 0){
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
            signal(SIGINT, SIG_DFL);
            if(handle_communications(client_fd) != -1){
                break;
            } else {
                signal(SIGINT, catch_signal);
                printf("Connection lost... Retrying in %i seconds...\n", timer);
                close(client_fd);
                i--;
                printf("Attempts left: %i \n", i);
                sleep(timer);
                timer += 20;
            }
            
        } else {
            signal(SIGINT, catch_signal);
            printf("Connection refused... Retrying in %i seconds...\n", timer);
            i--;
            printf("Attempts left: %i \n", i);
            close(client_fd);
            sleep(timer);
            timer += 20;
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