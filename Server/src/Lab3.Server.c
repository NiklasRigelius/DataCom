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

#define msgLength 256

struct Packet{
	int flags;  //0 = nothing, 1 = ACK, 2 = SYNC + ACK, 3 = SYNC, 4 = NAK, 5 = FIN, 6 = FIN + ACK
	int seq;
	int id;
	int windowSize;
	int crc;
	char data [msgLength];
};

struct sockaddr_in initSocket(int * _socket, int port);

void recvFromClient(int _socket, struct sockaddr_in *_client, struct Packet *_packet);
void sendToClient(int _socket, struct sockaddr_in _client, struct Packet _packet);

struct sockaddr_in connection(int _socket, struct Packet *_handshake);

void goBackN(int _socket,  struct sockaddr_in _client, int _seqMax);

int checkCRC(struct Packet _frame);
void sendFrame(int _socket, struct sockaddr_in _server, int _flag, int _id, int _seq, int _windowSize, char * _msg);
size_t buildCRCmsg(unsigned char * _CRCmsg, struct Packet _frame);
int calculateCRC16(unsigned char _msg [], int length );

void printData(struct Packet _frame);
void teardown(int _socket, struct sockaddr_in _client, struct Packet _frame, int _expectedSeq);

int main(int argc, char *argv[]) {

	int _socket;
	struct sockaddr_in _server;
	struct Packet _handshake = {-1, -1, -1, -1, -1}; //Junk values

	if(argc < 2){
	    perror("ERROR, Not enough arguments\n");
	    exit(EXIT_FAILURE);
	}

	_server = initSocket(&_socket, atoi(argv[1]));

	printf("-------------------------------------------\n");
	printf("-------------------------------------------\n");
	printf("3-way handshake\n");
	printf("-------------------------------------------\n");

	connection(_socket, &_handshake);

	printf("-------------------------------------------\n");
	printf("<< CLIENT CONNECTED >>\n");
	printf("-------------------------------------------\n");

	printf("-------------------------------------------\n");
	printf("GO-BACK-N STARTED\n");
	printf("-------------------------------------------\n");
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

	//TODO: remove : 0 = nothing, 1 = SYNC, 2 = SYNC + ACK, 3 = ACK, 4 = NAK, 5 = FIN
	while(_active){
		switch (_status){
			case 0:
				//wait for SYNC
				recvFromClient(_socket, &_client, _handshake);
				_status++;
				break;
			case 1:
				//Send SYNC + ACK
				if(checkCRC(*_handshake) == 1 && _handshake->seq == 0){
					printf("[RECEIVED - SYNC]\n");
					printf("Flag            : %d\n", _handshake->flags);
					printf("Sequence number : %d\n", _handshake->seq);
					printf("Crc             : %d\n", _handshake->crc);

					printf("-------------------------------------------\n");
				}else {
					printf("[RECEIVED - INVALID]\n");
					printf("Flag            : %d\n", _handshake->flags);
					printf("Sequence number : %d\n", _handshake->seq);
					printf("Crc             : %d\n", _handshake->crc);

					printf("-------------------------------------------\n");
					_status = 0;
				}
				_handshake->flags = 2;
				_handshake->id = 10;
				unsigned char _CRCmsg [sizeof(struct Packet)];
				size_t _CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, *_handshake);
				_handshake->crc = calculateCRC16(_CRCmsg, _CRCmsgSize);
				printf("[SENDING - SYNC + ACK]\n");
				printf("Flag            : %d\n", _handshake->flags);
				printf("Sequence number : %d\n", _handshake->seq);
				printf("Crc             : %d\n", _handshake->crc);
				printf("-------------------------------------------\n");
				sendToClient(_socket, _client, *_handshake);
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
					printf("[TIMEOUT] \n");
					printf("Reason          : No ACK received\n");
					printf("-------------------------------------------\n");
					_status = 1;
				} else {
					recvFromClient(_socket, &_client, _handshake);
					if(checkCRC(*_handshake) == 1 && _handshake->seq == 1){
						printf("[RECEIVED - ACK]\n");
						printf("Flag            : %d\n", _handshake->flags);
						printf("Sequence number : %d\n", _handshake->seq);
						printf("Crc             : %d\n", _handshake->crc);
						printf("-------------------------------------------\n");

						_status++;
						_active = 0;
					}else {
						printf("[RECEIVED - INVALID]\n");
						printf("Flag            : %d\n", _handshake->flags);
						printf("Sequence number : %d\n", _handshake->seq);
						printf("Crc             : %d\n", _handshake->crc);
						printf("-------------------------------------------\n");
						sleep(5);
						_status = 1;
					}
				}
				break;
		}
	}
	return _client;
}

void goBackN(int _socket, struct sockaddr_in _client, int _seqMax){
	int _expectedSeq = 0, _send = 1;
	int _recvACK [_seqMax];
	memset(_recvACK, 0, sizeof(_recvACK)); //set all elements to 0
	int _nrOfACKs = 0;

	struct Packet * _frame = (struct Packet*)malloc(sizeof(struct Packet));

	while(_send){
		sleep(1);
		recvFromClient(_socket, &_client, _frame);
		if(_frame->seq == _expectedSeq){ //Recv packet have the expected seq
			//Crc check
			if(checkCRC(*_frame) == 1){ //Recved packet is not corrupted -> send ACK
				if(_frame->flags == 5){ //Recved FIN start teardown process and return
					printf("GO-BACK-N ENDED\n");
					teardown(_socket, _client, *_frame, _expectedSeq);
					_send = 0;
					return;
				} else {
					printf("[RECEIVED - VALID]\n");
					printData(*_frame);
					sendFrame(_socket, _client, 3, _frame->id, _expectedSeq, _frame->windowSize, _frame->data);
					//Keep track of recv ACK to find duplicates
					_recvACK[_expectedSeq] = 1;
					_nrOfACKs++;
					if(_nrOfACKs == 3){
						_nrOfACKs--;
						if(_expectedSeq == 0) _recvACK [_seqMax - 2] = 0;
						else if(_expectedSeq == 1) _recvACK [_seqMax - 1] = 0;
						else {
							_recvACK[_expectedSeq - 2] = 0;
						}
					}
					//
					_expectedSeq = (_expectedSeq + 1) % _seqMax;
				}

			}
			else { //Recved packet is corrupted -> send NAK
				printf("[RECEIVED - CORRUPTED]\n");
				printData(*_frame);
				sendFrame(_socket, _client, 4, _frame->id, _frame->seq, _frame->windowSize, _frame->data);
			}
		} else if(_recvACK[_frame->seq] == 1){ //Recved packet is a duplicate -> Discard
			printf("[RECEIVED - DUPLICATE]\n");
			printData(*_frame);
			sendFrame(_socket, _client, 3, _frame->id, _frame->seq, _frame->windowSize, _frame->data);
		} else{	//Recved packet has wrong sequence -> Send NAK

					printf("[RECEIVED - WRONG SEQUENCE]\n");
					printData(*_frame);
					strcpy(_frame->data, "WRONG SEQUENCEEEE");
					sendFrame(_socket, _client, 4, _frame->id, _frame->seq, _frame->windowSize, _frame->data);
		}
	}

}

void teardown(int _socket, struct sockaddr_in _client, struct Packet _frame, int _expectedSeq){
	int  _status = 0;

	printf("-------------------------------------------\n");
	printf("TEARDOWN STARTED\n");
	printf("-------------------------------------------\n");

	//Setup for timeouts
	fd_set _fdSet;
	struct timeval _timeout;
	FD_ZERO(&_fdSet);
	FD_SET(_socket, &_fdSet);

	printf("[RECEIVED - FIN]\n");
	printData(_frame);
	//send FIN + ACK
	sendFrame(_socket, _client, 6, _frame.id, _expectedSeq, _frame.windowSize, _frame.data);
	//wait for FIN + ACK -> timeout
	FD_ZERO(&_fdSet);
	FD_SET(_socket, &_fdSet);
	_timeout.tv_sec = 5;
	_timeout.tv_usec = 0;
	int _checker = select(FD_SETSIZE, &_fdSet, NULL, NULL, &_timeout);
	if(_checker <= 0){
		printf("[TIMEOUT] \n");
		printf("Reason          : No FIN + ACK received\n");
		printf("<< Disconnecting... >>\n");
		printf("-------------------------------------------\n");
		_status = 1;
	} else {
		recvFromClient(_socket, &_client, &_frame);
		if(checkCRC(_frame) == 1 && _frame.seq == 6){
			printf("[RECEIVED - FIN + ACK]\n");
			printf("Flag            : %d\n", _frame.flags);
			printf("Sequence number : %d\n", _frame.seq);
			printf("Crc             : %d\n", _frame.crc);
			printf("-------------------------------------------\n");
		}
	}
	//disconnect

	printf("TEARDOWN ENDED\n");
	printf("-------------------------------------------\n");
	printf("-------------------------------------------\n");
	close(_socket);
	printf("<< SOCKET HAS BEEN CLOSED >>\n");
	printf("-------------------------------------------\n");
	printf("-------------------------------------------\n");


}

void printData(struct Packet _frame){
	printf("Flag            : %d\n", _frame.flags);
	printf("ID              : %d\n", _frame.id);
	printf("Sequence number : %d\n", _frame.seq);
	printf("Window size     : %d\n", _frame.windowSize);
	printf("Crc             : %d\n", _frame.crc);
	printf("Data            : %s\n", _frame.data);
	printf("-------------------------------------------\n");
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

	//Printing what is being sent
	char _type [20];
	switch(_flag){
		case 0:
			strcpy(_type, "FRAME");
			break;
		case 1:
			strcpy(_type, "SYNC");
			break;
		case 2:
			strcpy(_type, "SYNC + ACK");
			break;
		case 3:
			strcpy(_type, "ACK");
			break;
		case 4:
			strcpy(_type, "NAK");
			break;
		case 5:
			strcpy(_type, "FIN");
			break;
		case 6:
			strcpy(_type, "FIN + ACK");
			break;
	}
	printf("[SENDING - %s] \n", _type);
	printData(_frame);

	sendToClient(_socket, _server, _frame);
}

int checkCRC(struct Packet _frame){

	int _calculatedCRC;
	unsigned char _CRCmsg [sizeof(struct Packet)];
	size_t _CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, _frame);
	_calculatedCRC = calculateCRC16(_CRCmsg, _CRCmsgSize);

	if(_calculatedCRC == _frame.crc) return 1;
	return 0;
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

void sendToClient(int _socket, struct sockaddr_in _client, struct Packet _packet){
	int _checker;

	_checker = sendto(_socket, (struct Packet *)&_packet, (1024 + sizeof(_packet)), 0, (struct sockaddr *) &_client, sizeof(_client));
	if(_checker < 0){
	    perror("ERROR, Could not send\n");
	    exit(EXIT_FAILURE);
	}
}

void recvFromClient(int _socket, struct sockaddr_in *_client, struct Packet *_packet){
	int _checker;
	int _len = sizeof(*_client);

	_checker = recvfrom(_socket, (struct Packet *)_packet, (sizeof(*_packet)), 0, (struct sockaddr *) _client, &_len);
	if(_checker < 0){
	    perror("ERROR, Could not recv\n");
	    exit(EXIT_FAILURE);
	}
}
