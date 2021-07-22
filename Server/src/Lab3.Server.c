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
#include <time.h>


#define msgLength 256

struct Packet{
	int flags;  //0 = nothing, 1 = ACK, 2 = SYNC + ACK, 3 = SYNC, 4 = NAK, 5 = FIN, 6 = FIN + ACK
	int seq;
	int id;
	int windowSize;
	int crc;
	char data [msgLength];
	time_t timestamp;
};

//Function Declaration - Sorted by return type
//(void -> int -> struct -> size_t)
// and then alphabetical order
void goBackN(int _socket,  struct sockaddr_in _client, int _seqMax, int _windowSize);
void printData(struct Packet _frame, int _type);
void recvFromClient(int _socket, struct sockaddr_in *_client, struct Packet *_packet);
void sendFrame(int _socket, struct sockaddr_in _server, int _flag, int _id, int _seq, int _windowSize, char * _msg);
void sendToClient(int _socket, struct sockaddr_in _client, struct Packet _packet);
void teardown(int _socket, struct sockaddr_in _client, struct Packet _frame, int _expectedSeq);

int calculateCRC16(unsigned char _msg [], int length );
int checkCRC(struct Packet _frame);
int errorGenerator(struct Packet * _packet);

struct sockaddr_in connection(int _socket, struct Packet *_handshake);
struct sockaddr_in initSocket(int * _socket, int port);

size_t buildCRCmsg(unsigned char * _CRCmsg, struct Packet _frame);
//
 int _output = 0;
//Main Function
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
	goBackN(_socket, _server, (_handshake.windowSize * 2) + 1, _handshake.windowSize);

	return EXIT_SUCCESS;
}

//Function Declaration - Sorted by return type
//(void -> int -> struct -> size_t)
// and then alphabetical order
void goBackN(int _socket, struct sockaddr_in _client, int _seqMax, int _windowSize){
	int _expectedSeq = 0, _send = 1;
	//Variables to keep track of the ACK.
	//This will help with handling e.g duplicated packet or recv packet with wrong seq.
	int _recvACK [_seqMax];
	memset(_recvACK, 0, sizeof(_recvACK)); //set all elements in _recvACK to 0
	int _nrOfACKs = 0;

	struct Packet * _frame = (struct Packet*)malloc(sizeof(struct Packet));

	while(_send){
		sleep(1); //sleep for so its possible to see that the sliding window procedure works
		recvFromClient(_socket, &_client, _frame);
		if(_frame->seq == _expectedSeq){ //Recv packet have the expected seq
			//Crc check
			if(checkCRC(*_frame) == 1){ //Recved packet is not corrupted -> send ACK
				if(_frame->flags == 5){ //Recved FIN start teardown process and return
					printf("GO-BACK-N ENDED\n");
					teardown(_socket, _client, *_frame, _expectedSeq); //start teardown with the recv FIN
					_send = 0;
					return;
				} else { //Recv a ACK for the expected seq and is not corrupted
					printf("[RECEIVED - VALID]\n");
					printData(*_frame, 3);
					sendFrame(_socket, _client, 3, _frame->id, _expectedSeq, _frame->windowSize, _frame->data);
					//Keep track of recv ACK to find duplicates -> if an _recvACK["recved seq"] == 1, it is seen as a duplicate.
					_recvACK[_expectedSeq] = 1;	// Example: Max seq is 5 and we recved ACK from seq 0 and 1 earlier. Now we recv seq 2. Then we have _acceptedACKs = [1, 1, 1, 0, 0]
					_nrOfACKs++;				// The value is 2 but we increase it to 3
					if(_nrOfACKs == _windowSize + 1){			// WindowSize is 2 so we can only have 2 element in the array set to 1, so we change the the element first set to 0. Like FIFO.
						_nrOfACKs--;
						if(_expectedSeq < _windowSize) _recvACK [_seqMax - (_windowSize - _expectedSeq)] = 0;		//If we have _acceptedACKs = [1, 0, 0, 1, 1] we change it to [1, 0, 0, 0, 1]
						else {
							_recvACK[_expectedSeq - _windowSize] = 0;						//If we have _acceptedACKs = [1, 1, 1, 0, 0] we change it to [0, 1, 1, 0, 0]
						}
					}
					_expectedSeq = (_expectedSeq + 1) % _seqMax; //Set new expected seq
				}
			}
			else { //Recved packet is corrupted -> send NAK
				printf("[RECEIVED - CORRUPTED]\n");
				printData(*_frame, 3);
				sendFrame(_socket, _client, 4, _frame->id, _frame->seq, _frame->windowSize, _frame->data);
			}
		} else if(_recvACK[_frame->seq] == 1){ //Recved packet is a duplicate -> Discard, send new ACK
			printf("[RECEIVED - DUPLICATE]\n");
			printData(*_frame, 3);
			sendFrame(_socket, _client, 3, _frame->id, _frame->seq, _frame->windowSize, _frame->data);
		} else {	//Recved packet has sequence outside the "window", somehting has gone wrong -> send NAK
			printf("[RECEIVED - WRONG SEQUENCE]\n");
			printData(*_frame, 3);
			strcpy(_frame->data, "WRONG SEQUENCE\n");
			sendFrame(_socket, _client, 4, _frame->id, _frame->seq, _frame->windowSize, _frame->data);
		}
	}
	free(_frame);
}
void printData(struct Packet _frame, int _type){
	time_t _time = time(&_time);

	if(_type == 1){ // Connection / teardown
		printf("Flag            : %d\n", _frame.flags);
		printf("Sequence number : %d\n", _frame.seq);
		printf("Crc             : %d\n", _frame.crc);
		printf("Time sent       : %s", ctime(&_frame.timestamp));
		printf("Time received   : %s", ctime(&_time));
	} else if(_type == 2){	//Go-Back-N sending frame
		printf("Flag            : %d\n", _frame.flags);
		printf("ID              : %d\n", _frame.id);
		printf("Sequence number : %d\n", _frame.seq);
		printf("Window size     : %d\n", _frame.windowSize);
		printf("Crc             : %d\n", _frame.crc);
		printf("Data            : %s", _frame.data);
		printf("Time sent       : %s", ctime(&_frame.timestamp));
	} else{	//Go-Back-N recv frame
		printf("Flag            : %d\n", _frame.flags);
		printf("ID              : %d\n", _frame.id);
		printf("Sequence number : %d\n", _frame.seq);
		printf("Window size     : %d\n", _frame.windowSize);
		printf("Crc             : %d\n", _frame.crc);
		printf("Data            : %s", _frame.data);
		printf("Time sent       : %s", ctime(&_frame.timestamp));
		printf("Time received   : %s", ctime(&_time));
	}

	printf("-------------------------------------------\n");
}
void recvFromClient(int _socket, struct sockaddr_in *_client, struct Packet *_packet){
	//Recv data from client and save it into _packet
	int _checker;
	int _len = sizeof(*_client);
	_checker = recvfrom(_socket, (struct Packet *)_packet, (sizeof(*_packet)), 0, (struct sockaddr *) _client, &_len);
	if(_checker < 0){
	    perror("ERROR, Could not recv\n");
	    exit(EXIT_FAILURE);
	}
}
void sendFrame(int _socket, struct sockaddr_in _server, int _flag, int _id, int _seq, int _windowSize, char * _msg){
	struct Packet _frame;
	_frame.flags = _flag;
	_frame.id = _id;
	_frame.seq = _seq;
	_frame.windowSize = _windowSize;
	strcpy(_frame.data, _msg);

	//Calc crc
	unsigned char _CRCmsg [sizeof(struct Packet)];
	size_t _CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, _frame);
	_frame.crc = calculateCRC16(_CRCmsg, _CRCmsgSize);

	//time stamps
	_frame.timestamp = time(&_frame.timestamp);

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
	}
	printf("[SENDING - %s] \n", _type);
	printData(_frame, 2);

	sendToClient(_socket, _server, _frame);
}
void sendToClient(int _socket, struct sockaddr_in _client, struct Packet _packet){
	int _checker, _errorGen;
	_errorGen = errorGenerator(&_packet); //20 % chance of error (10% corruption and 10% lost), if returned 1 the packet should be sent else error generated lost and therefore packet will not be sent.
	if(_errorGen == 1){
		_checker = sendto(_socket, (struct Packet *)&_packet, (1024 + sizeof(_packet)), 0, (struct sockaddr *) &_client, sizeof(_client));
		if(_checker < 0){
		    perror("ERROR, Could not send\n");
		    exit(EXIT_FAILURE);
		}
	}
}
void teardown(int _socket, struct sockaddr_in _client, struct Packet _frame, int _expectedSeq){
	printf("-------------------------------------------\n");
	printf("TEARDOWN STARTED\n");
	printf("-------------------------------------------\n");

	//Setup for timeouts
	fd_set _fdSet;
	struct timeval _timeout;
	FD_ZERO(&_fdSet);
	FD_SET(_socket, &_fdSet);

	printf("[RECEIVED - FIN]\n");
	printData(_frame, 1);

	//Send FIN + ACK
	//wait for FIN + ACK -> timeout
	//If no FIN + ACK was recv, resend FIN + ACK. This can be done N number of times (N = 10), then close the connection anyway.
	int N = 10;
	while(N > 0){
		//SEND FIN + ACK
		printf("[SENDING - FIN + ACK]\n");
		_frame.flags = 6;
		_frame.timestamp = time(&_frame.timestamp);
		unsigned char _CRCmsg [sizeof(struct Packet)];							//Calculate crc
		size_t _CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, _frame);	//...
		_frame.crc = calculateCRC16(_CRCmsg, _CRCmsgSize);						//...
		printData(_frame, 1);
		sendToClient(_socket, _client, _frame);
		//WAIT for FIN + ACK
		FD_ZERO(&_fdSet);
		FD_SET(_socket, &_fdSet);
		_timeout.tv_sec = 1;
		_timeout.tv_usec = 0;
		int _checker = select(FD_SETSIZE, &_fdSet, NULL, NULL, &_timeout);
		if(_checker < 0){
			perror("ERROR, select returned < 0\n");
			exit(EXIT_FAILURE);
		} else if(_checker == 0){ //No FIN + ACK recv, re-send FIN + ACK
			printf("[TIMEOUT] \n");
			printf("Reason          : No FIN + ACK received\n");
			printf("-------------------------------------------\n");
		} else {
			recvFromClient(_socket, &_client, &_frame);
			if(checkCRC(_frame) == 1 && _frame.flags == 6){ //FIN + ACK recved, leave loop and start disconnecting from client.
				printf("[RECEIVED - FIN + ACK]\n");
				printData(_frame, 1);
				N = -1;
			} else { //If an FIN was recv again we need to send another FIN + ACK.
				printf("[RESENDING] - RECEIVED FIN BUT WAITED FOR FIN + ACK\n");
				printf("-------------------------------------------\n");
			}
		}
		N--;
	}
	if(N == 0){ // If No FIN + ACK was recv after N number of resend, we start disconnecting from client anyway.
		printf("<< Number of max timeouts reached, disconnecting...  >>\n");
		printf("-------------------------------------------\n");
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
int checkCRC(struct Packet _frame){
	//Check the CRC by calculating the CRC of _frame and see if the _frame.CRC is the same as calcualted CRC
	//If the same -> not corrupted, return 1
	//If not the same -> it is corrupted, return 0
	int _calculatedCRC;
	unsigned char _CRCmsg [sizeof(struct Packet)];
	size_t _CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, _frame);
	_calculatedCRC = calculateCRC16(_CRCmsg, _CRCmsgSize);

	if(_calculatedCRC == _frame.crc) return 1;
	return 0;
}
int errorGenerator(struct Packet * _packet){
	srand(time(NULL));
	int _generated = (rand() % 100) + 1; //rand() % 100 = range [0, 99], by adding 1 we get range [1, 100]
//	if(_output == 6 || _output == 12) {
//		_generated = 5;
//		_output++;
//	} else if(_output == 3 ){
//		_generated = 15;
//		_output ++;
//	} else {
//		_generated = 50;
//	}
//	_output++;
	if(_generated <= 20) { //20 % chance for error
		if(_generated <= 10){//10% to generate a corrupted packet
			printf("^^^^^^ [Generated error] : Above Packet Corrupted ^^^^^^\n");
			printf("-------------------------------------------\n");
			strcpy(_packet->data, "CORRUPTED FRAME\n");
			return 1;
		}
		else {//10% to generate an lost packet
			printf("^^^^^^ [Generated error] : Above Packet Lost ^^^^^^ \n");
			printf("-------------------------------------------\n");
			return -1;
		}
	}
	return 1;
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

	while(_active){
		switch (_status){
			case 0:
				//wait for SYNC
				recvFromClient(_socket, &_client, _handshake);
				//check if valid meaning it has correct seq and flag and the checkCRC returned 1. set _status to 1.
				if(checkCRC(*_handshake) == 1 && _handshake->seq == 0 && _handshake->flags == 3){
					printf("[RECEIVED - SYNC]\n");
					printData(*_handshake, 1);
					_status = 1;
				}else {	//If not valid, a NAK is sent
					printf("[RECEIVED - INVALID]\n");
					printData(*_handshake, 1);
					_handshake->flags = 4;
					_handshake->timestamp = time(&_handshake->timestamp);
					unsigned char _CRCmsg [sizeof(struct Packet)];								//CRC calculation
					size_t _CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, *_handshake);	//...
					_handshake->crc = calculateCRC16(_CRCmsg, _CRCmsgSize);
					printf("[SENDING - NAK]\n");
					printData(*_handshake, 1);
					sendToClient(_socket, _client, *_handshake);
					break;
				}
				break;
			case 1:
				//Recved valid SYNC so we send SYNC + ACK and set _status to 2.
				_handshake->flags = 2;
				_handshake->timestamp = time(&_handshake->timestamp);
				unsigned char _CRCmsg [sizeof(struct Packet)];
				size_t _CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, *_handshake);	//CRC calculation
				_handshake->crc = calculateCRC16(_CRCmsg, _CRCmsgSize);						//...
				printf("[SENDING - SYNC + ACK]\n");
				printData(*_handshake, 1);
				sendToClient(_socket, _client, *_handshake);
				_status++;
				break;
			case 2:
				//start timer and wait for ACK, if timeout or no ACK is recved (recved corrupted or wrong seq) resend SYNC + ACK by setting _status to 1.
				FD_ZERO(&_fdSet);
				FD_SET(_socket, &_fdSet);
				_timeout.tv_sec = 5;
				_timeout.tv_usec = 0;
				_checker = select(FD_SETSIZE, &_fdSet, NULL, NULL, &_timeout);
				if(_checker < 0){
				    perror("ERROR, select returned < 0\n");
				    exit(EXIT_FAILURE);
				}else if(_checker == 0){	//Timeout, nothing to recv from client, set -status to 1
					printf("[TIMEOUT] \n");
					printf("Reason          : No ACK received\n");
					printf("-------------------------------------------\n");
					_status = 1;
				} else {
					recvFromClient(_socket, &_client, _handshake);
					if(checkCRC(*_handshake) == 1 && _handshake->seq == 1){	//Recved valid ACK, end handshake.
						printf("[RECEIVED - ACK]\n");
						printData(*_handshake, 1);
						_status++;
						_active = 0;
					}else {	//Recved something that was not a valid ACK (corrupted, wrong seq etc.)
						printf("[RECEIVED - INVALID] ACK MISSING\n");
						printData(*_handshake, 1);
						_status = 1;
					}
				}
				break;
		}
	}
	return _client;
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
//

