#ifndef MESSAGE_COMPILATOR
#define MESSAGE_COMPILATOR

struct Topics {
    int topic_num;
    char selected_topic[2][64];
};

void message_compilator(int listener_to_llm, int llm_to_listener[][2], struct Topics *topics);

#define LLM_URL "http://localhost:11434/api/generate"
#define LLM_MODEL "llama3.2"
#define TOKEN_SIZE 30

#endif