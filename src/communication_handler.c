#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <message_compilator.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>

int counter = 2;


void catch_signal (int sig_num){
    printf("\nExit signal caught \n");
    printf("Closing gratefully\n");
    counter--;
    
}

void server_listener(int client_fd, char channel[]);
char* get_message(char buffer[]);

int handle_communications(int client_fd){
    signal(SIGINT, catch_signal);
    char *channels[32] = {
        "#cave", "#forTesting"
    };
    int channel_num = 2;
    pid_t listeners[channel_num];

    for(int i = 0; i < channel_num; i++){
        listeners[i] = fork();

        if(listeners[i] < 0) {
            perror("Forking failed");
            return -1;
        }

        if(listeners[i] == 0){
            server_listener(client_fd, channels[i]);
            exit(0);
        }
    }

    for(int i = 0; i < channel_num; i++){
        waitpid(listeners[i], NULL, 0);
    }
    return 1;
}

void server_listener(int client_fd, char channel[]){
    char target_channel[256] = "PRIVMSG ";
    strcat(target_channel, channel);
    char message_LLM[4096] = { 0 };
    char buffer[2048] = { 0 };
    dprintf(client_fd, "JOIN %s\r\n", channel);

    while(counter > 0){
        // FILE *fptr = fopen("./logs/chat.log", "a");
        memset(buffer, 0, sizeof(buffer));
        int valread = read(client_fd, buffer, sizeof(buffer) - 1);
        if (valread > 0) {
            printf("Buffer received");
            if(strstr(buffer, "PING :")){
                char pong_message[] = "PONG\r\n";
                send(client_fd, pong_message, strlen(pong_message), 0);
                // fprintf(fptr, "PING RESPONSE: %s", pong_message);
            } else if(strstr(buffer, target_channel)){
                printf("Message generation triggered\n");
                char *user_message = get_message(buffer);
                message_compilator(message_LLM, user_message);
                dprintf(client_fd, "PRIVMSG %s :%s\r\n", channel, message_LLM);

                // fprintf(fptr, "%s BOT: %s", TEST_CHANNEL, message_LLM);
                memset(message_LLM, 0, sizeof(message_LLM));
            }
        }
        // fclose(fptr);
    }
}

char* get_message(char buffer[]){
    char *message = strstr(buffer, " :");
    if(message != NULL){
        message += 2;
    }
    
    return message;
}