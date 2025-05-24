#include <connection_handler.h>
#include <stdio.h>
#include <unistd.h>

int main(){
    if(access("config/admin.cfg", F_OK) != 0 || access("config/channels.cfg", F_OK) != 0){
        perror("Missing config required config files");
        return -1;
    }

    if(access("logs/chat.log", F_OK) != 0){
        perror("Missing required log file");
        return -1;
    }

    if(access("responses/response.json", F_OK) != 0){
        perror("Missing required response file");
        return -1;
    }

    if(establish_connection() == -1){
        printf("Socket creation failed");
    }
    printf("Bye\n");
    return 1;
}