#include "blather.h"
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
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
#include <unistd.h>

char *fifo_name;
int p[2];
void handle(int sig);
int interuppted;

client_t *server_get_client(server_t *server, int idx){
// Gets a pointer to the client_t struct at the given index. If the
// index is beyond n_clients, the behavior of the function is
// unspecified and may cause a program crash.

    client_t *client = &server->client[idx];
    return client;
}

void server_start(server_t *server, char *server_name, int perms){
// Initializes and starts the server with the given name. A join fifo
// called "server_name.fifo" should be created. Removes any existing
// file of that name prior to creation. Opens the FIFO and stores its
// file descriptor in join_fd.
    log_printf("BEGIN: server_start()\n");

    strncpy(server->server_name, server_name, MAXPATH); //initializes server_t structure
    int i = 0;
    while(server->server_name[i] != 0){
        i++;
    }
    char fifoName[i+6];
    strncpy(fifoName, server_name, i);
    strncpy(fifoName+i, ".fifo\0", 6);

    remove(fifoName); //removes old fifo if one exists with that name
    mkfifo(fifoName, perms); //makes a new fifo with the '.fifo' suffix
    int fd = open(fifoName, O_RDONLY | O_NONBLOCK); //Doesn't block the join fifo because some intital log output (without clients) is required in the testing
    if(fd == -1){ //failed to make a the join fifo
        exit(0);
    }
    server->join_fd = fd; //initializes the rest of the server_t fields
    server->n_clients = 0;
    server->join_ready = 0;
    fifo_name = fifoName; //stores the name in a global variable for ease of access later on

    log_printf("END: server_start()\n");
}

void server_shutdown(server_t *server){
// Shut down the server. Close the join FIFO and unlink (remove) it so
// that no further clients can join. Send a BL_SHUTDOWN message to all
// clients and proceed to remove all clients in any order.

    log_printf("BEGIN: server_shutdown()\n");

    close(server->join_fd); 
    unlink(server->server_name); //closes and removes the join fifo
    mesg_t mesg;
    mesg.kind = BL_SHUTDOWN;
    server_broadcast(server, &mesg); //tells all the clients to shutdown

    log_printf("END: server_shutdown()\n");

}

int server_add_client(server_t *server, join_t *join){
// Adds a client to the server according to the parameter join which
// should have fileds such as name filed in.  The client data is
// copied into the client[] array and file descriptors are opened for
// its to-server and to-client FIFOs. Initializes the data_ready field
// for the client to 0. Returns 0 on success and non-zero if the
// server as no space for clients (n_clients == MAXCLIENTS).
//
    log_printf("BEGIN: server_add_client()\n");

    if(server->n_clients ==  MAXCLIENTS) //bounds check to make sure there's room to add another client
        return -1;

    client_t client;
    strncpy(client.name, join->name, MAXPATH); //uses strncpy to prevent buffer overruns
    client.to_client_fd =  open(join->to_client_fname, O_WRONLY); //O_NONBLOCK isn't required since the client should already have opened it's end of the fifo
    client.to_server_fd =  open(join->to_server_fname, O_RDONLY); //the server need only read from the to_server fifo and write to the to_client fifo
    strncpy(client.to_client_fname, join->to_client_fname, MAXPATH);
    strncpy(client.to_server_fname, join->to_server_fname, MAXPATH);

    client.data_ready = 0;
    server->client[server->n_clients] = client; //adds the new client to the end of the array
    server->n_clients = server->n_clients + 1; //increments the number of clients 

    log_printf("END: server_add_client()\n");
    return 0;
}

int server_remove_client(server_t *server, int idx){
// Remove the given client likely due to its having departed or
// disconnected. Close fifos associated with the client and remove
// them.  Shift the remaining clients to lower indices of the client[]
// preserving their order in the array; decreases n_clients.

    client_t *client = server_get_client(server, idx); //uses server_get_client for readability
    close(client->to_client_fd); //closes to_client and from_client fifos
    close(client->to_server_fd);
    unlink(client->to_client_fname);
    unlink(client->to_server_fname);

    int n = --server->n_clients;
    for(int i = idx; i < n; i++){ //shifts all clients higher then idx down, preserving their order
        server->client[i] = server->client[i + 1];
    }
    return 0;
}

int server_broadcast(server_t *server, mesg_t *mesg){
// Send the given message to all clients connected to the server by
// writing it to the file descriptors associated with them.
    for(int i = 0; i < server->n_clients; i++){
        write(server->client[i].to_client_fd, mesg, sizeof(mesg_t));
    }
    return 0;
}


int server_check_sources(server_t *server){
// Checks all sources of data for the server to determine if any are
// ready for reading. Sets the servers join_ready flag and the
// data_ready flags of each of client if data is ready for them.
// Makes use of the poll() system call to efficiently determine
// which sources are ready.
//
    log_printf("BEGIN: server_check_sources()\n");

    int n = server->n_clients; //stores number of clients in variable for readability

    interuppted = 0; //variable used to check for signal interupts
    log_printf("poll()'ing to check %d input sources\n",n+1);

    int dataReady = 0; //loop terminates if there is something to be done
    int ret = 1;

    while(!dataReady && !interuppted){
        signal(SIGINT, handle);
        signal(SIGTERM, handle);
        struct pollfd pollStruct = {server->join_fd, POLLIN}; //checks only the join fifo
        if (poll(&pollStruct, 1, 0) == 1) { //uses poll to check for data in the join fifo (returns immediately, which is why the while loop is necessary)
            server->join_ready = 1;
            dataReady = 1; //doesn't break out of the loop just yet because we want to check if clients have data to send, as well
        }
        for(int i = 0; i < n; i++){
            struct pollfd polls = {server->client[i].to_server_fd, POLLIN}; //uses poll to check for data in the each of the to_server fifos, checks one at a time
            if (poll(&polls, 1, 0) == 1) { 
                server->client[i].data_ready = 1;
                dataReady = 1; 
            }
        }
        if(interuppted){ //clean way to break from the loop if CTRL-c or CTRL-d are pressed
            ret = -1;
            break;
        }
    }
    log_printf("poll() completed with return value %d\n",ret);
    if(ret == -1){
        log_printf("poll() interrupted by a signal\n");
    }
    else{
        log_printf("join_ready = %d\n", server->join_ready);
        for(int i = 0; i < n; i++){
            log_printf("client %d '%s' data_ready = %d\n",i, server->client[i].name, server->client[i].data_ready);
        }
    }
    log_printf("END: server_check_sources()\n");
    return ret; //lets bl_server know if it should terminate or not 
}

int server_join_ready(server_t *server){
// Return the join_ready flag from the server which indicates whether
// a call to server_handle_join() is safe.
    return server->join_ready;
}

int server_handle_join(server_t *server){
// Call this function only if server_join_ready() returns true. Read a
// join request and add the new client to the server. After finishing,
// set the servers join_ready flag to 0.
//
    log_printf("BEGIN: server_handle_join()\n");

    join_t join;
    read(server->join_fd, &join, sizeof(join_t));

    log_printf("join request for new client '%s'\n",join.name);

    server_add_client(server, &join);
    server->join_ready = 0;

    mesg_t newUser; //message to store a JOINED message (e.g. -- Bruce JOINED --)
    newUser.kind = BL_JOINED;
    strncpy(newUser.name, join.name, MAXNAME);
    server_broadcast(server, &newUser); //broadcasts a JOINED message to every client in the server

    log_printf("END: server_handle_join()\n");
    return 0;
}

int server_client_ready(server_t *server, int idx){
// Return the data_ready field of the given client which indicates
// whether the client has data ready to be read from it.
    return server->client[idx].data_ready;
}

int server_handle_client(server_t *server, int idx){
// Process a message from the specified client. This function should
// only be called if server_client_ready() returns true. Read a
// message from to_server_fd and analyze the message kind. Departure
// and Message types should be broadcast to all other clients.  Ping
// responses should only change the last_contact_time below. Behavior
// for other message types is not specified. Clear the client's
// data_ready flag so it has value 0.


    log_printf("BEGIN: server_handle_client()\n");

    mesg_t mesg;
    read(server->client[idx].to_server_fd, &mesg, sizeof(mesg_t));
    server_broadcast(server,&mesg); //broadcasts all messages (not all messages trigger a client response, however)

    char *name = mesg.name;

    if(mesg.kind == BL_DEPARTED){
        log_printf("client %d '%s' DEPARTED\n", idx, name);
        server_remove_client(server, idx); //removes client if it tells the server that it's departing
    }
    if(mesg.kind == BL_MESG)
        log_printf("client %d '%s' MESSAGE '%s'\n", idx, name, mesg.body);

    server->client[idx].data_ready = 0; //clears the client's data ready flag

    log_printf("END: server_handle_client()\n");
    return 0;
}

void handle(int sig){
    interuppted = 1;
}
