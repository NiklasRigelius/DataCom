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
	int crc;
	char data [msgLength];
};

struct sockaddr_in initSocket(int *_socket, char * argv[]);

void sendToServer(int _socket, struct sockaddr_in _server, struct Packet _packet);
void recvFromServer(int _socket, struct sockaddr_in _server, struct Packet *_packet);

void connection(int _socket, struct sockaddr_in _server, struct Packet *_handshake);

char ** userInteraction(struct Packet *_handshake, int * _nrOfMsg);

void goBackN(int _socket, struct sockaddr_in _server, struct Packet _header, char ** _inputData, int _nrOfMsg);
int waitForResponse(int _socket, struct sockaddr_in _server, int _sec, int _usec, int _expectedSeq);

char ** multiDimAllocation(int _row);
void freeMultiDim(char ** _free, int _row);

int checkCRC(struct Packet _frame);
void sendFrame(int _socket, struct sockaddr_in _server, int _flag, int _id, int _seq, int _windowSize, char * _msg);
size_t buildCRCmsg(unsigned char * _CRCmsg, struct Packet _frame);
int calculateCRC16(unsigned char _msg [], int length );

int main(int argc, char *argv[]) {
	puts("!!!Hello World 2!!!"); /* prints !!!Hello World!!! */

	int _socket;
	struct sockaddr_in _server;
	struct Packet _handshake = {2, 2, 3, 1}; //JUNK values

	char ** _inputData; //TODO: make sure its cleared
	int _nrOfMsg = 0;

	if(argc != 3){
	    perror("ERROR, not enough arguments\n");
	    exit(EXIT_FAILURE);
	}

	_server = initSocket(&_socket, argv);

	_inputData = userInteraction(&_handshake, &_nrOfMsg);


	connection(_socket, _server, &_handshake);

//	unsigned char _CRCmsg [sizeof(struct Packet)];
//	size_t _CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, _handshake);
//	_handshake.crc = calculateCRC16(_CRCmsg, _CRCmsgSize);
//
//	printf("crc = %d\n", _handshake.crc);
//	return EXIT_SUCCESS;

	printf("--------------------------------\n");
	printf("Connected to the server\n");
	printf("--------------------------------\n");
	goBackN(_socket, _server, _handshake, _inputData, _nrOfMsg );

	freeMultiDim(_inputData, _nrOfMsg);
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

char ** userInteraction(struct Packet *_handshake, int  * _nrOfMsg){
	int _validator = 1;
	//Window Size
	while(_validator){
		printf("Enter window size: \n");
		scanf("%d", &(*_handshake).windowSize);
		if(_handshake->windowSize > 0) _validator = 0;
	}

	//Enter messages
	_validator = 1;
	while(_validator){
		printf("Enter number of messages to send (more than 0): \n");
		scanf("%d", _nrOfMsg);
		if(*_nrOfMsg > 0) _validator = 0;
	}
	char ** _data = multiDimAllocation(*_nrOfMsg);
	printf("Write your messages : \n");
	while(getchar() != '\n');
	for (int i = 0; i < *_nrOfMsg; i++){
		printf("Message %d index %d: ", i + 1, i);
		fgets(_data[i], msgLength, stdin);
		_data[i][msgLength - 1] = '\0';
	}

	//TODO: CRS stuff

	return _data;
}

void goBackN(int _socket, struct sockaddr_in _server, struct Packet _header,char ** _inputData, int _nrOfMsg){
	int _start = 0, _end = 0, _sentFrames = 0, _recvACK = 0;
	int _windowsAvailable = _header.windowSize;
	int _seqMax = (_header.windowSize * 2) + 1;

	struct Packet _frame = _header;


	while(!(_sentFrames == _nrOfMsg && _recvACK == _nrOfMsg)){
		//If there is free space in window -> send frame
		if(_windowsAvailable > 0 && _sentFrames != _nrOfMsg){
			sendFrame(_socket, _server, 0, _header.id, (_end % _seqMax), _header.windowSize, _inputData[_end]);
			_windowsAvailable --; 	//One frame is "on the move", there is one less window available
			_end++;					//increase the _end variable to point to next data to be sent
			_sentFrames++; 			//increase the amount of frames sent
		}

		int _recvStatus;
		//Do a quick check to see if there is any incoming msg
		//we are only interesting in ACKs and NAKs here, if timeout is triggered just continue
		//The timer is set to 0 sec, 1 usec
		_recvStatus = waitForResponse(_socket, _server, 0, 1, (_start % _seqMax));
		if(_recvStatus == 1){  		//recv an ACK
			_start++;				//increase the _start variable to point to the next data needing a ACK
			_windowsAvailable++;	//Open up a slot for data to be sent
			_recvACK ++;			//count the recv ACKs
		}
		else if(_recvStatus == -2 || _recvStatus == -3){ //recv an NAK or corrupted packet TODO: Check if a corrupted packet should lead to windows being resent
			_end = _start;			//reset _end to point to the start of the window -> will make sure the window is resent
			_windowsAvailable = _header.windowSize;	//Make all slots in window available
			_sentFrames -= (_end - _start) + 1;		//Subtract subtract _sentFrames with the number of frames needed to be resent
			_recvACK = _sentFrames;					//Subtract subtract _recvAck with the number of frames needed to be resent
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
			else if(_recvStatus == -1 ||_recvStatus == -2 || _recvStatus == -3){ //if timer is triggered or NAK was received
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
			if(checkCRC(_recv) == 1){ //Packet not corrupted -> return 1

				printf("RECEIVED ACK: seq %d data %s \n", _recv.seq, _recv.data);
				return 1;
			} else{ //Packet corrupted -> return -3;
				printf("RECEIVED CORRUPTED: Packet was corrupted\n");
				return -3;
			}

		}
		else{ //resend window
			printf("RECEIVED NAK: seq %d\n", _recv.seq);
			return -2;
		}
	}
	return -1;
}

void sendToServer(int _socket, struct sockaddr_in _server, struct Packet _packet){
	int _checker;

	_checker = sendto(_socket, (struct Packet *)&_packet, (1024 + sizeof(_packet)), 0, (struct sockaddr *) &_server, sizeof(_server));

	if(_checker < 0){
	    perror("ERROR, Could not send\n");
	    exit(EXIT_FAILURE);
	}
}

void recvFromServer(int _socket, struct sockaddr_in _server, struct Packet *_packet){
	int _checker;
	int _len = sizeof(_server);

	_checker = recvfrom(_socket, (struct Packet *)_packet, (sizeof(*_packet)), 0, (struct sockaddr *) &_server, &_len);
	if(_checker < 0){
	    perror("ERROR, Could not recv\n");
	    exit(EXIT_FAILURE);
	}
}

int checkCRC(struct Packet _frame){

	int _calculatedCRC;
	unsigned char _CRCmsg [sizeof(struct Packet)];
	size_t _CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, _frame);
	_calculatedCRC = calculateCRC16(_CRCmsg, _CRCmsgSize);

	if(_calculatedCRC == _frame.crc) return 1;
	return 0;
}

void sendFrame(int _socket, struct sockaddr_in _server, int _flag, int _id, int _seq, int _windowSize, char * _msg){
	struct Packet _frame;
	_frame.flags = _flag;
	_frame.id = _id;
	_frame.seq = _seq;
	_frame.windowSize = _windowSize;
	strcpy(_frame.data, _msg);

	unsigned char _CRCmsg [sizeof(struct Packet)];
	size_t _CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, _frame);
	_frame.crc = calculateCRC16(_CRCmsg, _CRCmsgSize);

	sendToServer(_socket, _server, _frame);
}

size_t buildCRCmsg(unsigned char * _CRCmsg, struct Packet _frame){
	//Add header information and Data together to be a part of the crc calculation
	//return the size of the unisgned char and the unsigned char containing the information
	size_t _retSize = 0;

	memcpy(&_CRCmsg[_retSize], &_frame.flags, sizeof(_frame.flags));
	_retSize += sizeof(_frame.flags);
	memcpy(&_CRCmsg[_retSize], &_frame.seq, sizeof(_frame.seq));
	_retSize += sizeof(_frame.seq);
	memcpy(&_CRCmsg[_retSize], &_frame.id, sizeof(_frame.id));
	_retSize += sizeof(_frame.seq);
	memcpy(&_CRCmsg[_retSize], &_frame.windowSize, sizeof(_frame.windowSize));
	_retSize += sizeof(_frame.seq);
	memcpy(&_CRCmsg[_retSize], &_frame.data, sizeof(_frame.data));
	_retSize += sizeof(_frame.seq);

	return _retSize;
}

int calculateCRC16(unsigned char _msg [], int length ){
	//Calculate the CRC by performing polynomial division with the help of bitwise operators
	int _crc = 0;
	for(int i = 0; i < length; i++){	//for each byte
		_crc ^= _msg[i];
		for (unsigned k = 0; k < 8; k++){ //for each bit in a byte
			_crc = _crc & 1 ? (_crc >> 1) ^ 0xa001 : _crc >> 1;

		}
	}
	return _crc;
}

char ** multiDimAllocation(int _row){
	char ** _retArr = (char **)malloc(_row * (sizeof(char *)));
	for(int i = 0; i < _row; i++){
		_retArr[i] = malloc(msgLength * (sizeof(char)));

	}
	return _retArr;
}

void freeMultiDim(char ** _free, int _row){
	for(int i = 0; i < _row; i++){
		free(_free[i]);
	}
	free(_free);
}
