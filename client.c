#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdbool.h>
#include <curl/curl.h>

#define SERVER "10.1.0.46"
#define PORT 6667
#define NICK "bkaza0056"
#define TEST_CHANNEL "#forTesting"

static int counter = 2;

void catch_signal (int sig_num){
    printf("\nExit signal caught \n");
    printf("If you wan't to continue press %i times\n", counter);
    counter--;
    fflush(stdout);
}

char* get_message(char buffer[]);
void authentication(int client_fd);

int main(){
    int client_fd, valread = 0, status, tmp_valread=1;
    struct sockaddr_in serv_addr;
    char *hello = "Hello I am a bot";
    char buffer[1024] = { 0 };
    FILE *fptr;
    char target_channel[256] = "PRIVMSG ";
    strcat(target_channel, TEST_CHANNEL);

    signal(SIGINT, catch_signal);

    if((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER, &serv_addr.sin_addr) <= 0){
        printf("\n Address not supported \n");
        return -1;
    }

    if((status = connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) < 0){
        printf("\n Connection failed \n");
        return -1;
    }

    authentication(client_fd);
    dprintf(client_fd, "JOIN %s\r\n", TEST_CHANNEL);
    sleep(2);
    dprintf(client_fd, "PRIVMSG %s :%s\r\n", TEST_CHANNEL, hello);
    sleep(2);
    printf("Hello message sent\n");
    while(true){
        if (counter <= 0){
            break;
        } else{
            memset(buffer, 0, sizeof(buffer));
            valread = read(client_fd, buffer, sizeof(buffer) - 1);

            if (valread != tmp_valread) {
                char *user_message;
                tmp_valread = valread;
                if(strstr(buffer, target_channel)){
                    user_message = get_message(buffer);
                    printf("User message: %s", user_message);
                }
                fptr = fopen("./logs/chat.log", "a");
                fprintf(fptr, "%s", buffer);
                fclose(fptr);
            }
        }
    }

    printf("Exiting...\n");
    close(client_fd);
    return 0;
}

void authentication(int client_fd){
    dprintf(client_fd, "NICK %s\r\n", NICK);
    sleep(2);
    dprintf(client_fd, "USER %s 0 * :%s\r\n", NICK, NICK);
    sleep(2);
    return;
}

char* get_message(char buffer[]){
    char *message = strstr(buffer, " :");
    if(message != NULL){
        message += 2;
    }
    
    return message;
}

