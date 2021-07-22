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
#include <time.h>

#define msgLength 256

struct Packet{
	int flags;	//0 = nothing, 1 = ACK, 2 = SYNC + ACK, 3 = SYNC, 4 = NAK, 5 = FIN, 6 = FIN + ACK
	int seq;
	int id;
	int windowSize;
	int crc;
	char data [msgLength];
	time_t timestamp;
};


//Function Declaration - Sorted by return type
//(void -> int -> char ** -> struct -> size_t)
// and then alphabetical order
void connection(int _socket, struct sockaddr_in _server, struct Packet *_handshake);
void freeMultiDim(char ** _free, int _row);
void printData(struct Packet _frame, int _type);
void recvFromServer(int _socket, struct sockaddr_in _server, struct Packet *_packet);
void sendFrame(int _socket, struct sockaddr_in _server, int _flag, int _id, int _seq, int _windowSize, char * _msg);
void sendToServer(int _socket, struct sockaddr_in _server, struct Packet _packet);
void teardown(int _socket, struct sockaddr_in _server, struct Packet _handshake, int _seq);

int calculateCRC16(unsigned char _msg [], int length );
int checkCRC(struct Packet _frame);
int errorGenerator(struct Packet * _packet);
int goBackN(int _socket, struct sockaddr_in _server, struct Packet _header, char ** _inputData, int _nrOfMsg);
int waitForResponse(int _socket, struct sockaddr_in _server, int _sec, int _usec, int _expectedSeq, int _recvACKs []);

char ** multiDimAllocation(int _row);
char ** userInteraction(struct Packet *_handshake, int * _nrOfMsg);

struct sockaddr_in initSocket(int *_socket, char * argv[]);

size_t buildCRCmsg(unsigned char * _CRCmsg, struct Packet _frame);
//

 int _output = 0;

//Main function
int main(int argc, char *argv[]) {

	int _socket;
	struct sockaddr_in _server;
	struct Packet _handshake = {0, 0, 0, 0}; //JUNK values

	char ** _inputData;
	int _nrOfMsg = 0;

	if(argc != 3){
	    perror("ERROR, not enough arguments\n");
	    exit(EXIT_FAILURE);
	}

	_server = initSocket(&_socket, argv);

	printf("-------------------------------------------\n");
	printf("-------------------------------------------\n");
	printf("USER INTERACTION\n");
	printf("-------------------------------------------\n");


	_inputData = userInteraction(&_handshake, &_nrOfMsg);

	printf("-------------------------------------------\n");
	printf("-------------------------------------------\n");
	printf("3-WAY HANDSHAKE\n");
	printf("-------------------------------------------\n");

	connection(_socket, _server, &_handshake);

	printf("-------------------------------------------\n");
	printf("<< CONNECTED TO SERVER >>\n");
	printf("-------------------------------------------\n");

	printf("-------------------------------------------\n");
	printf("GO-BACK-N STARTED\n");
	printf("-------------------------------------------\n");
	int _seq = goBackN(_socket, _server, _handshake, _inputData, _nrOfMsg );
	printf("<< GO-BACK-N ENDED >>\n");
	printf("-------------------------------------------\n");

	printf("-------------------------------------------\n");
	printf("TEARDOWN STARTED \n");
	printf("-------------------------------------------\n");
	teardown(_socket, _server, _handshake, _seq);
	printf("TEARDOWN ENDED \n");
	printf("-------------------------------------------\n");
	printf("-------------------------------------------\n");
	printf("<< CLOSING CONNECTION TO SERVER... >> \n");
	printf("...\n");
	close(_socket);
	printf("<< SOCKET HAS BEEN CLOSED >>\n");
	printf("-------------------------------------------\n");
	printf("-------------------------------------------\n");

	freeMultiDim(_inputData, _nrOfMsg);
	return EXIT_SUCCESS;
}


//Function Definition - Sorted by return type
//(void -> int -> char ** -> struct -> size_t)
// and then alphabetical order (Same as Declaration)
void connection(int _socket, struct sockaddr_in _server, struct Packet *_handshake){
	//Setup for timeouts
	fd_set _fdSet;
	struct timeval _timeout;
	FD_ZERO(&_fdSet);
	FD_SET(_socket, &_fdSet);
	//Variables for CRC calc
	unsigned char _CRCmsg [sizeof(struct Packet)];
	size_t _CRCmsgSize;

	int _status = 0;
	int _active = 1;
	int _checker = 0;
	while(_active){
		switch (_status){
			case 0:
				//send SYNC
				//use the current time to generate ID
				_handshake->id = (int)(time(NULL)%10000);
				_handshake->flags = 3;
				_handshake->seq = 0;
				_handshake->timestamp = time(&_handshake->timestamp);
				_CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, *_handshake);	//CRC calculation
				_handshake->crc = calculateCRC16(_CRCmsg, _CRCmsgSize);				//...
				printf("[SENDING - SYNC]\n");
				printData(*_handshake, 1);
				sendToServer(_socket, _server, *_handshake);
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
				if(_checker < 0){
				    perror("ERROR, select returned < 0\n");
				    exit(EXIT_FAILURE);
				} else if(_checker <= 0){  //Timeout -> set _status to 0 so a SYNC will be sent again
					printf("[TIMEOUT] \n");
					printf("Reason          : No SYNC + ACK received\n");
					printf("-------------------------------------------\n");
					_status = 0;
				} else {	//Recv something from Server
					recvFromServer(_socket, _server, _handshake);
					if(checkCRC(*_handshake) == 1 && _handshake->flags == 2 && _handshake->seq == 0){	//If recv packet was the one expected -> set status to 2 and send ACK
						printf("[RECEIVED - SYNC + ACK]\n");											//Expected is checked by looking at the CRC, flag and seq number.
						printData(*_handshake, 1);
						_status ++;
					}else {		//If recv packet was not the one expected -> set status to 0 and resend SYNC
						printf("[RECEIVED - INVALID]\n");
						printData(*_handshake, 1);
						_status = 0;
					}
				}
				break;
			case 2:
				//Send ACK
				_handshake->flags = 1;
				_handshake->seq = 1;
				_handshake->timestamp = time(&_handshake->timestamp);
				_CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, *_handshake);	//For calculating CRC, _CRCmsgSize will contain the size of merged data from the frames flag, seq, id, windowSize and data
				_handshake->crc = calculateCRC16(_CRCmsg, _CRCmsgSize);				//calculating CRC
				printf("[SENDING - ACK]\n");
				printData(*_handshake, 1);
				sendToServer(_socket, _server, *_handshake);
				_status++;
				break;
			case 3:
				//To make sure no NAK or unexpected packet is recved from the server we wait 3 sec before starting the Go-Back-N procedure
				FD_ZERO(&_fdSet);
				FD_SET(_socket, &_fdSet);
				_timeout.tv_sec = 3;
				_timeout.tv_usec = 0;
				_checker = select(FD_SETSIZE, &_fdSet, NULL, NULL, &_timeout);
				if(_checker < 0){
					perror("ERROR, select returned < 0\n");
					exit(EXIT_FAILURE);
				} else if(_checker == 0){
					printf("[NO ACK RECEIVED] - Connection established \n");
					printf("-------------------------------------------\n");
					_status ++;
					_active = 0;
				} else { //Something went wrong (ACK lost/corrupted) -> resending ACK
					recvFromServer(_socket, _server, _handshake);
					printf("[RESENDING] - Recieved invalid packet, resending ACK... \n");
					printData(*_handshake, 1);
					printf("-------------------------------------------\n");
					_status = 2;
				}
				break;
		}
	}
}
void freeMultiDim(char ** _free, int _row){
	for(int i = 0; i < _row; i++){
		free(_free[i]);
	}
	free(_free);
}
void printData(struct Packet _frame, int _type){
	time_t _time = time(&_time);

	if(_type == 1) { //Handshake / teardown
		printf("Flag            : %d\n", _frame.flags);
		printf("Sequence number : %d\n", _frame.seq);
		printf("Crc             : %d\n", _frame.crc);
		printf("Time sent       : %s", ctime(&_frame.timestamp));
		printf("Time received   : %s", ctime(&_time));
	}
	else if(_type == 2){ // Sliding window send
		printf("Flag            : %d\n", _frame.flags);
		printf("ID              : %d\n", _frame.id);
		printf("Sequence number : %d\n", _frame.seq);
		printf("Window size     : %d\n", _frame.windowSize);
		printf("Crc             : %d\n", _frame.crc);
		printf("Data            : %s", _frame.data);
		printf("Time sent       : %s", ctime(&_frame.timestamp));
	}	else if(_type == 3){ // Sliding window recv
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
void recvFromServer(int _socket, struct sockaddr_in _server, struct Packet *_packet){
	//Recv data from server and save it into _packet
	int _checker;
	int _len = sizeof(_server);
	_checker = recvfrom(_socket, (struct Packet *)_packet, (sizeof(*_packet)), 0, (struct sockaddr *) &_server, &_len);
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

	//Calc CRC
	unsigned char _CRCmsg [sizeof(struct Packet)];
	size_t _CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, _frame);
	_frame.crc = calculateCRC16(_CRCmsg, _CRCmsgSize);

	//set Time sent
	_frame.timestamp = time(&_frame.timestamp);

	//Printing what is being sent
	char _type [20];
	switch(_flag){
		case 0:
			strcpy(_type, "FRAME");
			break;
		case 1:
			strcpy(_type, "ACK");
			break;
		case 2:
			strcpy(_type, "NAK");
			break;
	}
	printf("[SENDING - %s] \n", _type);
	printData(_frame, 2);

	sendToServer(_socket, _server, _frame);
}
void sendToServer(int _socket, struct sockaddr_in _server, struct Packet _packet){
	int _checker, _errorGen;
	_errorGen = errorGenerator(&_packet); //20 % chance of error (10% corruption and 10% lost), if returned 1 the packet should be sent else error generated lost and therefore packet will not be sent.
	if(_errorGen == 1){
		_checker = sendto(_socket, (struct Packet *)&_packet, (1024 + sizeof(_packet)), 0, (struct sockaddr *) &_server, sizeof(_server));
		if(_checker < 0){
		    perror("ERROR, Could not send\n");
		    exit(EXIT_FAILURE);
		}
	}

}
void teardown(int _socket, struct sockaddr_in _server, struct Packet _handshake, int _seq){
	int _active = 1, _status = 0, _nrOfTimeouts = 0;
	int _seqMax = (_handshake.windowSize * 2) + 1;
	//Setup for timeouts
	fd_set _fdSet;
	struct timeval _timeout;
	FD_ZERO(&_fdSet);
	FD_SET(_socket, &_fdSet);
	unsigned char _CRCmsg [sizeof(struct Packet)];
	size_t _CRCmsgSize;

	while(_active){
		switch(_status){
		case 0:
			//sending FIN and setting _status to 1
			_handshake.flags = 5;
			_handshake.seq = _seq;
			_handshake.timestamp = time(&_handshake.timestamp);
			_CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, _handshake);
			_handshake.crc = calculateCRC16(_CRCmsg, _CRCmsgSize);
			printf("[SENDING - FIN]\n");
			printData(_handshake, 1);
			sendToServer(_socket, _server, _handshake);
			_status++;
			break;
		case 1:
			//waiting for FIN + ACK
			//if no FIN + ACK is recv -> change _status to 0 and re-send FIN
			//This can be done N number of time, in this case we set N to be 10 times.
			//If no FIn + ACK is recved after N number of times -> disconnect anyway.
			//Timer - 1 sec
			FD_ZERO(&_fdSet);
			FD_SET(_socket, &_fdSet);
			_timeout.tv_sec = 1;
			_timeout.tv_usec = 0;

			int _checker = select(FD_SETSIZE, &_fdSet, NULL, NULL, &_timeout);
			if(_checker < 0){
			    perror("ERROR, select returned < 0\n");
			    exit(EXIT_FAILURE);
			} else if(_checker <= 0){ //No FIN + ACK recv, increase _nrOfTimeouts until it is equal to N as described above
				printf("[TIMEOUT] \n");
				printf("Reason          : No FIN + ACK received\n");
				_nrOfTimeouts++;
				_status  = 0;
				if(_nrOfTimeouts > 10){ //If FIN + ACK has been resent N (N = 10) number of times, set _status to 2 and send FIN + ACK and disconnect.
					printf("<< Number of max timeouts reached, sending FIN + ACK  >>\n");
					_status = 2;
				}
				printf("-------------------------------------------\n");
			} else {
				recvFromServer(_socket, _server, &_handshake);
				if(checkCRC(_handshake) == 1 && _handshake.flags == 6){	//If recv packet that is expected, set _status to 2 and send FIN + ACK.
					printf("[RECEIVED - FIN + ACK]\n");
					printData(_handshake, 1);
					_status ++;
				}else {	//If recv is not the packet to be expect, set _status to 0 and re-send FIN to server.
					printf("[RECEIVED - INVALID]\n");
					printData(_handshake, 1);
					_status = 0;
				}
			}
			break;
		case 2:
			//send FIN + ACK and return were the disconnect to the server take place.
			_handshake.flags = 6;
			_handshake.seq = (_seq + 1) % _seqMax;
			_handshake.timestamp = time(&_handshake.timestamp);
			_CRCmsgSize = buildCRCmsg((unsigned char *)&_CRCmsg, _handshake);
			_handshake.crc = calculateCRC16(_CRCmsg, _CRCmsgSize);
			printf("[SENDING - FIN + ACK]\n");
			printData(_handshake, 1);
			sendToServer(_socket, _server, _handshake);
			_status++;
			_active = 0;
			break;
		}
	}

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
//	if(_output == 5 ) {
//		_generated = 5;
//		_output++;
//	} else if(_output == 7){
//		_generated = 15;
//		_output ++;
//	}else {
//		_generated = 50;
//	}
//	_output++;
	if(_generated <= 20) { //20 % chance for error
		if(_generated <= 10){ //10% to generate a corrupted packet
			printf("^^^^^^ [Generated error] : Above Packet Corrupted ^^^^^^ \n");
			strcpy(_packet->data, "CORRUPTED FRAME\n");
			printf("-------------------------------------------\n");
			return 1;
		}
		else {	//10% to generate an lost packet
			printf("^^^^^^ [Generated error] : Above Packet Lost ^^^^^^ \n");
			printf("-------------------------------------------\n");
			return -1;
		}
	}
	return 1;
}
int goBackN(int _socket, struct sockaddr_in _server, struct Packet _header,char ** _inputData, int _nrOfMsg){
	int _start = 0, _end = 0, _sentFrames = 0, _recvACK = 0, _expectedACK = 0;
	int _windowsAvailable = _header.windowSize;
	int _seqMax = (_header.windowSize * 2) + 1;

	//Variables to keep track of the ACK.
	//This will help with handling e.g duplicated packet or recv packet with wrong seq.
	int _acceptedACKs [_seqMax];
	memset((int *)_acceptedACKs, 0, sizeof(_acceptedACKs)); //set all elements to 0
	int _nrOfAcceptedACKs = 0;

	while(!(_sentFrames == _nrOfMsg && _recvACK == _nrOfMsg)){	//Keep going until all packet has been sent and all ACK for these packet has been recv.
		if(_windowsAvailable > 0 && _sentFrames != _nrOfMsg){	//If there is free space in window -> send frame
			sendFrame(_socket, _server, 0, _header.id, (_end % _seqMax), _header.windowSize, _inputData[_end]);
			_windowsAvailable --; 	//One frame is "on the move", there is one less window available
			_end++;					//increase the _end variable to point to next data to be sent
			_sentFrames++; 			//increase the amount of frames sent
		}
		int _recvStatus;
		//Do a quick check to see if there is any incoming msg
		//we are only interesting in ACKs and NAKs here, if timeout is triggered just continue
		//The timer is set to 0 sec, 1 usec
		_recvStatus = waitForResponse(_socket, _server, 0, 1, (_start % _seqMax), _acceptedACKs); //Listen for packets to be recved. returns 1 if recved ACK, -2 if recved is NAK and -3 if recv is  corrupted.
		if(_recvStatus == 1){  		//recved a ACK
			//Keep track of recv ACK to find duplicates
			_expectedACK = (_start % _seqMax);
			_acceptedACKs[_expectedACK] = 1;
			_nrOfAcceptedACKs++;
			if(_nrOfAcceptedACKs == _header.windowSize + 1){
				_nrOfAcceptedACKs--;
				if(_expectedACK < _header.windowSize) _acceptedACKs [_seqMax - (_header.windowSize - _expectedACK)] = 0;
				else {
					_acceptedACKs[_expectedACK - _header.windowSize] = 0;
				}
			}
			_start++;				//increase the _start variable to point to the next data needing a ACK
			_windowsAvailable++;	//Open up a slot for data to be sent
			_recvACK ++;
		}
		else if(_recvStatus == -2 || _recvStatus == -3){ //recv an NAK or corrupted packet
			_sentFrames -= (_end - _start);
			_end = _start;			//reset _end to point to the start of the window -> will make sure the window is resent
			_windowsAvailable = _header.windowSize;	//Make all slots in window available
		}
		//If there is no windows available or we sent all frames but not recv all ACKs for them
		//we need to wait for ACK, NAK or timeout
		//Timer is set to 5 sec, if no response from server is available we resend the packets in the window
		//We are therefore interested in if the timer is triggered
		if(_windowsAvailable == 0 || (_sentFrames == _nrOfMsg && _sentFrames != _recvACK)){
			_recvStatus = waitForResponse(_socket, _server, 5, 0, (_start % _seqMax), _acceptedACKs);//Listen for packets to be recved. returns 1 if recved ACK, -1 if timeout was triggered, -2 if recved is NAK and -3 if recv is  corrupted.
			if(_recvStatus == 1){  //recv an ACK
				//Keep track of recv ACK to find duplicates -> if an _acceptedACKs["recved seq"] == 1, it is seen as a duplicate.
				_expectedACK = (_start % _seqMax);
				_acceptedACKs[_expectedACK] = 1;  // Example: Max seq is 5 and we recved ACK from seq 0 and 1 earlier. Now we recv seq 2. Then we have _acceptedACKs = [1, 1, 1, 0, 0]
				_nrOfAcceptedACKs++;					// The value is 2 but we increase it to 3
				if(_nrOfAcceptedACKs == _header.windowSize){				// WindowSize is 2 so we can only have 2 element in the array set to 1, so we change the the element first set to 0. Like FIFO.
					_nrOfAcceptedACKs--;
					if(_expectedACK < _header.windowSize) _acceptedACKs [_seqMax - (_header.windowSize - _expectedACK)] = 0;	//If we have _acceptedACKs = [1, 0, 0, 1, 1] we change it to [1, 0, 0, 0, 1]
					else {
						_acceptedACKs[(_start % _seqMax) - _header.windowSize] = 0; 						//If we have _acceptedACKs = [1, 1, 1, 0, 0] we change it to [0, 1, 1, 0, 0]
					}
				}
				_start++;
				_windowsAvailable++;
				_recvACK ++;
			}
			else if(_recvStatus == -1 ||_recvStatus == -2 || _recvStatus == -3){ //if timer is triggered, NAK was received or packet corrupted
				_sentFrames -= (_end - _start);
				_end = _start;	//reset _end to point to the start of the window -> will make sure the window is resent
				_windowsAvailable = _header.windowSize;
			}
		}
	}
	return (_end % _seqMax); //next sequence to be sent for the Teardown
}
int waitForResponse(int _socket, struct sockaddr_in _server, int _sec, int _usec, int _expectedSeq, int _recvACKs []){
	//returns 1 if ACK is recv
	//returns -1 if timer is triggered
	//returns -2 if NAK is recv
	//returns -3 if Corrupted
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
	    perror("ERROR, select returned < 0\n");
	    exit(EXIT_FAILURE);
	}
	else if(_checker == 0){ //timer triggered
		if(_usec == 0) {	//If timeouts is important
			printf("[TIMEOUT] \n");
			printf("Reason          : No ACK received for packet with sequence number %d\n", _expectedSeq);
			printf("-------------------------------------------\n");
		}
		return -1;
	}
	else {
		//recv msg -> check if correct flag and seq
		recvFromServer(_socket, _server, &_recv);
		if(_recv.flags == 3 && _recv.seq == _expectedSeq){ //move window to the "right"
			if(checkCRC(_recv) == 1){ //Packet not corrupted -> return 1
				printf("[RECEIVED - VALID]\n");
				printData(_recv, 3);
				return 1;
			} else{ //Packet corrupted -> return -3;
				printf("[RECEIVED - CORRUPTED]\n");
				printData(_recv, 3);
				return -3;
			}
		} else if(_recvACKs[_recv.seq] == 1 ){	// recved "duplicate" do nothing
			printf("[RECEIVED - UNEXPECTED SEQ]\n");
			printData(_recv, 3);
			return 0;
		} else if((_recv.seq != _expectedSeq && _recv.flags == 4)){	//recved NAK from wrong seq do nothing
			printf("[RECEIVED - NAK FROM WRONG SEQ]\n");
			printData(_recv, 3);
			return 0;
		}else if((_recv.seq != _expectedSeq && _recv.flags == 3)){	//recved ACK from wrong seq do nothing
			printf("[RECEIVED - RESPONSE FROM WRONG SEQ]\n");
			printData(_recv, 3);
			return 0;
		}
		else{ //recved NAK re-send window.
			printf("[RECEIVED - NAK]\n");
			printData(_recv, 3);
			return -2;
		}
	}
	return -1;
}

char ** multiDimAllocation(int _row){
	char ** _retArr = (char **)malloc(_row * (sizeof(char *)));
	for(int i = 0; i < _row; i++){
		_retArr[i] = malloc(msgLength * (sizeof(char)));

	}
	return _retArr;
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

	return _data;
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
	memset(&_server, 0, sizeof(_server));	//Set _server elements to have value 0

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

size_t buildCRCmsg(unsigned char * _CRCmsg, struct Packet _frame){
	//Add header information and Data together to be a part of the crc calculation
	//return the size of the unisgned char and the unsigned char containing the data
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
