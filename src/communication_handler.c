#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <message_compilator.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <communication_handler.h>

#define LLM_SEMAPHORE "/llm_semaphore"
#define WRITE_SERVER "/to_server"

int channel_num = 2;

pid_t channel_workers[32];
pid_t central_reader;
pid_t response_generator;

void handle_exit(){
    printf("Performing graceful exit... Please wait...\n");
    kill(central_reader, SIGINT);
    kill(response_generator, SIGINT);
    for(int i = 0; i < channel_num; i++){
        kill(channel_workers[i], SIGINT);
    }
}

void sig_action_setup(){
    struct sigaction sa;
    sa.sa_handler = handle_exit;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);;
}

struct RequestLLM {
    int listener_id;
    char prompt[1024];
};

void server_reader(int client_fd, int reader_to_listener[][2], int channel_num, char *channels[32]);
void get_message(char buffer[]);
void server_listener(int client_fd, char channel[], int listener_id, int listener_to_llm, int llm_to_listener, int reader_to_listener);

int handle_communications(int client_fd){
    sem_t *response_semaphore;
    sem_t *to_server_semaphore;
    char *channels[32] = {
        "#testingFor", "#forTesting"
    };


    int reader_to_listener[channel_num][2];
    int listener_to_llm[2];
    int llm_to_listener[channel_num][2];

    // int shm_id = shmget(1234, sizeof(counter), 0666 | IPC_CREAT);
    // if(shm_id == -1){
    //     perror("Shared memory error");
    //     return -1;
    // }

    // counter = shmat(shm_id, NULL, 0);
    // if(counter == (void *)-1){
    //     perror("Shared memory attachment error");
    //     return -1;
    // }

    // *counter = 2;


    for(int i = 0; i < channel_num; i++){
        if(pipe(reader_to_listener[i])){
            perror("Reader to listener pipes failed");
            return -1;
        }
    }

    for(int i = 0; i < channel_num; i++){
        if(pipe(llm_to_listener[i])){
            perror("LLM to listener pipes failed");
            return -1;
        }
    }

    if(pipe(listener_to_llm)){
        perror("Listener to llm pipes failed");
        return -1;
    }

    response_semaphore = sem_open(LLM_SEMAPHORE, O_CREAT | O_EXCL, 0600, 1);
    if(response_semaphore == NULL){
        perror("Response semaphore creation failed");
        return -1;
    }

    to_server_semaphore = sem_open(WRITE_SERVER, O_CREAT | O_EXCL, 0600, 1);
    if(to_server_semaphore == NULL){
        perror("Response semaphore creation failed");
        return -1;
    }

    central_reader = fork();
    if(central_reader < 0){
        perror ("Forking failed");
        return -1;
    } else if(central_reader == 0){
        for(int i = 0; i < channel_num; i++){
            // close read end of pipe reader_to_listener
            close(reader_to_listener[i][0]);
        }
        server_reader(client_fd, reader_to_listener, channel_num, channels);
        exit(0);
    }

    response_generator = fork();
    if(response_generator < 0){
        perror ("Forking failed");
        return -1;
    } else if (response_generator == 0){
        for(int i = 0; i < channel_num; i++){
            // close read side of pipe llm_to_listener
            close(llm_to_listener[i][0]);
        }
        // close write side of pipe listener_to_llm
        close(listener_to_llm[1]);
        message_compilator(listener_to_llm[0], llm_to_listener);
        exit(0);
    }

    for(int i = 0; i < channel_num; i++){
        channel_workers[i] = fork();

        if(channel_workers[i] < 0){
            perror("Forking failed");
            return -1;
        } else if (channel_workers[i] == 0){
            // close write side of the pipe reader_to_listener
            close(reader_to_listener[i][1]);
            // close the write side of the pipe llm_to_listener
            close(llm_to_listener[i][1]);
            // close the read side of the pipe listener_to_llm
            close(listener_to_llm[0]);
            server_listener(client_fd, channels[i], i, listener_to_llm[1], llm_to_listener[i][0], reader_to_listener[i][0]);
            exit(0);
        }
    }

    sig_action_setup();

    waitpid(central_reader, NULL, 0);

    waitpid(response_generator, NULL, 0);

    for(int i = 0; i < channel_num; i++){
        waitpid(channel_workers[i], NULL, 0);
    }

    // shmdt(counter);
    // shmctl(shm_id, IPC_RMID, NULL);

    sem_close(to_server_semaphore);
    sem_unlink(WRITE_SERVER);

    dprintf(client_fd, "QUIT");

    sem_close(response_semaphore);
    sem_unlink(LLM_SEMAPHORE);

    return 1;
}

void server_listener(int client_fd, char channel[], int listener_id, int listener_to_llm, int llm_to_listener, int reader_to_listener){
    struct RequestLLM request;
    request.listener_id = listener_id;
    char message_LLM[4096] = { 0 };
    dprintf(client_fd, "JOIN %s\r\n", channel);
    sem_t *response_semaphore = sem_open(LLM_SEMAPHORE, 0);
    sem_t *to_server_semaphore = sem_open(WRITE_SERVER, 0);
    

    while(1){
        if(read(reader_to_listener, request.prompt, sizeof(request.prompt)) > 0){
            get_message(request.prompt);
            printf("Message inside listener %s\n", request.prompt);
            sem_wait(response_semaphore);
            write(listener_to_llm, &request, sizeof(request));
            read(llm_to_listener, message_LLM, sizeof(message_LLM));
            printf("Message after generation %s\n", message_LLM);
            sem_post(response_semaphore);
            sem_wait(to_server_semaphore);
            dprintf(client_fd, "PRIVMSG %s :%s\r\n", channel, message_LLM);
            sem_post(to_server_semaphore);
            memset(message_LLM, 0, sizeof(message_LLM));
            memset(request.prompt, 0, sizeof(request.prompt));
        }
    }
}

void server_reader(int client_fd, int reader_to_listener[][2], int channel_num, char *channels[32]){
    char buffer[1024] = { 0 };
    char target_channel[256] = { 0 };
    sem_t *to_server_semaphore = sem_open(WRITE_SERVER, 0);
    while(1){
        memset(buffer, 0, sizeof(buffer));
        int valread = read(client_fd, buffer, sizeof(buffer) - 1);
        if (valread > 0) {
            printf("Read buffer %s\n", buffer);
            if(strstr(buffer, "PING :")){
                char pong_message[] = "PONG\r\n";
                sem_wait(to_server_semaphore);
                send(client_fd, pong_message, strlen(pong_message), 0);
                sem_post(to_server_semaphore);
            } else {
                for(int i = 0; i < channel_num; i++){
                    strcat(target_channel, "PRIVMSG ");
                    strcat(target_channel, channels[i]);
                    printf("Channel string %s\n", target_channel);
                    if(strstr(buffer, target_channel)){
                        printf("Match found %s\n", target_channel);
                        write(reader_to_listener[i][1], buffer, sizeof(buffer));
                    }
                    memset(target_channel, 0, sizeof(target_channel));
                }
            }
        }
    }
}
void get_message(char buffer[]){
    buffer = strstr(buffer, " :");
    printf("Buffer in handler %s", buffer);
    if(buffer != NULL){
        buffer += 2;
    }
}