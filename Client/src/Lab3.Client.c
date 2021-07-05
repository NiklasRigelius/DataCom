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
#include <sys/select.h>

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

void connection(int _socket, struct sockaddr_in _server, struct Packet *_handshake);

int * userInteraction(struct Packet *_handshake, int * _nrOfData);

void goBackN(int _socket, struct sockaddr_in _server, struct Packet _header, int * _data, int _nrOfData);

int main(int argc, char *argv[]) {
	puts("!!!Hello World 2!!!"); /* prints !!!Hello World!!! */

	int _socket;
	struct sockaddr_in _server;
	struct Packet _handshake = {-1, -1, -1, -1, -1}; //JUNK values
	int * _data; //TODO: make sure its cleared
	int _nrOfData = 0;

	if(argc != 3){
	    perror("ERROR, not enough arguments\n");
	    exit(EXIT_FAILURE);
	}

	_server = initSocket(&_socket, argv);

	_data = userInteraction(&_handshake, &_nrOfData);

	connection(_socket, _server, &_handshake);

	printf("Connected to the server\n");

	goBackN(_socket, _server, _handshake, _data, _nrOfData);


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

void connection(int _socket, struct sockaddr_in _server, struct Packet *_handshake){
	//TODO: remove : 0 = nothing, 1 = ACK, 2 = SYNC + ACK, 3 = SYNC, 4 = NAK, 5 = FIN
	//Setup for timeouts
	fd_set _fdSet;
	struct timeval _timeout;
	FD_ZERO(&_fdSet);
	FD_SET(_socket, &_fdSet);


	int _status = 0;
	int _active = 1;
	int _checker = 0;
	while(_active){
		switch (_status){
			case 0:
				//send SYNC
				_handshake->flags = 1;
				sendToServer(_socket, _server, *_handshake);
				printf("sent SYNC %d\n", _handshake->flags);
				_status++;
				break;
			case 1:
				//start timer and wait for SYNC + ACK
				//Timer
				FD_ZERO(&_fdSet);
				FD_SET(_socket, &_fdSet);
				_timeout.tv_sec = 5;
				_timeout.tv_usec = 0;
				_checker = select(FD_SETSIZE, &_fdSet, NULL, NULL, &_timeout);
				if(_checker <= 0){
					printf("_checker = %d\n", _checker);
					printf("TIMEOUT: resending ACK...\n");
					_status = 0;
				} else {
					recvFromServer(_socket, _server, _handshake);
					printf("recv SYNC + ACK %d  id  %d\n", _handshake->flags, _handshake->id);
					_status ++;
				}
				break;
			case 2:
				//Send SYNC
				_handshake->flags = 3;
				sendToServer(_socket, _server, *_handshake);
				printf("sent ACK %d\n", _handshake->flags);
				_status++;
				_active = 0;
				break;
		}
	}
}

int * userInteraction(struct Packet *_handshake, int * _nrOfData){
	int _validator = 1;
	//Window Size
	while(_validator){
		printf("Enter window size: \n");
		scanf("%d", &(*_handshake).windowSize);
		if(_handshake->windowSize > 0) _validator = 0;
	}
	_validator = 1;
	//Fill data
	while(_validator){
		printf("Enter number of data (more than 0): \n");
		scanf("%d", _nrOfData);
		if(*_nrOfData > 0) _validator = 0;
	}
	int * _data = (int *)malloc(sizeof(int) * (*_nrOfData));
	if(_data == NULL){
		printf("ERROR, memory could not be allocated");
		exit(EXIT_FAILURE);
	}
	printf("Enter data in integers: \n");
	for(int i = 0; i < *_nrOfData; i++){
		scanf("%d", &_data[i]);
	}
	//TODO: CRS stuff

	return _data;
}

void goBackN(int _socket, struct sockaddr_in _server, struct Packet _header, int * _data, int _nrOfData){
	int _send = 1, _start = 0, _end = 0, _currentSeq = 0, _expectedSeq = 0;
	int _windowsAvailable = _header.windowSize;
	int _checker =0 ;
	int _seqMax = (_header.windowSize * 2) + 1;

	//Setup for timeouts
	fd_set _fdSet;
	struct timeval _timeout;
	FD_ZERO(&_fdSet);
	FD_SET(_socket, &_fdSet);

	struct Packet _frame = _header;
	while(_send){
		//If there is free space in window -> send frame
		if(_windowsAvailable > 0){
			_frame.flags = 0;
			_frame.seq = _currentSeq % _seqMax;
			_frame.data = _data[_end];
			printf("Sending frame seq %d data %d\n", _frame.seq, _frame.data);
			sendToServer(_socket, _server, _frame);
			_windowsAvailable --;
			_currentSeq ++;
			_end++;
		}

		//Listen for ACKs briefly
		_timeout.tv_sec = 0;
		_timeout.tv_usec = 1;
		_checker = select(FD_SETSIZE, &_fdSet, NULL, NULL, &_timeout);
		if(_checker < 0){
			//ERROR
		} else if(_checker > 0){
			recvFromServer(_socket, _server, &_frame);
			if(_frame.flags == 1){ //move window to the "right"
				_start++;
				_windowsAvailable++;
				_expectedSeq = (_expectedSeq + 1) % _seqMax;
			} else{ //resend window
				_end = _start;
				_windowsAvailable = _header.windowSize;
				_currentSeq = _expectedSeq;
			}
		}

		//If _windowsAvailable is 0 then wait for 3 sec and resend if no ack is recv...
		if(_windowsAvailable == 0){	//Wait for ACK's for 3sec then timeout -> resend window
			_timeout.tv_sec = 3;
			_timeout.tv_usec = 0;
			_checker = select(FD_SETSIZE, &_fdSet, NULL, NULL, &_timeout);
			if(_checker < 0){
				//Error
			} else if(_checker == 0){
				//No msg -> resend
				_end = _start;
				_windowsAvailable = _header.windowSize;
				_currentSeq = _expectedSeq;
			} else {
				//recv msg -> free space in window
				_start++;
				_windowsAvailable++;
				_expectedSeq = (_expectedSeq + 1) % _seqMax;
			}
		}
	}
}


void sendToServer(int _socket, struct sockaddr_in _server, struct Packet _packet){
	int _checker;

	_checker = sendto(_socket, (struct Packet *)&_packet, sizeof(_packet), 0, (struct sockaddr *) &_server, sizeof(_server));

	if(_checker < 0){
	    perror("ERROR, Could not send\n");
	    exit(EXIT_FAILURE);
	}
}

void recvFromServer(int _socket, struct sockaddr_in _server, struct Packet *_packet){
	int _checker;
	int _len = sizeof(_server);

	_checker = recvfrom(_socket, (struct Packet *)_packet, sizeof(*_packet), 0, (struct sockaddr *) &_server, &_len);
	if(_checker < 0){
	    perror("ERROR, Could not recv\n");
	    exit(EXIT_FAILURE);
	}
}
