#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>
#include "blather.h"

//lots of global variables so that the two threads can access them easily
simpio_t *simpio;
server_t *server;
char *to_serverName;
char *to_clientName;
int to_server_fd;
int to_client_fd;
char *name;
pthread_t user_thread;
pthread_t server_thread;

//pthread functions
void *user_worker(void *arg);
void *server_worker(void *arg);

int interuppted = 0; 
void sigHandle(int sig); //signal handler identical to the ones in bl_server and server_funcs

int main(int argc, char *argv[]){
    char clientName[MAXPATH];
    strncpy(clientName, argv[2], MAXPATH);
    name = clientName; //sets global pointer
    simpio = malloc(sizeof(simpio_t)); //couldn't find a way to avoid malloc without getting a seg fault
    char prompt[MAXNAME];
    snprintf(prompt, MAXNAME, "%s>> ",name); // create a prompt string
    simpio_set_prompt(simpio, prompt);         // set the prompt
    simpio_reset(simpio);                      // initialize io
    simpio_noncanonical_terminal_mode();       // set the terminal into a compatible mode

    int pid = (int) getpid(); //to_client and to_server fifos use PIDs to ensure unique names
    char to_server_name[MAXPATH];
    char to_client_name[MAXPATH];
    char *n1 = "toServer"; //to_server fifo looks like "toServer5267.fifo" for a process with pid 5267
    char *n2 = "toClient"; //to_client fifo looks like "toClient5267.fifo" for a process with pid 5267
    char n3[MAXPATH];
    int num = sprintf(n3,"%d", pid);
    strncpy(to_server_name, n1, 8);
    strncpy(to_client_name, n2, 8);
    strncpy(to_server_name+8, n3, num);
    strncpy(to_client_name+8, n3, num);
    strncpy(to_server_name+8+num, ".fifo\0", 6);
    strncpy(to_client_name+8+num, ".fifo\0", 6);
    mkfifo(to_server_name, DEFAULT_PERMS);
    mkfifo(to_client_name, DEFAULT_PERMS);

    to_clientName = to_client_name; //sets global variables to match local ones
    to_serverName = to_server_name;

    join_t join;
    strncpy(join.name, name, MAXPATH);
    strncpy(join.to_client_fname, to_client_name, MAXPATH);
    strncpy(join.to_server_fname, to_server_name, MAXPATH);

    char serverName[MAXPATH];
    strncpy(serverName, argv[1], MAXPATH);
    int i = 0;
    while(serverName[i] != 0){ //gets length of 1st argument (name of the server)
        i++;
    }
    char fifoName[i+6];
    strncpy(fifoName, serverName, i);
    strncpy(fifoName+i, ".fifo\0",6); //constructs name of join fifo based on the server name the user gives it

    int fd = open(fifoName, O_WRONLY); //O_NONBLOCK not required since (if user entered correctly) the Read end should already have been opened by bl_server
    write(fd, &join, sizeof(join_t)); //write the join request to the join fifo

    pthread_create(&server_thread, NULL, server_worker, NULL); //create a thread to read from the to_client fifo
    pthread_create(&user_thread, NULL, user_worker, NULL); //create a thread to write to the to_server fifo
    pthread_join(user_thread, NULL); //wait for the threads to finish
    pthread_join(server_thread, NULL);
    simpio_reset_terminal_mode(); //reset the terminal mode
    free(simpio); //free the simpio_t struct 
    pthread_cancel(server_thread); //cancels server thread before returning
    return 0;
}

void *user_worker(void *arg){
    to_server_fd = open(to_serverName, O_WRONLY); //blocks processing of the thread until the server adds the client and opens its end 
    while(!simpio->end_of_input || interuppted == 0){
        signal(SIGINT, sigHandle);
        signal(SIGTERM, sigHandle);
        simpio_reset(simpio); //resets after each message
        iprintf(simpio, "");
        int count = 0;
        while(!simpio->line_ready && !simpio->end_of_input){
            simpio_get_char(simpio);
            count++;
        }
        if(simpio->line_ready){
            mesg_t mesg;
            mesg.kind = BL_MESG; //all typed input is of BL_MESSAGE type
            strncpy(mesg.name, name, MAXPATH); //why declaring the client name globally was useful
            strncpy(mesg.body, simpio->buf, MAXLINE);
            write(to_server_fd, &mesg, sizeof(mesg));
        }
    }
    mesg_t depart = {BL_DEPARTED}; //writes a departed message if interupted by a signal
    strncpy(depart.name, name, MAXPATH);
    write(to_server_fd, &depart, sizeof(depart));
    pthread_cancel(user_thread);
    return NULL;
}

void *server_worker(void *arg){
    to_client_fd = open(to_clientName, O_RDONLY);
    mesg_t mesg; //keeps overwriting to the same mesg_t struct to save space
    while(!interuppted){
        signal(SIGINT, sigHandle);
        signal(SIGTERM, sigHandle);
        int nread = read(to_client_fd, &mesg, sizeof(mesg_t));
        if(mesg.kind == BL_SHUTDOWN || interuppted == 1){ //breaks out of the loop if instructed to shut down or interupted by signal
            iprintf(simpio, "!!! %s !!!\n", "server is shutting down");
            interuppted = 1;
            break;
        }
        else if(mesg.kind == BL_JOINED){
            iprintf(simpio, "-- %s JOINED --\n", mesg.name);
        }
        else if (nread == sizeof(mesg_t)){
            iprintf(simpio, "[%s] : %s\n", mesg.name, mesg.body); //preserves prompt
        }
    }
    return NULL;
}

void sigHandle(int sig){interuppted = 1;}