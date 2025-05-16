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
void curl_LLM(char prompt_LLM[]);
char* format_message(char user_message[]);

int main(){
    int client_fd, valread = 0, status, tmp_valread=1;
    struct sockaddr_in serv_addr;
    char *hello = "Hello I am a bot";
    char buffer[1024] = { 0 };
    FILE *fptr;
    char target_channel[256] = "PRIVMSG ";
    char prompt_LLM[1024] = { 0 };
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
                    format_message(user_message);
                    snprintf(prompt_LLM, sizeof(prompt_LLM), "{\"model\": \"llama3.2\", \"prompt\": \"%s\"}", user_message);

                    curl_LLM(prompt_LLM);
                    fopen("./responses/response.json", "r");
                    char buff[100];
                    while(fgets(buff, sizeof(buff), fptr) != NULL){
                        dprintf(client_fd, "PRIVMSG %s :%s\r\n", TEST_CHANNEL, buff);
                    }
                    fclose(fptr);
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

char* format_message(char *user_message){
    for(int i = strlen(user_message) - 4; user_message[i] != '\0'; i++){
        if(user_message[i] == '\n'){
            user_message[i] = ' ';
        } else if (user_message[i] == '\r'){
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

  /* Here is a list of options the curl code used that cannot get generated
     as source easily. You may choose to either not use them or implement
     them yourself.

  CURLOPT_WRITEDATA was set to an object pointer
  CURLOPT_INTERLEAVEDATA was set to an object pointer
  CURLOPT_WRITEFUNCTION was set to a function pointer
  CURLOPT_READDATA was set to an object pointer
  CURLOPT_READFUNCTION was set to a function pointer
  CURLOPT_SEEKDATA was set to an object pointer
  CURLOPT_SEEKFUNCTION was set to a function pointer
  CURLOPT_ERRORBUFFER was set to an object pointer
  CURLOPT_STDERR was set to an object pointer
  CURLOPT_HEADERFUNCTION was set to a function pointer
  CURLOPT_HEADERDATA was set to an object pointer

  */

  ret = curl_easy_perform(hnd);

  curl_easy_cleanup(hnd);
  hnd = NULL;
  fclose(fileptr);
}

