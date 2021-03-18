CFLAGS = -Wall -g
CC     = gcc $(CFLAGS)

all : bl_server bl_client

util.o : util.c blather.h
	$(CC) -c util.c

simpio.o : simpio.c blather.h
	$(CC) -c simpio.c

server_funcs.o : server_funcs.c blather.h 
	$(CC) -c server_funcs.c

bl_server.o : bl_server.c blather.h
	$(CC) -c bl_server.c

bl_client.o : bl_client.c blather.h 
	$(CC) -c bl_client.c

bl_server : bl_server.o server_funcs.o util.o
	$(CC) -o bl_server bl_server.o server_funcs.o util.o

bl_client : bl_client.o simpio.o 
	$(CC) -pthread -o bl_client bl_client.o simpio.o 

clean : 
	rm -f bl_server bl_client *.o *.fifo

include test_Makefile
