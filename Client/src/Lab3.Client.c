/*
 ============================================================================
 Name        : Client.c
 Author      : Niklas Rigelius
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

// gcc -g Lab3.Client.c -o client

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

struct Packet{
	int flags;
	int seq;
	int id;
	int windowSize;
	int data;
};

struct sockaddr_in initSocket(int *_socket, char * argv[]);

void sendToServer(int _socket, struct sockaddr_in _server, struct Packet _packet);
void recvFromServer(int _socket, struct sockaddr_in _server, struct Packet *_packet);


int main(int argc, char *argv[]) {
	puts("!!!Hello World 2!!!"); /* prints !!!Hello World!!! */

	int _socket;
	struct sockaddr_in _server;

	if(argc != 3){
	    perror("ERROR, not enough arguments\n");
	    exit(EXIT_FAILURE);
	}

	_server = initSocket(&_socket, argv);

	struct Packet _test = {-1, -1, -1, 0};
	printf("%d\n", _socket);
	sendToServer(_socket, _server, _test);

	recvFromServer(_socket, _server, &_test);


	return EXIT_SUCCESS;
}

struct sockaddr_in initSocket(int *_socket, char * argv[]){
	struct sockaddr_in _server;
	struct hostent *_hp;
	//Create socket
	*_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if(*_socket < 0){
	    perror("ERROR, could not create socket\n");
	    exit(EXIT_FAILURE);
	}
	memset(&_server, 0, sizeof(_server));	//Set _server to have 0

	//Fill server info
	_server.sin_family = AF_INET; //IPv4
	_hp = gethostbyname(argv[1]);
	if(_hp == 0){
	    perror("ERROR, Unknown host\n");
	    exit(EXIT_FAILURE);
	}


	bcopy((char *)_hp->h_addr, (char *)&_server.sin_addr, _hp->h_length); //Get machine IP address of server
	_server.sin_port = htons(atoi(argv[2])); //Convert to network format

	return _server;
}

void sendToServer(int _socket, struct sockaddr_in _server, struct Packet _packet){
	int _checker;

	_checker = sendto(_socket, (struct Packet *)&_packet, sizeof(_packet), 0, (struct sockaddr *) &_server, sizeof(_server));

	if(_checker < 0){
	    perror("ERROR, Could not send\n");
	    exit(EXIT_FAILURE);
	}
	printf("Sent data %d\n", _packet.data);
}

void recvFromServer(int _socket, struct sockaddr_in _server, struct Packet *_packet){
	int _checker;
	int _len = sizeof(_server);

	_checker = recvfrom(_socket, (struct Packet *)_packet, sizeof(*_packet), 0, (struct sockaddr *) &_server, &_len);
	if(_checker < 0){
	    perror("ERROR, Could not recv\n");
	    exit(EXIT_FAILURE);
	}
	printf("Recv data %d\n", _packet->data);
}
