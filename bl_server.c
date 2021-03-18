#include "blather.h"
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <signal.h>
#include <semaphore.h>
#include <poll.h>
#include <limits.h>

int interrupted = 0;

void handleSig(int sig){interrupted = 1;}

int main(int argc, char *argv[]){
    putenv("PANEL_FIFO=1"); //fix for fifo not working bug
    server_t server;
    server_start(&server, argv[1], DEFAULT_PERMS);

    while(1){ //continues until interupted by a signal 
        signal(SIGINT, handleSig);
        signal(SIGTERM, handleSig);
        if(interrupted){ //breaks out of loop if a signal is caught
            break;
        }

        int ret = server_check_sources(&server); 
        if(ret == -1) //if CTRL-c or CTRL-d was pressed while the server was polling, the program still terminates gracefully
            break;
        if(server_join_ready(&server))
            server_handle_join(&server);

        for(int i = 0; i < server.n_clients; i++){ 
            if(server_client_ready(&server,i)) //checks each client's "data_ready" flag
                server_handle_client(&server,i);
        }
    }
    server_shutdown(&server);
    return 0;
}
