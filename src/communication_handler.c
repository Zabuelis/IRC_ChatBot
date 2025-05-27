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
#include <stdbool.h>
#include <errno.h>
#include <communication_handler.h>

#define LLM_SEMAPHORE "/llm_semaphore"
#define WRITE_SERVER "/to_server"
#define WRITE_FILE "/to_file"
#define MAX_CHANNELS 32

int channel_num;
bool *is_socket_alive;

pid_t channel_workers[MAX_CHANNELS];
pid_t central_reader;
pid_t response_generator;
pid_t logger;
pid_t admin;

struct RequestLLM {
    int listener_id;
    char prompt[1024];
};

struct IgnoredUsers {
    int count;
    char user_name[10][10];
};

struct MutedChannels {
    int count;
    char chan_name[32][64];
};

void handle_exit(){
    if(*is_socket_alive){
        printf("Performing graceful exit... Please wait...\n");
    } else {
        printf("It appears that socket has disconnected... Please wait...\n");
    }
    kill(central_reader, SIGINT);
    kill(response_generator, SIGINT);
    kill(logger, SIGINT);
    kill(admin, SIGINT);
    for(int i = 0; i < channel_num; i++){
        kill(channel_workers[i], SIGINT);
    }
}

void sig_action_setup(){
    struct sigaction sa;
    sa.sa_handler = handle_exit;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
}

void server_reader(int client_fd, int reader_to_listener[][2], int channel_num, char channels[][64], char admin_channel_name[], int reader_to_admin, struct IgnoredUsers *ignored_users, bool *is_socket_alive, struct MutedChannels *muted_channels);
void get_message(char buffer[]);
void server_listener(int client_fd, char channel[], int listener_id, int listener_to_llm, int llm_to_listener, int reader_to_listener, pid_t logger_pid, char *log_message);
void server_logger(FILE *fptr, char *log_message);
void logger_wake_signal(int sig);
void admin_channel(char admin_name[], char admin_pass[], int reader_to_admin, char *log_message, pid_t logger_pid, int client_fd, struct IgnoredUsers *ignored_users, struct MutedChannels *muted_channels, struct Topics *topics);
int channel_read(FILE *fptr, char channels[][64]);
void load_admin_config(FILE *fptr, char admin_channel_name[], char admin_channel_password[]);

int handle_communications(int client_fd){
    sem_t *response_semaphore;
    sem_t *to_server_semaphore;
    sem_t *to_file_semaphore;
    char channels[MAX_CHANNELS][64];
    char admin_channel_name[64];
    char admin_channel_password[64];

    FILE* admin_file;
    admin_file = fopen("config/admin.cfg", "r");
    if(admin_file == NULL){
        perror("Failed to access file");
        return -2;
    }
    load_admin_config(admin_file, admin_channel_name, admin_channel_password);
    fclose(admin_file);

    FILE* channels_file;
    channels_file = fopen("config/channels.cfg", "r");
    if(channels_file == NULL){
        perror("Failed to access file");
        return -2;
    }
    channel_num = channel_read(channels_file, channels);
    fclose(channels_file);

    if(channel_num <= 0 || channel_num > MAX_CHANNELS){
        perror("You've reached out of bounds for the number of channels, please adjust your channels.cfg file");
        return -2;
    }

    int reader_to_listener[channel_num][2];
    int listener_to_llm[2];
    int llm_to_listener[channel_num][2];
    int reader_to_admin[2];

    char *log_message;

    // Creating pipes reader_to_listener
    for(int i = 0; i < channel_num; i++){
        if(pipe(reader_to_listener[i])){
            perror("Reader to listener pipes failed");
            return -2;
        }
    }

    // Creating pipes llm_to_listener
    for(int i = 0; i < channel_num; i++){
        if(pipe(llm_to_listener[i])){
            perror("LLM to listener pipes failed");
            return -2;
        }
    }

    // Creating pipe listener_to_llm
    if(pipe(listener_to_llm)){
        perror("Listener to llm pipe failed");
        return -2;
    }

    // Creating pipe reader_to_admin
    if(pipe(reader_to_admin)){
        perror("Reader to admin pipe failed");
        return -2;
    }

    // Creating a semaphore used in generating response messages
    response_semaphore = sem_open(LLM_SEMAPHORE, O_CREAT | O_EXCL, 0600, 1);
    if(response_semaphore == NULL){
        perror("Response semaphore creation failed");
        return -2;
    }

    // Creating a semaphore used in sending messages to the server
    to_server_semaphore = sem_open(WRITE_SERVER, O_CREAT | O_EXCL, 0600, 1);
    if(to_server_semaphore == NULL){
        perror("To server semaphore creation failed");
        return -2;
    }

    // Creating a semaphore used in writing log file
    to_file_semaphore = sem_open(WRITE_FILE, O_CREAT | O_EXCL, 0600, 1);
    if(to_file_semaphore == NULL){
        perror("To file semaphore creation failed");
        return -2;
    }

    // Allocating shared memory for logging
    int shm_id_logging = shmget(1234, 2048, 0666 | IPC_CREAT);
    if(shm_id_logging == -1){
        perror("Shared memory error");
        return -2;
    }

    log_message = shmat(shm_id_logging, NULL, 0);
    if(log_message == (void *)-1){
        perror("Shared memory attachment error");
        return -2;
    }

    // Allocating shared memory for ignored user list
    int shm_id_ignore = shmget(4321, sizeof(struct IgnoredUsers), 0666 | IPC_CREAT);
    if(shm_id_ignore == -1){
        perror("Shared memory error");
        return -2;
    }

    struct IgnoredUsers *ignored_users = shmat(shm_id_ignore, NULL, 0);
    if(ignored_users == (void *)-1){
        perror("Shared memory attachment error");
        return -2;
    }

    // Allocating shared memory for socket status monitoring
    int shm_id_socket = shmget(4231, sizeof(is_socket_alive), 0666 | IPC_CREAT);
    if(shm_id_socket == -1){
        perror("Shared memory error");
        return -2;
    }

    is_socket_alive = shmat(shm_id_socket, NULL, 0);
    if(is_socket_alive == (void *)-1){
        perror("Shared memory error");
        return -2;
    }

    *is_socket_alive = true;

    // Allocating shared memory for muted channels
    int shm_id_muted_channels = shmget(1111, sizeof(struct MutedChannels), 0666 | IPC_CREAT);
    if(shm_id_muted_channels == -1){
        perror("Shared memory error");
        return -2;
    }
    
    struct MutedChannels *muted_channels = shmat(shm_id_muted_channels, NULL, 0);
    if(muted_channels == (void *)-1){
        perror("Shared memory error");
        return -2;
    }

    // Allocating shared memory for topic selection
    int shm_id_topics = shmget(4444, sizeof(struct Topics), 0666 | IPC_CREAT);
    if(shm_id_topics == -1){
        perror("Shared memory error");
        return -2;
    }

    struct Topics *topics = shmat(shm_id_topics, NULL, 0);
    if(topics == (void *)-1){
        perror("Shared memory error");
        return -2;
    }

    topics->topic_num = 0;
    strncpy(topics->selected_topic[0], "Topic:Unix", 64);
    strncpy(topics->selected_topic[1], "Topic:Cooking", 64);

    central_reader = fork();
    if(central_reader < 0){
        perror ("Forking failed");
        return -2;
    } else if(central_reader == 0){
        for(int i = 0; i < channel_num; i++){
            // Close read end of pipe reader_to_listener
            close(reader_to_listener[i][0]);
        }
        // Close read end of pipe reader_to_admin
        close(reader_to_admin[0]);
        server_reader(client_fd, reader_to_listener, channel_num, channels, admin_channel_name, reader_to_admin[1], ignored_users, is_socket_alive, muted_channels);
        exit(0);
    }

    response_generator = fork();
    if(response_generator < 0){
        perror ("Forking failed");
        return -2;
    } else if (response_generator == 0){
        for(int i = 0; i < channel_num; i++){
            // Close read side of pipe llm_to_listener
            close(llm_to_listener[i][0]);
        }
        // Close write side of pipe listener_to_llm
        close(listener_to_llm[1]);
        message_compilator(listener_to_llm[0], llm_to_listener, topics);
        exit(0);
    }

    FILE *fptr;
    fptr = fopen("logs/chat.log", "a");
    logger = fork();
    if(logger < 0){
        perror ("Forking failed");
        return -2;
    } else if (logger == 0){
        server_logger(fptr, log_message);
        exit(0);
    }

    admin = fork();
    if(admin < 0){
        perror("Forking failed");
        return -2;
    } else if(admin == 0){
        // Close the write end of the reader_to_admin pipe
        close(reader_to_admin[1]);
        admin_channel(admin_channel_name, admin_channel_password, reader_to_admin[0], log_message, logger, client_fd, ignored_users, muted_channels, topics);
        exit(0);
    }

    for(int i = 0; i < channel_num; i++){
        channel_workers[i] = fork();
        if(channel_workers[i] < 0){
            perror("Forking failed");
            return -2;
        } else if (channel_workers[i] == 0){
            // Close write side of the pipe reader_to_listener
            close(reader_to_listener[i][1]);
            // Close the write side of the pipe llm_to_listener
            close(llm_to_listener[i][1]);
            // Close the read side of the pipe listener_to_llm
            close(listener_to_llm[0]);
            server_listener(client_fd, channels[i], i, listener_to_llm[1], llm_to_listener[i][0], reader_to_listener[i][0], logger, log_message);
            exit(0);
        }
    }

    sig_action_setup();

    waitpid(central_reader, NULL, 0);

    waitpid(response_generator, NULL, 0);

    waitpid(logger, NULL , 0);

    waitpid(admin, NULL , 0);

    for(int i = 0; i < channel_num; i++){
        waitpid(channel_workers[i], NULL, 0);
    }

    fclose(fptr);

    shmdt(log_message);
    shmctl(shm_id_logging, IPC_RMID, NULL);

    shmdt(ignored_users);
    shmctl(shm_id_ignore, IPC_RMID, NULL);

    shmdt(muted_channels);
    shmctl(shm_id_muted_channels, IPC_RMID, NULL);

    shmdt(topics);
    shmctl(shm_id_topics, IPC_RMID, NULL);

    dprintf(client_fd, "QUIT");

    sem_close(to_server_semaphore);
    sem_unlink(WRITE_SERVER);

    sem_close(response_semaphore);
    sem_unlink(LLM_SEMAPHORE);

    sem_close(to_file_semaphore);
    sem_unlink(WRITE_FILE);

    if(*is_socket_alive){
        shmdt(is_socket_alive);
        shmctl(shm_id_socket, IPC_RMID, NULL);
        return 1;
    } else if(!*is_socket_alive){
        shmdt(is_socket_alive);
        shmctl(shm_id_socket, IPC_RMID, NULL);
        return -1;
    }
}

// Function used for processes that monitor specified channels
void server_listener(int client_fd, char channel[], int listener_id, int listener_to_llm, int llm_to_listener, int reader_to_listener, pid_t logger_pid, char *log_message){
    struct RequestLLM request;
    request.listener_id = listener_id;
    char message_LLM[2048] = { 0 };
    sem_t *response_semaphore = sem_open(LLM_SEMAPHORE, 0);
    sem_t *to_server_semaphore = sem_open(WRITE_SERVER, 0);
    sem_t *to_file_semaphore = sem_open(WRITE_FILE, 0);

    sem_wait(to_server_semaphore);
    dprintf(client_fd, "JOIN %s\r\n", channel);
    sem_post(to_server_semaphore);
    

    while(1){
        if(read(reader_to_listener, request.prompt, sizeof(request.prompt)) > 0){

            sem_wait(to_file_semaphore);
            strcpy(log_message, request.prompt);
            kill(logger_pid, SIGALRM);
            usleep(10000);
            sem_post(to_file_semaphore);

            get_message(request.prompt);
            sem_wait(response_semaphore);
            write(listener_to_llm, &request, sizeof(request));
            read(llm_to_listener, message_LLM, sizeof(message_LLM));
            sem_post(response_semaphore);

            sem_wait(to_server_semaphore);
            dprintf(client_fd, "PRIVMSG %s :%s\r\n", channel, message_LLM);
            sem_post(to_server_semaphore);

            sem_wait(to_file_semaphore);
            strcat(message_LLM, channel);
            strcpy(log_message, message_LLM);
            kill(logger_pid, SIGALRM);
            usleep(10000);
            sem_post(to_file_semaphore);

            memset(message_LLM, 0, sizeof(message_LLM));
            memset(request.prompt, 0, sizeof(request.prompt));
        }
    }
}

// Function used by a process that monitors all data received from the server
void server_reader(int client_fd, int reader_to_listener[][2], int channel_num, char channels[][64], char admin_channel_name[], int reader_to_admin, struct IgnoredUsers *ignored_users, bool *is_socket_alive, struct MutedChannels *muted_channels){
    char buffer[1024] = { 0 };
    char target_channel[256] = { 0 };
    sem_t *to_server_semaphore = sem_open(WRITE_SERVER, 0);
    char admin_channel[256] = { 0 };
    pid_t parent_pid = getppid();
    strcat(admin_channel, "PRIVMSG ");
    strcat(admin_channel, admin_channel_name);

    while(1){
        memset(buffer, 0, sizeof(buffer));
        int valread = read(client_fd, buffer, sizeof(buffer) - 1);
        if (valread > 0) {
            if(strstr(buffer, "PING :")){
                char pong_message[] = "PONG\r\n";
                sem_wait(to_server_semaphore);
                send(client_fd, pong_message, strlen(pong_message), 0);
                sem_post(to_server_semaphore);
            } else {
                if(strstr(buffer, admin_channel)){
                    write(reader_to_admin, buffer, sizeof(buffer));
                } else {
                    for(int i = 0; i < channel_num; i++){
                        strcat(target_channel, "PRIVMSG ");
                        strcat(target_channel, channels[i]);
                        if(strstr(buffer, target_channel)){
                            bool match_user_found = false;
                            bool match_channel_found = false;
                            for(int j = 0; j < muted_channels->count; j++){
                                char channel[256] = {"PRIVMSG "};
                                strcat(channel, muted_channels->chan_name[j]);
                                if(strcmp(channel, target_channel) == 0){
                                    match_channel_found = true;
                                    break;
                                }
                            }
                            if(!match_channel_found){
                                for(int j = 0; j < ignored_users->count; j++){
                                    char user[20] = { ":" };
                                    strcat(user, ignored_users->user_name[j]);
                                    strcat(user, "!");
                                    if(strstr(buffer, user)){
                                        match_user_found = true;
                                        break;
                                    }
                                }
                            }
                            if(!match_user_found && !match_channel_found){
                                write(reader_to_listener[i][1], buffer, sizeof(buffer));
                            }
                        }
                        memset(target_channel, 0, sizeof(target_channel));
                    }
                }
            }
        } else if(valread == 0 || (valread == -1 && errno != EWOULDBLOCK && errno != EAGAIN)){
            *is_socket_alive = false;
            kill(parent_pid, SIGINT);
            break;
        }
    }
}

void get_message(char buffer[]){
    int pos = -1;
    int len;
    bool is_second = false;
    for(int i = 0; buffer[i] != '\0'; i++){
        if(buffer[i] == ':'){
            pos = i + 1;
            if(is_second){
                break;
            }
            is_second = true;
        }
    }

    if(pos != -1){
        len = strlen(buffer + pos);
        memmove(buffer, buffer + pos, len + 1);
    } else{
        buffer[0] = '\0';
    }
}

// Function used by a process that logs data
void server_logger(FILE *fptr, char *log_message){
    signal(SIGALRM, logger_wake_signal);
    while(true){
        pause();
        if(*log_message != '\0'){
            fprintf(fptr, "%s\n", log_message);
            fflush(fptr);
            memset(log_message, 0, 2048);
            *log_message = '\0';
        }
    }
}

void logger_wake_signal(int sig){}

int channel_read(FILE *fptr, char channels[][64]){
    int i;
    for(i = 0; i < MAX_CHANNELS; i++){
        if(fgets(channels[i], 64, fptr) == NULL){
            break;
        }
        for(int j = 0; j < strlen(channels[i]); j++){
            if(channels[i][j] == '\n'){
                channels[i][j] = '\0';
            }
        }
    }
    return i;
}

void load_admin_config(FILE *fptr, char admin_channel_name[], char admin_channel_password[]){
    char tmp[256];
    while(fgets(tmp, 256, fptr) != NULL){
        tmp[strcspn(tmp, "\n")] = '\0';
        if(strstr(tmp, "name: ")){
            strncpy(admin_channel_name, tmp + 6, strcspn(tmp, "\0"));
        } else if(strstr(tmp, "password: ")){
            strncpy(admin_channel_password, tmp + 10, strcspn(tmp, "\0"));
        }
    }
}

// Function used by a process that handles admin channel
void admin_channel(char admin_name[], char admin_pass[], int reader_to_admin, char *log_message, pid_t logger_pid, int client_fd, struct IgnoredUsers *ignored_users, struct MutedChannels *muted_channels, struct Topics *topics){
    char buffer[1024] = { 0 };
    sem_t *to_file_semaphore = sem_open(WRITE_FILE, 0);
    sem_t *to_server_semaphore = sem_open(WRITE_SERVER, 0);
    pid_t parent_pid = getppid();

    sem_wait(to_server_semaphore);
    dprintf(client_fd, "JOIN %s\r\n", admin_name);
    sem_post(to_server_semaphore);

    sem_wait(to_server_semaphore);
    dprintf(client_fd, "MODE %s +k %s\r\n", admin_name, admin_pass);
    sem_post(to_server_semaphore);
    

    while(1){
        if(read(reader_to_admin, buffer, sizeof(buffer) - 1) > 0){

            sem_wait(to_file_semaphore);
            strcpy(log_message, buffer);
            kill(logger_pid, SIGALRM);
            usleep(10000);
            sem_post(to_file_semaphore);

            get_message(buffer);
            if(strstr(buffer, "ignore ")){
                if(ignored_users->count > 9){
                    sem_wait(to_server_semaphore);
                    dprintf(client_fd, "PRIVMSG %s :It seems that the maximum number of ignored users has been reached\r\n", admin_name);
                    sem_post(to_server_semaphore);
                } else {
                    buffer[strcspn(buffer, "\r")] = '\0';
                    int i = ignored_users->count;
                    strncpy(ignored_users->user_name[i], buffer + 7, strcspn(buffer, "\0"));
                    sem_wait(to_server_semaphore);
                    dprintf(client_fd, "PRIVMSG %s :User %s will now be ignored\r\n", admin_name, ignored_users->user_name[i]);
                    sem_post(to_server_semaphore);
                    ignored_users->count += 1;
                }
            } else if(strstr(buffer, "poweroff")){
                kill(parent_pid, SIGINT);
            } else if(strstr(buffer, "donotchat ")){
                if(muted_channels->count > 31){
                    sem_wait(to_server_semaphore);
                    dprintf(client_fd, "PRIVMSG %s :It seems that the maximum number of muted channels has been reached\r\n", admin_name);
                    sem_post(to_server_semaphore);
                } else {
                    buffer[strcspn(buffer, "\r")] = '\0';
                    int i = muted_channels->count;
                    strncpy(muted_channels->chan_name[i], buffer + 10, strcspn(buffer, "\0"));
                    sem_wait(to_server_semaphore);
                    dprintf(client_fd, "PRIVMSG %s :Channel %s will now be ignored\r\n", admin_name, muted_channels->chan_name[i]);
                    sem_post(to_server_semaphore);
                    muted_channels->count += 1;
                }
            } else if(strstr(buffer, "topic ")){
                char tmp;
                tmp = buffer[6];
                if(tmp == '0'){
                    topics->topic_num = 0;
                    sem_wait(to_server_semaphore);
                    dprintf(client_fd, "PRIVMSG %s :No topic will be used now\r\n", admin_name);
                    sem_post(to_server_semaphore);
                } else if(tmp == '1'){
                    topics->topic_num = 1;
                    sem_wait(to_server_semaphore);
                    dprintf(client_fd, "PRIVMSG %s :Topic: %s will be used now\r\n", admin_name, topics->selected_topic[topics->topic_num - 1]);
                    sem_post(to_server_semaphore);
                } else if(tmp == '2'){
                    topics->topic_num = 2;
                    sem_wait(to_server_semaphore);
                    dprintf(client_fd, "PRIVMSG %s :Topic: %s will be used now\r\n", admin_name, topics->selected_topic[topics->topic_num - 1]);
                    sem_post(to_server_semaphore);
                } else {
                    sem_wait(to_server_semaphore);
                    dprintf(client_fd, "PRIVMSG %s :Topic choice is invalid\r\n", admin_name);
                    sem_post(to_server_semaphore);
                }
            }
        }
    }
}