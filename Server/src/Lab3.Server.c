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
#include <unistd.h>

struct Packet{
	int flags;  //0 = nothing, 1 = ACK, 2 = SYNC + ACK, 3 = SYNC, 4 = NAK, 5 = FIN
	int seq;
	int id;
	int windowSize;
	int data;
};

struct sockaddr_in initSocket(int * _socket, int port);

void recvFromClient(int _socket, struct sockaddr_in *_client, struct Packet *_packet);
void sendToClient(int _socket, struct sockaddr_in _client, struct Packet _packet);

struct sockaddr_in connection(int _socket, struct Packet *_handshake);

void goBackN(int _socket,  struct sockaddr_in _client, int _seqMax);

int main(int argc, char *argv[]) {
	puts("!!!Hello World!!!"); /* prints !!!Hello World!!! */

	int _socket;
	struct sockaddr_in _server;
	struct Packet _handshake = {-1, -1, -1, -1, -1}; //Junk values

	if(argc < 2){
	    perror("ERROR, Not enough arguments\n");
	    exit(EXIT_FAILURE);
	}

	_server = initSocket(&_socket, atoi(argv[1]));

	connection(_socket, &_handshake);
	printf("-----------------\n");
	printf("Connected\n");
	printf("-----------------\n");
	goBackN(_socket, _server, (_handshake.windowSize * 2) + 1);
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

struct sockaddr_in connection(int _socket, struct Packet *_handshake){
	struct sockaddr_in _client;
	int _status = 0;
	int _active = 1;
	int _checker = 0;
	//Setup for timeouts
	fd_set _fdSet;
	struct timeval _timeout;
	FD_ZERO(&_fdSet);
	FD_SET(_socket, &_fdSet);

	//TODO: remove : 0 = nothing, 1 = ACK, 2 = SYNC + ACK, 3 = SYNC, 4 = NAK, 5 = FIN
	while(_active){
		switch (_status){
			case 0:
				//wait for SYNC
				recvFromClient(_socket, &_client, _handshake);
				printf("recv SYNC %d data %d\n", _handshake->flags, _handshake->data);
				_status++;
				break;
			case 1:
				//Send SYNC + ACK
				_handshake->flags = 2;
				_handshake->id = 10;
				sendToClient(_socket, _client, *_handshake);
				printf("send to client SYNC + ACK %d id = %d\n", _handshake->flags, _handshake->id);
				_status++;
				break;
			case 2:
				//start timer N wait for ACK
				FD_ZERO(&_fdSet);
				FD_SET(_socket, &_fdSet);
				_timeout.tv_sec = 5;
				_timeout.tv_usec = 0;
				_checker = select(FD_SETSIZE, &_fdSet, NULL, NULL, &_timeout);
				if(_checker <= 0){
					printf("_checker = %d\n", _checker);
					printf("TIMEOUT: resending SYNC + ACK...\n");
					_status = 1;
				} else {
					recvFromClient(_socket, &_client, _handshake);
					printf("recv ACK %d\n", _handshake->flags);
					_status++;
					_active = 0;
				}

				break;
		}
	}
	return _client;
}

void goBackN(int _socket, struct sockaddr_in _client, int _seqMax){
	int _expectedSeq = 0, _send = 1;

	//Setup for timeouts
	fd_set _fdSet;
	struct timeval _timeout;
	FD_ZERO(&_fdSet);
	FD_SET(_socket, &_fdSet);

	struct Packet _frame = {-1, -1, -1, -1, -1};  //Junk
	while(_send){
		sleep(1);
		recvFromClient(_socket, &_client, &_frame);
		if(_frame.seq != _expectedSeq){
			//Wrong seq
			printf("SENDING NAK: Wrong seq %d (expected %d) data %d\n", _frame.seq, _expectedSeq, _frame.data);
			_frame.flags = 4;
			sendToClient(_socket, _client, _frame);
			printf("-----------------\n");
		} else if(_frame.seq == _expectedSeq){
			printf("SENDING ACK: Recv data %d, seq %d\n", _frame.data, _frame.seq);
			_frame.flags = 1;
			_frame.seq = _expectedSeq;
			sendToClient(_socket, _client, _frame);
			_expectedSeq = (_expectedSeq + 1) % _seqMax;
		}
		printf("-----------------\n");
	}
}

void sendToClient(int _socket, struct sockaddr_in _client, struct Packet _packet){
	int _checker;

	_checker = sendto(_socket, (struct Packet *)&_packet, sizeof(_packet), 0, (struct sockaddr *) &_client, sizeof(_client));
	if(_checker < 0){
	    perror("ERROR, Could not send\n");
	    exit(EXIT_FAILURE);
	}
}

void recvFromClient(int _socket, struct sockaddr_in *_client, struct Packet *_packet){
	int _checker;
	int _len = sizeof(*_client);

	_checker = recvfrom(_socket, (struct Packet *)_packet, sizeof(*_packet), 0, (struct sockaddr *) _client, &_len);
	if(_checker < 0){
	    perror("ERROR, Could not recv\n");
	    exit(EXIT_FAILURE);
	}
}
