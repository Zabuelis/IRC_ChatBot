#ifndef MESSAGE_COMPILATOR
#define MESSAGE_COMPILATOR

void message_compilator(int listener_to_llm, int llm_to_listener[][2]);

#define LLM_URL "http://localhost:11434/api/generate"
#define LLM_MODEL "llama3.2"

#endif