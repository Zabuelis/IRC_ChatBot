#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

#define TEST_CHANNEL "#forTesting"

char buffer[2048] = { 0 };
pthread_mutex_t message_mutex;
pthread_cond_t message_ready;
bool compile_response = false;
int client_fd;
int counter = 2;
bool cancel_threads = false;

void catch_signal (int sig_num){
    printf("\nExit signal caught \n");
    printf("If you wan't to continue press %i times\n", counter);
    counter--;
    cancel_threads = true;
    fflush(stdout);
    sleep(3);
}

void* server_listener(void* arg);
void* message_compilator(void* arg);
char* get_message(char buffer[]);
void get_LLM_message(char message_LLM[]);
void curl_LLM(char prompt_LLM[]);
char* format_message(char *user_message);


int handle_communications(int fd){
    client_fd = fd;
    signal(SIGINT, catch_signal);

    pthread_t listener, compilator;

    if(pthread_mutex_init(&message_mutex, NULL) != 0){
        perror("Mutex creation failed");
        return -1;
    }

    if(pthread_cond_init(&message_ready, NULL) != 0){
        perror("Condition creation failed");
        return -1;
    }
    
    if(pthread_create(&listener, NULL, &server_listener, NULL) != 0){
        perror("Listener thread failed to create");
        return -1;
    }

    if(pthread_create(&compilator, NULL, &message_compilator, NULL) != 0){
        perror("Compilator thread failed to create");
        return -1;
    }

    while(!cancel_threads){
        sleep(1);
    }
    
    pthread_mutex_destroy(&message_mutex);
    pthread_cond_destroy(&message_ready);
    return 0;
}

void* server_listener(void* arg){
    char target_channel[256] = "PRIVMSG ";
    strcat(target_channel, TEST_CHANNEL);

    while(!cancel_threads){
        FILE *fptr = fopen("./logs/chat.log", "a");
        pthread_mutex_lock(&message_mutex);
        memset(buffer, 0, sizeof(buffer));
        int valread = read(client_fd, buffer, sizeof(buffer) - 1);
        if (valread > 0) {
            fprintf(fptr, "%s", buffer);
            if(strstr(buffer, "PING :")){
                char pong_message[] = "PONG\r\n";
                send(client_fd, pong_message, strlen(pong_message), 0);
                fprintf(fptr, "PING RESPONSE: %s", pong_message);
            } else if(strstr(buffer, target_channel) && !compile_response){
                compile_response = true;
                pthread_cond_signal(&message_ready);
            }
        } else {
            printf("aaaa");
        }
        fclose(fptr);
        pthread_mutex_unlock(&message_mutex);
        sleep(3);
    }
    return NULL;
}

void* message_compilator(void* arg){

    while(!cancel_threads){
        printf("Compilator loop");
        pthread_mutex_lock(&message_mutex);
        while(!compile_response){
            pthread_cond_wait(&message_ready, &message_mutex);
        }
        FILE *fptr = fopen("./logs/chat.log", "a");
        char *user_message;
        char prompt_LLM[1024] = { 0 };
        char message_LLM[4096] = { 0 };
        user_message = get_message(buffer);
        compile_response = false;
        pthread_mutex_unlock(&message_mutex);
        printf("User message: %s", user_message);

        user_message = format_message(user_message);
        snprintf(prompt_LLM, sizeof(prompt_LLM), "{\"model\": \"llama3.2\", \"prompt\": \"%s\"}", user_message);

        curl_LLM(prompt_LLM);
        get_LLM_message(message_LLM);
        dprintf(client_fd, "PRIVMSG %s :%s\r\n", TEST_CHANNEL, message_LLM);

        fprintf(fptr, "%s BOT: %s", TEST_CHANNEL, message_LLM);
        memset(prompt_LLM, 0, sizeof(prompt_LLM));
        memset(message_LLM, 0, sizeof(message_LLM));
        fprintf(fptr, "\n");
        fclose(fptr);

    }
    return NULL;
}

char* get_message(char buffer[]){
    char *message = strstr(buffer, " :");
    if(message != NULL){
        message += 2;
    }
    
    return message;
}

char* format_message(char *user_message){
    for(int i = 0; user_message[i] != '\0'; i++){
        if(user_message[i] == '\n' || user_message[i] == '\r'){
            user_message[i] = ' ';
        }
    }
    return user_message;
}

void curl_LLM(char prompt_LLM[]){
  CURLcode ret;
  CURL *hnd;
  FILE *fileptr;

  fileptr = fopen("./responses/response.json", "w");

  hnd = curl_easy_init();
  curl_easy_setopt(hnd, CURLOPT_BUFFERSIZE, 102400L);
  curl_easy_setopt(hnd, CURLOPT_URL, "http://localhost:11434/api/generate");
  curl_easy_setopt(hnd, CURLOPT_POSTFIELDS, prompt_LLM);
  curl_easy_setopt(hnd, CURLOPT_USERAGENT, "curl/7.88.1");
  curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 50L);
  curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
  curl_easy_setopt(hnd, CURLOPT_CAPATH, "/usr/lib/ssl/certs");
  curl_easy_setopt(hnd, CURLOPT_PROXY_CAPATH, "/usr/lib/ssl/certs");
  curl_easy_setopt(hnd, CURLOPT_FTP_SKIP_PASV_IP, 1L);
  curl_easy_setopt(hnd, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(hnd, CURLOPT_WRITEDATA, fileptr);

  ret = curl_easy_perform(hnd);

  curl_easy_cleanup(hnd);
  hnd = NULL;
  fclose(fileptr);
}

void get_LLM_message(char message_LLM[]){
    cJSON *response = NULL;
    char line[4069];
    FILE *fptr = fopen("responses/response.json", "r");


    while(fgets(line, sizeof(line), fptr)){
        cJSON *string = cJSON_Parse(line);
        if(string == NULL){
            perror("Error reading JSON file");
        }

        response = cJSON_GetObjectItemCaseSensitive(string, "response");
        if(cJSON_IsString(response) && (response->valuestring != NULL)){
            strcat(message_LLM, response->valuestring);
        }

        cJSON_Delete(string);
    }


    fclose(fptr);
}