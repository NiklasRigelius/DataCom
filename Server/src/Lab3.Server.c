/*
 ============================================================================
 Name        : Server.c
 Author      : Niklas Rigelius
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include<netinet/in.h>

struct Packet{
	int flags;
	int seq;
	int id;
	int windowSize;
	int data;
};

struct sockaddr_in initSocket(int * _socket, int port);

void recvFromClient(int _socket, struct sockaddr_in *_client, struct Packet *_packet);
void sendToClient(int _socket, struct sockaddr_in _client, struct Packet _packet);

int main(int argc, char *argv[]) {
	puts("!!!Hello World!!!"); /* prints !!!Hello World!!! */

	int _socket;
	struct sockaddr_in _server;

	if(argc < 2){
	    perror("ERROR, Not enough arguments\n");
	    exit(EXIT_FAILURE);
	}

	_server = initSocket(&_socket, atoi(argv[1]));
	printf("%d\n", _socket);
	struct sockaddr_in _client;
	struct Packet _packet = {-1, -1, -1, -1, -1};
	recvFromClient(_socket, &_client, &_packet);
	_packet.data = 2;
	sendToClient(_socket, _client, _packet);


	return EXIT_SUCCESS;
}

struct sockaddr_in initSocket(int *_socket, int port){
	struct sockaddr_in _server;
	//Create socket
	*_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if(*_socket < 0){
	    perror("ERROR, could not create socket\n");
	    exit(EXIT_FAILURE);
	}
	memset(&_server, 0, sizeof(_server));	//Set _server to have 0
	//Fill server info
	_server.sin_family = AF_INET; //IPv4
	_server.sin_addr.s_addr = INADDR_ANY; //Get machine IP address
	_server.sin_port = htons(port); //Convert to network format
	if(bind(*_socket, (struct sockaddr *) &_server, sizeof(_server)) < 0){
	    perror("ERROR, could not bind\n");
	    exit(EXIT_FAILURE);
	}
	return _server;
}

void sendToClient(int _socket, struct sockaddr_in _client, struct Packet _packet){
	int _checker;

	_checker = sendto(_socket, (struct Packet *)&_packet, sizeof(_packet), 0, (struct sockaddr *) &_client, sizeof(_client));
	if(_checker < 0){
	    perror("ERROR, Could not send\n");
	    exit(EXIT_FAILURE);
	}
	printf("Sent data %d\n", _packet.data);
}

void recvFromClient(int _socket, struct sockaddr_in *_client, struct Packet *_packet){
	int _checker;
	int _len = sizeof(*_client);

	_checker = recvfrom(_socket, (struct Packet *)_packet, sizeof(*_packet), 0, (struct sockaddr *) _client, &_len);
	if(_checker < 0){
	    perror("ERROR, Could not recv\n");
	    exit(EXIT_FAILURE);
	}
	printf("Recv data %d\n", _packet->data);
}
