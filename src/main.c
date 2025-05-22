#include <connection_handler.h>
#include <stdio.h>

int main(){
    if(establish_connection() == -1){
        printf("Socket creation failed");
    }
    printf("Bye\n");
    return 1;
}