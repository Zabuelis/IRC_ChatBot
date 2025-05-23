#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <message_compilator.h>
#include <communication_handler.h>
#include <unistd.h>

struct RequestLLM {
    int listener_id;
    char prompt[1024];
};

void format_message(char user_message[]);
void curl_LLM(char prompt_LLM[]);
void get_LLM_message(char message_LLM[]);
void message_compilator(int listener_to_llm, int llm_to_listener[][2]){
    struct RequestLLM request;
    char prompt_LLM[1024] = { 0 };
    char message_LLM[2048] = { 0 };

    while(1){
            if(read(listener_to_llm, &request, sizeof(request)) > 0){
            format_message(request.prompt);
            snprintf(prompt_LLM, sizeof(prompt_LLM), "{\"model\": \"llama3.2\", \"prompt\": \"%s\", \"options\": {\"num_predict\": 50}}", request.prompt);
            curl_LLM(prompt_LLM);
            get_LLM_message(message_LLM);
            format_message(message_LLM);
            write(llm_to_listener[request.listener_id][1], message_LLM, sizeof(message_LLM));
            memset(prompt_LLM, 0, sizeof(prompt_LLM));
            memset(message_LLM, 0, sizeof(message_LLM));
        }
    }
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

void format_message(char user_message[]){
    for(int i = 0; user_message[i] != '\0'; i++){
        if(user_message[i] == '\n' || user_message[i] == '\r'){
            user_message[i] = ' ';
        }
    }
}