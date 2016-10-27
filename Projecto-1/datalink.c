#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "util.h"
#include <signal.h>
#include <errno.h>
#include "datalink.h"

#define BAUDRATE B38400
#define MODEMDEVICE "/dev/ttyS1"
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

#define START 0
#define FLAG_RCV 1
#define A_RCV 2
#define C_RCV 3
#define BCC_RCV 4
#define COMPLETE 5

#define TRANSMITTER 0
#define RECEIVER 1

#define SET_PACK 0
#define UA_PACK 1
#define DISC_PACK 2
#define RR_0PACK 3
#define RR_1PACK 4
#define REJ_0PACK 5
#define REJ_1PACK 6

#define DATA_PACK 1
#define START_PACK 2
#define END_PACK 3

#define TRAMA_SIZE 64


volatile int flag=1, conta=1;


void atende()                   // atende alarme
{
	printf("alarme # %d\n", conta);
	flag=1;
	conta++;
}

void installAlarm(){
	(void) signal(SIGALRM, atende);  // instala  rotina que atende interrupcao
	printf("Alarme Instalado\n");
}


//TODO: ver argumento porta pwp18
int llopen(int flag, int fd)
{
	if(flag == TRANSMITTER)
	llopenTransmitter(fd);
	else if(flag == RECEIVER)
	llopenReceiver(fd);
	return 1;
}

int llclose(int flag, int fd)
{
	if(flag == TRANSMITTER)
	llcloseTransmitter(fd);
	else if(flag == RECEIVER)
	llcloseReceiver(fd);
	sleep(5);
	return 1;
}


int llwrite(int fd, char *buffer, int length, int C){

	char * copy = malloc(length + 1);
	memcpy(copy,buffer,length);

	char bcc2 = makeBCC2(buffer, length);
	copy[length] = bcc2;

	int size = stuffing(copy, length + 1);

	size = packagePayLoad(C, size, copy);

	int i;
	printf("llwrite trama I : size:%d \n",size);
	for(i = 0; i < size ; i ++){
		printf("%d - %x\n",i, copy[i]);
	}

	int noResponse = 1;
	char ch;
	int status = -1;
	flag = 1;
	conta = 0;

	while(conta < 4  && noResponse != COMPLETE){
		if(flag){
			alarm(3);
			flag=0;
			sendMensage(fd,copy,size);
		}

		noResponse = receiveSupervision(fd,&ch);
		if(noResponse == COMPLETE && (ch == C_RR0 || ch == C_RR1 || ch == C_REJ0 || ch == C_REJ1)){
			status = checkRR_Reject(C, ch);
		}
	}

	free(copy);
	return status;
}

int llread(int fd,char *buffer, int C){

	char *trama  = malloc(8); //replace with 1
	int size  = getTrama(fd, trama);

	/* //test
	int size = 8;
	trama[0] = 0x7E;
	trama[1] = 0x03;
	trama[2] = 0x00;
	trama[3] = 0x03;
	trama[4] = 0x6F;
	trama[5] = 0x69;
	trama[6] = 0x06;
	trama[7] = 0x7E;*/

	if(size < 5)
	{
		printf("Wrong trama: size: %d\n",size);
		if(C == 1)
		createAndSendPackage(fd, REJ_1PACK);
		else if(C == 0)
		createAndSendPackage(fd, REJ_0PACK);
		return -1;
	}

	printf("package Valid size, %d\n",size);

	char *package = malloc(1);
	size = extractPackage(package,trama,size);
	printf("package extracted:size %d \n",size);


	size = deStuffing(package, size);
	int i;

	for(i = 0; i < size;i++){
		printf("p: %x \n",(unsigned char) package[i]);
	}

	char bcc = makeBCC2( package, size-1);

	if(bcc == package[size - 1])
	{
		printf("BCC2 check: %d\n", C);
		if(C == 1)
			createAndSendPackage(fd, RR_0PACK);
		else if (C == 0)
			createAndSendPackage(fd, RR_1PACK);

	}
	else
	{
		printf("BCC2 fail\n");
		if(C == 1)
		createAndSendPackage(fd, REJ_1PACK);
		else if (C == 0)
		createAndSendPackage(fd, REJ_0PACK);

		return -1;
	}

	size -= 1; //removes bcc2;

	buffer = realloc(buffer, size);

	memcpy(buffer, package,size);

	free(package);

	for(i = 0; i < size; i++){
		printf("%d - %x - %c \n", i, buffer[i], buffer[i]);
	}

	return 0;
}

int main(int argc, char** argv)
{

	int fd;
	struct termios oldtio,newtio;

	if ( (argc < 3) ||
	((strcmp("/dev/ttyS0", argv[1])!=0) &&
	(strcmp("/dev/ttyS1", argv[1])!=0) )) {
		printf("Usage:\tnserial SerialPort\n\tex: nserial /dev/ttyS0  RECEIVER||TRANSMITTER\n");
		exit(1);
	}

	if( (strcmp("RECEIVER", argv[2]) != 0) && (strcmp("TRANSMITTER", argv[2]) != 0))
	{
		printf("choose RECEIVER or TRANSMITTER\n");
		exit(2);
	}

	fd = open(argv[1], O_RDWR | O_NOCTTY|O_NONBLOCK);
	if (fd <0) {perror(argv[1]); exit(-1); }

	if ( tcgetattr(fd,&oldtio) == -1) { // save current port settings
		perror("tcgetattr");
		exit(-1);
	}

	bzero(&newtio, sizeof(newtio));
	newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = OPOST;
	// set input mode (non-canonical, no echo,...)
	newtio.c_lflag = 0;
	newtio.c_cc[VTIME]    = 0;   // inter-character timer unused
	newtio.c_cc[VMIN]     = 1;   // blocking read until 1 char received


	tcflush(fd, TCIFLUSH);

	if ( tcsetattr(fd,TCSANOW,&newtio) == -1) {
		perror("tcsetattr");
		exit(-1);
	}

	int mode = 0;
	if(strcmp("RECEIVER", argv[2]) == 0)
	mode = RECEIVER;
	else
	mode = TRANSMITTER;

	if(mode == TRANSMITTER)
		installAlarm();

/*	llopen(mode, fd);
	llclose(mode, fd);*/


	//TEST - DO NOT UNCOMMENT
	/*	if(mode == TRANSMITTER){
	char *test = malloc(2);
	test[0] = 'a';
	test[1] = 'b';
	llwrite(fd, test, 2,0);
	free(test);
}
else if(mode == RECEIVER){
char* test = malloc(20);
int size = llread(fd, test);
int t;
for(t= 0; t < size; t++){
printf("t: %x \n",(unsigned char)test[t]);
}
free(test);

}*/
/*
char *jesus = malloc(2);
jesus [0] = 0xF4;
jesus [1] = 0x7E;
int l;
printf("PRE Stuffing\n");
l= stuffing(jesus,2);

printf("Stuffing: SIZE: %d \n",l);

int t;
for(t= 0; t < l; t++){
printf("t: %x \n",(unsigned char)jesus[t]);
}

l = deStuffing(jesus,l);


printf("deStuffing: SIZE: %d \n",l);

for(t= 0; t < l; t++){
printf("t: %x \n",(unsigned char)jesus[t]);
}


free(jesus);
*/

// TEST SEND AND RECEIVE

char str[2] = "oi";

if(mode == TRANSMITTER){
	llwrite(fd, str, 2, 0);
}
else if(mode == RECEIVER){
	char *readBuffer = malloc(1);
	llread(fd, readBuffer, 0);
	free(readBuffer);
}

	sleep(2);

	if ( tcsetattr(fd,TCSANOW,&oldtio) == -1) {
		perror("tcsetattr");
		exit(-1);
	}

	close(fd);
	return 0;
}

int llopenTransmitter(int fd)
{
	int noResponse = 1;
	conta = 0;

	while(conta < 4 && noResponse != COMPLETE)
	{
		if(flag)
		{
			alarm(3);
			flag=0;
			createAndSendPackage(fd,SET_PACK);
		}
		char C;
		noResponse = receiveSupervision(fd,&C);
	}
	printf("Connection opened with success\n");
	return 0;
}

int llopenReceiver(int fd)
{
	flag = 0; //To dont break processing package loop
	char C;
	if(receiveSupervision(fd,&C) == COMPLETE)
	createAndSendPackage(fd,UA_PACK);
	printf("connection opened successfuly\n");
	return 0;
}

int llcloseTransmitter(int fd)
{
	int noResponse = 1;
	char C;
	conta = 0;

	while(conta < 4 && noResponse != COMPLETE)
	{
		if(createAndSendPackage(fd, DISC_PACK) == 5)
		{
			alarm(3);
			flag = 0;

			while(flag == 0 && noResponse != COMPLETE)
			noResponse = receiveSupervision(fd, &C);

		}
	}

	if(conta < 4){
		createAndSendPackage(fd,UA_PACK);//TODO NEED FIX SHOULD ONLY SEND ONCE
		printf("connection closed sucessfully\n");
		return 0;
	}
	printf("no success closing connection\n");
	return 1;
}

int llcloseReceiver(int fd)
{
	flag = 0; //To dont break processing package loop
	char C;
	if(receiveSupervision(fd,&C) == COMPLETE)
	createAndSendPackage(fd,DISC_PACK);

	if(receiveSupervision(fd,&C) == COMPLETE)
	{
		printf("connection closed successfully\n");
		return 0;
	}
	return 1;
}

int stuffing(char * package, int length){

	int size = length;
	int i;
	for(i = 0; i < length;i++){
		char oct = package[i];
		if(oct == F_FLAG || oct == ESC){
			size++;
		}
	}

	if(size == length)
	return size;

	package = realloc(package, size);

	for(i = 0; i < size; i++){
		char oct = package[i];
		if(oct == F_FLAG || oct == ESC) {
			memmove(package + i + 2, package + i+1, size - i);
			if (oct == F_FLAG) {
				package[i+1] = XOR_7E_20;
				package[i] = ESC ;
			}
			else package[i+1] = XOR_7D_20;
		}
	}
	return size;
}

int deStuffing( char * package, int length){
	int size = length;

	int i;
	for(i = 0; i < size; i++){
		char oct = package[i];
		if(oct == ESC)
		{
			if(package[i+1] == XOR_7E_20){
				package[i] = F_FLAG;
				memmove(package + i + 1, package + i + 2,length - i + 2);

			}
			else if(package[i+1] == XOR_7D_20){
				memmove(package + i + 1, package + i + 2,length - i + 2);
			}
			size--;
		}
	}
	package = realloc(package,size);
	return size;
}

char makeBCC2(char* message, int length){
	char bcc = 0;
	int i;
	for(i = 0; i < length; i++){
		bcc ^= message[i];
	}
	return bcc;
}

int receiveSupervision(int fd,char * C)
{
	int status = 0;
	char A;
	do
	{
		switch(status)
		{
			case START:
			status = receiveFlag(fd);
			break;
			case FLAG_RCV:
			status = receiveA(fd, &A);
			break;
			case A_RCV:
			status = receiveC(fd, C);
			break;
			case C_RCV:
			status = checkBCC(fd, A, *C);
			break;
			case BCC_RCV:
			status = receiveFlag(fd);
			if(status == FLAG_RCV)
			status = COMPLETE;
			//printf("full package!\n");}
			break;
		}

	}while(status != COMPLETE && flag == 0);
	//printf("Flag %d \n",flag);
	//printf("status %d \n",status);
	return status;
}

int receiveFlag(int fd)
{
	//printf("antes FLAG \n");
	char ch;
	read(fd, &ch, 1);

	//printf("Flag value: %x \n",ch);
	if(ch == F_FLAG)
	return FLAG_RCV;
	else
	return START;
}

int receiveA(int fd, char* ch)
{
	int res;
	res = read(fd, ch, 1);
	//printf("readA %x \n",*ch);
	if(res <= 0)
	return START;
	else if(*ch == F_FLAG)
	return FLAG_RCV;
	return A_RCV;
}

int receiveC(int fd, char* ch)
{
	int res;
	res = read(fd, ch, 1);
	//printf("receiveC %x \n",*ch);
	if(res <= 0)
	return START;
	else if(*ch == F_FLAG)
	return FLAG_RCV;
	return C_RCV;
}

int checkBCC(int fd, char A, char C)
{
	char ch;
	read(fd, &ch, 1);
	char expected = A ^ C;

	if(ch == expected)
	return BCC_RCV;
	else
	return START;
}

char* createSet()
{
	char* set = malloc(5 * sizeof(char));

	set[0] = F_FLAG;
	set[1] = A_EM;
	set[2] = C_SET;
	set[3] = A_EM ^ C_SET;
	set[4] = F_FLAG;

	return set;
}

char* createUA()
{
	char* UA = malloc(5*sizeof(char));
	UA[0] = F_FLAG;
	UA[1] = A_EM;
	UA[2] = C_UA;
	UA[3] = A_EM ^ C_UA;
	UA[4] = F_FLAG;

	return UA;
}

char* createDisc()
{
	char* DISC = malloc(5*sizeof(char));
	DISC[0] = F_FLAG;
	DISC[1] = A_EM;
	DISC[2] = C_DISC;
	DISC[3] = A_EM ^ C_DISC;
	DISC[4] = F_FLAG;

	return DISC;
}

char* createRR(int package)
{
	char* RR = malloc(5*sizeof(char));
	RR[0] = F_FLAG;
	RR[1] = A_EM;
	if(package == 0)
	RR[2] = C_RR0;
	else if(package == 1)
	RR[2] = C_RR1;
	RR[3] = RR[1] ^ RR[2];
	RR[4] = F_FLAG;
	return RR;
}

char* createREJ(int package)
{
	char* REJ = malloc(5*sizeof(char));
	REJ[0] = F_FLAG;
	REJ[1] = A_EM;
	if(package == 0)
	REJ[2] = C_REJ0;
	else if(package == 1)
	REJ[2] = C_REJ1;
	REJ[3] = REJ[1] ^ REJ[2];
	REJ[4] = F_FLAG;
	return REJ;
}


int createAndSendPackage(int fd,int type)
{
	char * msg;

	switch(type){
		case SET_PACK:
		msg = createSet();
		break;
		case UA_PACK:
		msg = createUA();
		break;
		case DISC_PACK:
		msg = createDisc();
		break;
		case RR_0PACK:
		msg = createRR(0);
		printf("RR0\n");
		break;
		case RR_1PACK:
		msg = createRR(1);
		printf("RR1\n");
		break;
		case REJ_0PACK:
		msg = createREJ(0);
		break;
		case REJ_1PACK:
		msg = createREJ(1);
		break;
	}

	int res = sendMensage(fd,msg,5);

	free(msg);
	return res;
}

int createStart(char *filename, int length, unsigned int size, int type,char *package){
	package = realloc(package, length+10);
	package [0] = type;
	package [1] = 0;
	package [2] = 0x04;

	int i = 0;
	while(size != 0){
		package[3 + i] = size % 255;
		size = size % 255;
		i++;
	}

	package [3+i] = 1;
	package [4+i] = length;

	int j;
	int l = 5+i;
	for ( j = 0; j < length; j++){
		int l = l+j;
		package[l] = filename[j];
	}
	package[l+length] = makeBCC2(package,length + l);


	return (length + l + 1);
}

int packagePayLoad(int C, int size, char * payload){
	int tramaSize = size + 5;
	char * buffer = malloc(tramaSize);
	buffer[0] = F_FLAG;
	buffer[1] = A_EM;
	buffer[2] = C;
	buffer[3] = buffer[1] ^ buffer[2];
	int i;
	for(i = 0; i < size; i++)
	buffer[i + 4] = payload[i];

	buffer[tramaSize-1] = F_FLAG;
	memcpy(payload, buffer, tramaSize);

	free(buffer);
	/*
	printf("TRAMA I size:%d\n",tramaSize);
	for(i = 0; i < size +4 ; i++){
	printf("t%d:%x\n",i,payload[i]);
}*/

return tramaSize;
}

int sendMensage(int fd, char *message, int length)
{
	int res  = 0;
	while(res <= 0){
		res=write(fd, message, length);
		//printf("sending... %d\n", res);
	}
	return res;
}

int checkRR_Reject(int C, char ch){
	if((C_RR1 == ch && C == 1) || (C_RR0 == ch && C==0))
	return COMPLETE;
	else
	return -1;
}

int extractPackage(char *package, char *trama,int length){
	// 5 is from F A C1 BBC1 |--| F
	int packageSize = length - 5;

	package = realloc(package,packageSize);

	memmove(package, trama + 4, packageSize);
	free(trama);

	int size = deStuffing(package,packageSize);

	return size;
}

int getTrama(int fd, char* trama){
	int size = 0;

	int flags = 0;
	int multi = 1;

	trama = realloc(trama, multi * TRAMA_SIZE);

	char ch;
	int res;
	printf("getting trama\n");
	while(flags < 2){
		res = read(fd,&ch,1);
		if( res > 0){
			printf("package cell -- ");
			if(ch == F_FLAG)
			flags++;

			size++;
			if(size > multi*TRAMA_SIZE)
			{
				multi *= 2;
				trama = realloc(trama, multi * TRAMA_SIZE);
			}
			trama[size - 1 ] = ch;
		}

	}

	trama = realloc(trama, size);

	//VALIDATE
	printf("\ntrama received\n");
	if((trama[1] ^ trama[2]) == trama[3]){
		printf("BCC1 CHECK: TRUE\n" );
		return size;
	}
	else{
		printf("BCC1 CHECK: FALSE\n" );
		return -1;
	}

}
