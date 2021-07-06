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
#include <unistd.h>

#define msgLength 256

struct Packet{
	int flags;
	int seq;
	int id;
	int windowSize;
	int integerData;
	char data [msgLength];
};

struct sockaddr_in initSocket(int *_socket, char * argv[]);

void sendToServer(int _socket, struct sockaddr_in _server, struct Packet _packet);
void recvFromServer(int _socket, struct sockaddr_in _server, struct Packet *_packet);

void connection(int _socket, struct sockaddr_in _server, struct Packet *_handshake);

int * userInteraction(struct Packet *_handshake, int * _nrOfintegerData);

void goBackN(int _socket, struct sockaddr_in _server, struct Packet _header, int * _integerData, int _nrOfintegerData);
int waitForResponse(int _socket, struct sockaddr_in _server, int _sec, int _usec, int _expectedSeq);

int main(int argc, char *argv[]) {
	puts("!!!Hello World 2!!!"); /* prints !!!Hello World!!! */

	int _socket;
	struct sockaddr_in _server;
	struct Packet _handshake = {-1, -1, -1, -1, -1}; //JUNK values
	int * _integerData; //TODO: make sure its cleared
	int _nrOfintegerData = 0;

	if(argc != 3){
	    perror("ERROR, not enough arguments\n");
	    exit(EXIT_FAILURE);
	}

	_server = initSocket(&_socket, argv);

	_integerData = userInteraction(&_handshake, &_nrOfintegerData);

	connection(_socket, _server, &_handshake);
	printf("--------------------------------\n");
	printf("Connected to the server\n");
	printf("--------------------------------\n");
	goBackN(_socket, _server, _handshake, _integerData, _nrOfintegerData);


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

int * userInteraction(struct Packet *_handshake, int * _nrOfintegerData){
	int _validator = 1;
	//Window Size
	while(_validator){
		printf("Enter window size: \n");
		scanf("%d", &(*_handshake).windowSize);
		if(_handshake->windowSize > 0) _validator = 0;
	}

	//Enter messages
	_validator = 1;
	int _nrOfMsg = 0;
	while(_validator){
		printf("Enter number of messages to send (more than 0): \n");
		scanf("%d", &_nrOfMsg);
		if(_nrOfMsg > 0) _validator = 0;
	}
	char msg[_nrOfMsg][msgLength];
	printf("Write your messages : \n");

	for (int i = 0; i < _nrOfMsg; i++){
		printf("Message %d : ", i + 1);
		fgets(msg[i], msgLength, stdin);
		msg[i][msgLength - 1] = '\0';
	}

	printf("This is your messages : \n");
	for (int i = 0; i < _nrOfMsg; i++){
		printf("Msg %d : %s\n", i + 1, msg[i]);
	}

	//Fill integerData
	_validator = 1;
	while(_validator){
		printf("Enter number of integerData (more than 0): \n");
		scanf("%d", _nrOfintegerData);
		if(*_nrOfintegerData > 0) _validator = 0;
	}
	int * _integerData = (int *)malloc(sizeof(int) * (*_nrOfintegerData));
	if(_integerData == NULL){
		printf("ERROR, memory could not be allocated");
		exit(EXIT_FAILURE);
	}
	printf("Enter integerData in integers: \n");
	for(int i = 0; i < *_nrOfintegerData; i++){
		scanf("%d", &_integerData[i]);
	}
	//TODO: CRS stuff

	return _integerData;
}

void goBackN(int _socket, struct sockaddr_in _server, struct Packet _header, int * _integerData, int _nrOfintegerData){
	int _start = 0, _end = 0, _sentFrames = 0, _recvACK = 0;
	int _windowsAvailable = _header.windowSize;
	int _seqMax = (_header.windowSize * 2) + 1;

	struct Packet _frame = _header;

	while(!(_sentFrames == _nrOfintegerData && _recvACK == _nrOfintegerData)){
		//If there is free space in window -> send frame
		if(_windowsAvailable > 0 && _sentFrames != _nrOfintegerData){
			_frame.flags = 0;
			_frame.seq = _end % _seqMax;
			_frame.integerData = _integerData[_end];
			sendToServer(_socket, _server, _frame);
			_windowsAvailable --;
			_end++;
			_sentFrames++;
			printf("Sending frame seq %d integerData %d\n", _frame.seq, _frame.integerData);
			printf("Windows available %d\n", _windowsAvailable);
			printf("------------------------------\n");
		}

		int _recvStatus;
		//Do a quick check to see if there is any incoming msg
		//we are only interesting in ACKs and NAKs here, if timeout is triggered just continue
		//The timer is set to 0 sec, 1 usec
		_recvStatus = waitForResponse(_socket, _server, 0, 1, (_start % _seqMax));
		if(_recvStatus == 1){  //recv an ACK
			_start++;
			_windowsAvailable++;
			_recvACK ++;
		}
		else if(_recvStatus == -2){ //recv an NAK
			_end = _start;
			_windowsAvailable = _header.windowSize;
			_sentFrames -= (_end - _start) + 1;
			_recvACK = _sentFrames;
		}

		//If there is no windows available we need to wait for ACK, NAK or timeout
		//Timer is set to 5 sec, if no response from server is available we resend the packets
		//We are therefore interested in if the timer is triggered
		if(_windowsAvailable == 0){
			_recvStatus = waitForResponse(_socket, _server, 5, 0, (_start % _seqMax));
			if(_recvStatus == 1){  //recv an ACK
				_start++;
				_windowsAvailable++;
				_recvACK ++;
			}
			else if(_recvStatus == -1 ||_recvStatus == -2){ //if timer is triggered or NAK was received
				_end = _start;
				_windowsAvailable = _header.windowSize;
				_sentFrames -= (_end - _start) + 1;
				_recvACK = _sentFrames;
			}
		}
	}
	printf("After while in GoBackN\n");
}
//returns 1 if ACK is recv
//returns -1 if timer is triggered
//return -2 if NAK is recv
int waitForResponse(int _socket, struct sockaddr_in _server, int _sec, int _usec, int _expectedSeq){
	//Setup for timeouts
	fd_set _fdSet;
	FD_ZERO(&_fdSet);
	FD_SET(_socket, &_fdSet);
	struct timeval _timeout;
	_timeout.tv_sec = _sec;
	_timeout.tv_usec = _usec;

	struct Packet _recv = {-1, -1, -1, -1,-1}; //Junk value;
	int _checker;

	_checker = select(FD_SETSIZE, &_fdSet, NULL, NULL, &_timeout);
	if(_checker < 0){
		//TODO: ERROR
	}
	else if(_checker == 0){ //timer triggered
		//No msg -> resend
		if(_usec == 0) printf("TIMEOUT \n");
		return -1;
	}
	else {
		//recv msg -> check if correct flag and seq
		recvFromServer(_socket, _server, &_recv);
		if(_recv.flags == 1 && _recv.seq == _expectedSeq){ //move window to the "right"
			printf("RECEIVED ACK: integerData %d, seq %d\n", _recv.integerData, _recv.seq);
			return 1;
		}
		else{ //resend window
			printf("RECEIVED NAK: integerData %d, seq %d\n", _recv.integerData, _recv.seq);
			return -2;
		}
	}
	return -1;
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
