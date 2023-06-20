/*
 * X32UDP.c
 * (POSIX compliant version, Linux...)
 * Created on: June 2, 2015
 * Author: Patrick-Gilles Maillot
 *
 * Copyright 2015, Patrick-Gilles Maillot
 * This software is distributed under he GNU GENERAL PUBLIC LICENSE.
 *
 * This software allows connecting to a remote X32 or XAIR system using
 * UDP protocol; It provides a set of connect, send and receive functions.
 * The receive mode is non-blocking, i.e. a timeout enables returning from
 * the call even if no response is obtained by the server.
 *
 * Send and Receive buffers are provided by the caller. No provision is
 * made in this package to keep or buffer data for deferred action or
 * transfers.
 */
#include "M32.h"

#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>

#include <stdio.h>

#define BSIZE 512 // MAX receive buffer size
#define TIMEOUT 50 // default timeout

#define round4(x) ((x) + 3) & ~0x3

int X32Send(char *buffer, int length);
int X32Recv(char *buffer, int timeout);

int CONNECTION_STATE = 0;


struct sockaddr_in Xip;
struct sockaddr* Xip_addr = (struct sockaddr *)&Xip;
socklen_t Xip_len = sizeof(Xip); // length of addresses
int Xfd; // X32 socket
struct pollfd ufds;
int r_len, p_status; // length and status for receiving


/*

I want to associate a number with each node, to make selection of what to sync easier.
Probably best to index on each part of the slash, since some are explicitly numbered
0	config
	0	chlink
		[1-2, ..., 31-32]
	1	auxlink
		[1-2, ..., 7-8]
	2	fxlink
		[1-2, ..., 7-8]
	3	buslink
		[1-2, ..., 15-16]
	4	mtxlink
		[1-2, 3-4, 5-6]
	5	mute
		[1, ..., 6]
	6	linkcfg
		[hadly, eq, dyn, fdrmute]
	7	mono
		[mode, link]
	8	solo
	9	talk
	10	osc
	11	routing
		IN
			[1-8, ..., 25-32, AUX]
		AES50A
			[1-8, ..., 41-48]
		AES50B
			[1-8, ..., 41-48]
		CARD
			[1-8, ..., 25-32]
		OUT
			[1-4,...13-16]
	12	userctrl
		[A, B, C]
			color
			enc
				[1, ..., 4]
			btn
				[5, ..., 12]
	13	tape
		[gainL, gainR, autoplay]

0	ch
*/

typedef struct osc_node{
	char *label;
	int no_children;
	const struct osc_node *children;
} osc_node_t;


// Array of leafs which are sets of 8 numbers (1-8,9-16,etc)
// used in routing config
//      !!!!!!!!!!!! TODO NOT USED FOR IN cause it needs AUX
const osc_node_t eight_nums[] =
    {
        {"1-8",0,NULL},{"9-16",0,NULL},{"17-24",0,NULL},{"25-32",0,NULL},{"33-40",0,NULL},{"41-48",0,NULL}
    };

// Array of leafs which are sets of 4 numbers (1-4,5-8,etc)
// used in OUT config
const osc_node_t four_nums[] =
    {
        {"1-4",0,NULL},{"5-8",0,NULL},{"9-12",0,NULL},{"13-16",0,NULL}
    };

// Array of leafs which are 'linked' numbers (odd-even pairs up to 31-32)
// used in chlink, auxlink,buslink,mtxlink
const osc_node_t linked_nums[] =
    {
        {"1-2",0,NULL},{"3-4",0,NULL},{"5-6",0,NULL},{"7-8",0,NULL},{"9-10",0,NULL},{"11-12",0,NULL},{"13-14",0,NULL},{"15-16",0,NULL},
		{"17-18",0,NULL},{"19-20",0,NULL},{"21-22",0,NULL},{"23-24",0,NULL},{"25-26",0,NULL},{"27-28",0,NULL},{"29-30",0,NULL},{"31-32",0,NULL}
    };

// Array of leafs which are single numbers
// used in mutegroups,selection of channl/bus/aux/fx/etc.., and userctrl encoder/buttons
const osc_node_t single_nums[] =
    {
        {"1",0,NULL},{"2",0,NULL},{"3",0,NULL},{"4",0,NULL},{"5",0,NULL},{"6",0,NULL},
		{"7",0,NULL},{"8",0,NULL},{"9",0,NULL},{"10",0,NULL},{"11",0,NULL},{"12",0,NULL}
    };

// Array of leafs for config/linkcfg
const osc_node_t config_linkcfg[] =
    {
		{"hadly",0,NULL}, {"eq",0,NULL}, {"dyn",0,NULL}, {"fdrmute",0,NULL}
	};

// Array of leafs for config/mono
const osc_node_t config_mono[] =
    {
		{"mode",0,NULL}, {"link",0,NULL}
	};

// Array of leafs for config/solo
const osc_node_t config_solo[] =
    {
		{"level",0,NULL}, {"source",0,NULL}, {"sourcetrim",0,NULL}, {"chmode",0,NULL}, {"busmode",0,NULL}, {"dcamode",0,NULL}, {"exclusive",0,NULL}, {"followsel",0,NULL}, {"followsolo",0,NULL},
		{"dimatt",0,NULL}, {"dim",0,NULL}, {"mono",0,NULL}, {"delay",0,NULL}, {"delaytime",0,NULL}, {"masterctrl",0,NULL}, {"mute",0,NULL}, {"dimpfl",0,NULL}
	};

// Array of leafs for config/talk/{A/B}
const osc_node_t config_talk_AB[] =
    {
		{"level",0,NULL}, {"dim",0,NULL}, {"latch",0,NULL}, {"destmap",0,NULL}
	};

// Array of leafs for config/talk
const osc_node_t config_talk[] =
    {
		{"enable",0,NULL}, {"source",0,NULL}, {"A",4,config_talk_AB},{"B",4,config_talk_AB}
	};

// Array of leafs for config/osc
const osc_node_t config_osc[] =
    {
		{"level",0,NULL}, {"f1",0,NULL}, {"f2",0,NULL}, {"fsel",0,NULL}, {"type",0,NULL}, {"dest",0,NULL}
	};

// Array of leafs for config/routing/IN
const osc_node_t config_routing_IN[] =
    {
		{"1-8",0,NULL},{"9-16",0,NULL},{"17-24",0,NULL},{"25-32",0,NULL},{"AUX",0,NULL}
	};

// Array of leafs for config/routing
const osc_node_t config_routing[] =
    {
		{"IN",5,config_routing_IN}, {"AES50A",6,eight_nums}, {"AES50B",6,eight_nums}, {"CARD",4,eight_nums}, {"OUT",4,four_nums}
	};

// Array of leafs for config/usrctrl{A/B/C}
const osc_node_t config_userctrl_ABC[] =
    {
		{"color",0,NULL},{"enc",4,single_nums},{"btn",8,single_nums+4}
	};

// Array of leafs for config/userctrl
const osc_node_t config_userctrl[] =
    {
		{"A",3,config_userctrl_ABC},{"B",3,config_userctrl_ABC},{"C",3,config_userctrl_ABC}
	};

// Array of leafs for config/tape
const osc_node_t config_tape[] =
    {
		{"gainL",0,NULL},{"gainR",0,NULL},{"autoplay",0,NULL}
	};

const osc_node_t config[] =
	{
		{"chlink", 16, linked_nums},
		{"auxlink", 4, linked_nums},
		{"fxlink", 4, linked_nums},
		{"buslink", 8, linked_nums},
		{"mtxlink", 3, linked_nums},
		{"mute", 6, single_nums},
		{"linkcfg", 4, config_linkcfg},
		{"mono", 2, config_mono},
		{"solo", 17, config_solo},
		{"talk", 4, config_talk},
		{"osc", 6, config_osc},
		{"routing", 5, config_routing},
		{"usrctrl", 3, config_userctrl},
		{"tape", 3, config_tape}
	};

const osc_node_t top = {
	"config",
	14,
	config
};

void walkTree(char *string, int offset, const osc_node_t *node){
	int add = strlen(node->label) + 1;
	snprintf(string+offset, add+2, "/%s", node->label);
	
	if(node->no_children == 0){
		printf("%s\n", string);
	}

	for(int i = 0; i < node->no_children; i++){
		walkTree(string, offset + add, node->children + i);
	}
}


void printBuffer(char* buffer, int length){
	int i = 0;
	while (i < length) {
		if (buffer[i] < ' '){
			putchar('~');
		} else {
			putchar(buffer[i]);
		}
		i++;
	}
	putchar('\n');
}

/**
 * Parses the arguments out of a OSC response
 * Returns a malloced char** with the arguments in order.
 * Returns null if there are no arguments or on malloc failure
*/
char** parseArgs(char* buffer, int length){
	if(buffer == NULL || length <= 0){
		return NULL;
	}

	printf("\t%s\n", buffer);
	// have to look through manually cause string has nulls. ew.
	char* comma = NULL;
	for(int i = 0; i < length; i++){
		if(buffer[i] == ','){
			comma = buffer + i;
			break;
		}
	}

	if(comma == NULL){
		return NULL;
	}

	int argnum = strlen(comma) - 1;
	int offset = round4(strlen(comma) + 1);

	char** args = malloc(argnum * sizeof(char *));
	if(args == NULL){
		return NULL;
	}

	for(int i = 0; i < argnum; i++){
		printf("\targ %d: ", i);
		char type = comma[i + 1];
		if(type == 'i'){
			args[i] = malloc(4 * sizeof(char));
			if(args[i] == NULL){
				return NULL;
			}
			((int *)args[i])[0] = ntohl(*(int *)(comma + offset));
			offset += 4;

			printf("%i\n", ((int *)args[i])[0]);

		}else if(type == 'f'){
			args[i] = malloc(4 * sizeof(char));
			if(args[i] == NULL){
				return NULL;
			}
			((float *)args[i])[0] = *(float *)(comma + offset);
			offset += 4;

			printf("%f\n", ((float *)args[i])[0]);

		}else if(type == 's'){
			int str_len = strlen(comma + offset) + 1; // count ending null
			
			args[i] = malloc(str_len * sizeof(char));
			if(args[i] == NULL){
				return NULL;
			}
			strcpy(args[i], comma + offset);
			printf("%s\n", args[i]);

			offset += round4(str_len);
		}/*else if(type == 'b'){

		}*/
	}
	
	return args;
}

/**
 * Generates a message to send to the M32 given:
 * address: string representing the node to send the command to
 * argtypes: string with with the arg types in order (eg. "s", "ifff", "ss")
 * args: string array with the arguments to be used. Length of array must be equal to strlen(argtypes)
 * Args must be 4 bytes each
 * 
 * Returns response from X32Send
*/
int generateAndSendMessageWithArgs(char* address, char* argtypes, char** args){
	// Pad out address length
	int add_len = round4(strlen(address)+1); // add ending null then round

	// determine number of args and length of arg string
	int argnum = strlen(argtypes);
	int type_len = round4(2 + argnum); // add comma and ending null


	int args_len = 0;

	for(int i = 0; i < argnum; i++){
		char type = argtypes[i];
		if(type == 'i'){
			args_len += 4;
		}else if(type == 'f'){
			args_len += 4;
		}else if(type == 's'){
			int str_len = round4(strlen(args[i]) + 1); // count ending null

			args_len += str_len;
		}/*else if(type == 'b'){

		}*/
	}
	int message_len = add_len + type_len + args_len;

	char *message = malloc(message_len);
	if(message == NULL){
		return -1;
	}

	memset(message, 0, message_len);
	strcpy(message, address);
	message[add_len] = ',';
	strcpy(message + add_len + 1, argtypes);

	int offset = add_len + type_len;

	for(int i = 0; i < argnum; i++){
		char type = argtypes[i];
		if(type == 'i'){
			memcpy(message + offset, args[i], 4);
			offset += 4;
		}else if(type == 'f'){
			memcpy(message + offset, args[i], 4);
			offset += 4;
		}else if(type == 's'){
			strcpy(message + offset, args[i]);

			int str_len = round4(strlen(args[i]) + 1); // count ending null

			offset += str_len;
		}/*else if(type == 'b'){

		}*/
	}

	int res = X32Send(message, message_len);
	free(message);
	return res;
}

/**
 * Generates a message to send to the M32 with no arguments
 * address: string representing the node to send the command to
 * 
 * Returns response from X32Send
*/
int generateAndSendMessage(char* address){
	// Pad out address length
	int add_len = round4(strlen(address)+1); // add null

	int message_len = add_len+4;

	char *message = malloc(message_len);
	if(message == NULL){
		return -1;
	}

	memset(message, 0, message_len);
	strcpy(message, address);
	message[add_len] = ',';

	int res = X32Send(message, message_len);
	free(message);
	return res;
}

/**
 * Generates a message to send to the M32 with no arguments,
 * then attempts to receive an integer value back.
 * address: string representing the node to send the command to
 * 
 * Returns integer response, or -1 on failure (Console should never send -integer);
*/
int getIntValue(char* address){
	if(generateAndSendMessage(address)){
		char r_buf[BSIZE];
		r_len = X32Recv(r_buf, TIMEOUT);
		char **results = parseArgs(r_buf, r_len);
		if(results == NULL){
			return -1;
		}

		//convert int back to network order
		int res = ((int *)results[0])[0];
		free(results[0]);
		free(results);
		return res;
	}
	return -1;
}

/**
 * Generates a message to send to the M32 with one int argument
 * address: string representing the node to send the command to
 * int: host order integer to send as argument
 * 
 * Returns response from generateAndSendMessageWithArgs.
*/
int sendIntValue(char *address, int value){
	int32_t arg1 = htonl(value);
	char** args = malloc(1*sizeof(char*));
	args[0] = (char *) &arg1;

	int res = generateAndSendMessageWithArgs(address, "i", args);
	free(args);
	return res;
}

/**
 * Generates a message to send to the M32 with no arguments,
 * then attempts to receive a float value back.
 * address: string representing the node to send the command to
 * 
 * Returns float response, or -1 on failure (Console should never send -integer);
*/
float getFloatValue(char* address){
	if(generateAndSendMessage(address)){
		char r_buf[BSIZE];
		r_len = X32Recv(r_buf, TIMEOUT);
		char **results = parseArgs(r_buf, r_len);
		if(results == NULL){
			return -1;
		}

		//convert int back to network order
		int res = ((float *)results[0])[0];
		free(results[0]);
		free(results);
		return res;
	}
	return -1;
}

/**
 * Generates a message to send to the M32 with one float argument
 * address: string representing the node to send the command to
 * float: host order float to send as argument
 * 
 * Returns response from generateAndSendMessageWithArgs.
*/
int sendFloatValue(char *address, float value){
	float arg1 = value;
	char** args = malloc(1*sizeof(char*));
	args[0] = (char *) &arg1;

	int res = generateAndSendMessageWithArgs(address, "f", args);
	free(args);
	return res;
}

/**
 * Generates a message to send to the M32 with no arguments,
 * then attempts to receive a string value back.
 * address: string representing the node to send the command to
 * 
 * Returns malloced string response, or NULL on failure
*/
char *getStringValue(char* address){
	if(generateAndSendMessage(address)){
		char r_buf[BSIZE];
		r_len = X32Recv(r_buf, TIMEOUT);
		char **results = parseArgs(r_buf, r_len);
		if(results == NULL){
			return NULL;
		}

		//convert int back to network order
		char *res = results[0];
		free(results);
		return res;
	}
	return NULL;
}

/**
 * Generates a message to send to the M32 with one string argument
 * address: string representing the node to send the command to
 * string: null terminated string to send as argument
 * 
 * Returns response from generateAndSendMessageWithArgs.
*/
int sendStringValue(char *address, char *value){
	char* arg1 = value;
	char** args = malloc(1*sizeof(char*));
	args[0] = (char *) &arg1;

	int res = generateAndSendMessageWithArgs(address, "s", args);
	free(args);
	return res;
}

int getChannelName(int ch, char* r_buf){
	if(ch < 1 || ch > 32){
		return -1;
	}

	char addr[20];
	snprintf(addr, 20, "/ch/%02i/config/name", ch);
	if(generateAndSendMessage(addr) < 0){
		return -1;
	}

	int r_len = X32Recv(r_buf, TIMEOUT);
	return r_len;
}

int getChannelEq(int ch, int band, char* r_buf){
	if(ch < 1 || ch > 32){
		return -1;
	}

	char addr[13];
	snprintf(addr, 13, "/ch/%02i/eq/%i", ch, band);
	if(generateAndSendMessage(addr) < 0){
		return -1;
	}

	int r_len = X32Recv(r_buf, TIMEOUT);
	return r_len;
}

struct channel* getChannelInfo(int ch){
	struct channel* channel;

	if(ch < 1 || ch > 32){
		return NULL;
	}

	channel = malloc(sizeof(struct channel));
	if(channel == NULL){
		return NULL;
	}

	char addr[40];
	snprintf(addr, 40, "/ch/%02i", ch);
	char *base_addr = addr + 6;

	printf("Getting name\n");
	// Config values
	snprintf(base_addr, 34, "/config/name");
	char* str_res = getStringValue(addr);
	strcpy(channel->config.scribble.name, str_res);
	free(str_res);
	snprintf(base_addr, 34, "/config/icon");
	channel->config.scribble.icon = getIntValue(addr);
	snprintf(base_addr, 34, "/config/color");
	channel->config.scribble.color = getIntValue(addr);
	snprintf(base_addr, 34, "/config/source");
	channel->config.source = getIntValue(addr);

	// Delay
	snprintf(base_addr, 34, "/delay/on");
	channel->delay.on = getIntValue(addr);
	snprintf(base_addr, 34, "/delay/time");
	channel->delay.time = getFloatValue(addr);

	// Preamp/HPF
	snprintf(base_addr, 34, "/preamp/trim");
	channel->preamp.trim = getFloatValue(addr);
	snprintf(base_addr, 34, "/preamp/invert");
	channel->preamp.invert = getIntValue(addr);
	snprintf(base_addr, 34, "/preamp/hpon");
	channel->preamp.hpon = getIntValue(addr);
	snprintf(base_addr, 34, "/preamp/hpslope");
	channel->preamp.hpslope = getIntValue(addr);
	snprintf(base_addr, 34, "/preamp/hpf");
	channel->preamp.hpf = getFloatValue(addr);

	// Gate
	snprintf(base_addr, 34, "/gate/on");
	channel->gate.on = getIntValue(addr);
	snprintf(base_addr, 34, "/gate/mode");
	channel->gate.mode = getIntValue(addr);
	snprintf(base_addr, 34, "/gate/thr");
	channel->gate.thr = getFloatValue(addr);
	snprintf(base_addr, 34, "/gate/range");
	channel->gate.range = getFloatValue(addr);
	snprintf(base_addr, 34, "/gate/attack");
	channel->gate.attack = getFloatValue(addr);
	snprintf(base_addr, 34, "/gate/hold");
	channel->gate.hold = getFloatValue(addr);
	snprintf(base_addr, 34, "/gate/release");
	channel->gate.release = getFloatValue(addr);
	snprintf(base_addr, 34, "/gate/keysrc");
	channel->gate.keysrc = getIntValue(addr);

	snprintf(base_addr, 34, "/gate/filter/on");
	channel->gate.filter_on = getIntValue(addr);
	snprintf(base_addr, 34, "/gate/filter/type");
	channel->gate.filter_type = getIntValue(addr);
	snprintf(base_addr, 34, "/gate/filter/f");
	channel->gate.filter_f = getFloatValue(addr);

	return channel;
}

int copyChannelConfig(int chsrc, int chdst){
	char r_buf[BSIZE];

	if(chsrc < 1 || chsrc > 32 || chdst < 1 || chdst > 32){
		return -1;
	}
	char addr[30];

	// Copy name
	snprintf(addr, 30, "/ch/%02i/config/name", chsrc);
	if(generateAndSendMessage(addr) < 0){
		return -1;
	}

	int r_len = X32Recv(r_buf, TIMEOUT);
	char **results = parseArgs(r_buf, r_len);
	if(results == NULL){
		return -1;
	}

	snprintf(addr, 30, "/ch/%02i/config/name", chdst);
	if(generateAndSendMessageWithArgs(addr, "s", results) < 0){
		return -1;
	}
	free(results[0]);
	free(results);

	// Copy icon
	snprintf(addr, 30, "/ch/%02i/config/icon", chsrc);
	if(generateAndSendMessage(addr) < 0){
		return -1;
	}

	r_len = X32Recv(r_buf, TIMEOUT);
	results = parseArgs(r_buf, r_len);
	if(results == NULL){
		return -1;
	}

	//convert int back to network order
	((int *)results[0])[0] = htonl(((int *)results[0])[0]);

	snprintf(addr, 30, "/ch/%02i/config/icon", chdst);
	if(generateAndSendMessageWithArgs(addr, "i", results) < 0){
		return -1;
	}
	free(results[0]);
	free(results);

	// Copy color
	snprintf(addr, 30, "/ch/%02i/config/color", chsrc);
	if(generateAndSendMessage(addr) < 0){
		return -1;
	}

	r_len = X32Recv(r_buf, TIMEOUT);
	results = parseArgs(r_buf, r_len);
	if(results == NULL){
		return -1;
	}

	//convert int back to network order
	((int *)results[0])[0] = htonl(((int *)results[0])[0]);

	snprintf(addr, 30, "/ch/%02i/config/color", chdst);
	if(generateAndSendMessageWithArgs(addr, "i", results) < 0){
		return -1;
	}
	free(results[0]);
	free(results);

	return 0;
}

/*
Initialize communication with Console at the given ip/port

Returns:
    -3 on data send error
    -2 on socket creation error
    -1 on polling data error
    0 on connection timeout
    1 on validated connection
*/
int X32Connect(char *ip_str, int port) {
    char r_buf[128]; // receive buffer for /info command test
    char Info[8] = "/info"; // testing connection with /info request (X32, M32)
    //char Info[8] = "/xinfo"; // testing connection with /xinfo request (XR series)
    
    // Create UDP socket
    if ((Xfd = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		CONNECTION_STATE = 0;
        return -2; // An error occurred on socket creation
    }

    // Server sockaddr_in structure
    memset (&Xip, 0, sizeof(Xip)); // Clear structure
    Xip.sin_family = AF_INET; // Internet/IP
    Xip.sin_addr.s_addr = inet_addr(ip_str); // IP address
    Xip.sin_port = htons(port); // server port
    //
    // Prepare for poll() on receiving data
    ufds.fd = Xfd;
    ufds.events = POLLIN; //Check for normal data
    //
    // Validate connection by sending a /info command
    if (sendto (Xfd, Info, 8, 0, Xip_addr, Xip_len) < 0) {
		CONNECTION_STATE = 0;
        return (-3);
    }
    if ((p_status = poll (&ufds, 1, 100)) > 0) { // X32 sent something?
        r_len = recvfrom(Xfd, r_buf, 128, 0, 0, 0); // Get answer and
        if ((strncmp(r_buf, Info, 5)) == 0) { // test data (5 bytes)
			CONNECTION_STATE = 1;
            return 1; // Connected
        }
    } else if (p_status < 0) {
		CONNECTION_STATE = 0;
        return -1; // Error on polling (not connected)
    }
    // Not connected on timeout
	CONNECTION_STATE = 0;
    return 0;
}

/*
Searches for a console on all ports in a 255.255.255.0 subnet
*/
int Search(int port){
	struct sockaddr_in test_ip;
	struct sockaddr* test_ip_addr = (struct sockaddr *)&test_ip;
	socklen_t test_ip_len = sizeof(test_ip); // length of addresses
	int test_fd; // X32 socket
	struct pollfd test_ufds;
	//
	int test_r_len, test_p_status; // length and status for receiving


	char r_buf[128]; // receive buffer for /info command test
    char Info[8] = "/info"; // testing connection with /info request (X32, M32)
    //char Info[8] = "/xinfo"; // testing connection with /xinfo request (XR series)
    
    // Create UDP socket
    if ((test_fd = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        return -2; // An error occurred on socket creation
    }

	// Server sockaddr_in structure
	memset (&test_ip, 0, sizeof(test_ip)); // Clear structure
	test_ip.sin_family = AF_INET; // Internet/IP
	test_ip.sin_port = htons(port); // server port
	//
	// Prepare for poll() on receiving data
	test_ufds.fd = test_fd;
	test_ufds.events = POLLIN; //Check for normal data

	char ip_addr_str[14];
	strcpy(ip_addr_str, "192.168.0.");
	for(int i = 0; i < 256; i++) {
		ip_addr_str[10]= i/100 % 10 +48;
		ip_addr_str[11] = i/10 % 10 +48 ;
		ip_addr_str[12] = i%10 + 48;
		ip_addr_str[13] = '\0';

		//printf("%s ", ip_addr_str);

		test_ip.sin_addr.s_addr = inet_addr(ip_addr_str); // IP address

		//
		// Validate connection by sending a /info command
		if (sendto (test_fd, Info, 8, 0, test_ip_addr, test_ip_len) < 0) {
			// printf("%s: Error Sending\n", ip_addr_str);
			continue;
		}
		if ((test_p_status = poll (&test_ufds, 1, 50)) > 0) { // Console sent something?
			test_r_len = recvfrom(test_fd, r_buf, 128, 0, 0, 0); // Get answer and
			if ((strncmp(r_buf, Info, 5)) == 0) { // test data (5 bytes)
				printf("%s ", ip_addr_str);
				printBuffer(r_buf, test_r_len);
				printf("\n");
				continue;
			}
		} else if (test_p_status < 0) {
			// printf("%s: Error Polling\n", ip_addr_str);
			continue;
		}
		// Not connected on timeout
		// printf("%s: Timeout\n", ip_addr_str);
	}
	return 0;
}

/*
Sends a message to the connected Console
    buffer should be a char* with the data to send
    length should be the size of the buffer in bytes

Returns -1 on error, otherwise the length of data sent
*/
int X32Send(char *buffer, int length) {
	
	int ret = (sendto (Xfd, buffer, length, 0, Xip_addr, Xip_len));
	printf("SEND %d: ", ret);
	printBuffer(buffer, length);
	return ret;
} 

/*
Receives data from the Connected server
    buffer should be a char* of at least 512 bytes to read data into
    timeout is the time in ms to wait before failing
        0 always returns no data
        negative means infinite

Returns
    -1 on polling error
    0 on timeout
    otherwise the amount of data read
*/
int X32Recv(char *buffer, int timeout) {
	if ((p_status = poll (&ufds, 1, timeout)) > 0) { // Data in?
		int ret = recvfrom(Xfd, buffer, BSIZE, 0, 0, 0);// return length

		printf("RECV %d: ", ret);
		printBuffer(buffer, ret);

		return ret;
	} else if (p_status < 0) {
		return -1; //An error occurred on polling
	}
	return 0; // No error, timeout
}

////
// Test purpose only - comment when linking the package to an application
//
int main() {

	//Search(10023);

	char r_buf[512];
	char s_buf[] = {"/status\0"};
	int r_len = 0;
	int s_len = 0;
	int status;

	char c_buf[] = "/ch/01/config/name\0";

	//status = X32Connect("192.168.0.101", 10023);
	status = X32Connect("10.139.81.1", 10023);
	printf ("Connection status: %d\n", status);

	if (status) {
		s_len = X32Send(s_buf, 8);
		if (s_len) {
			r_len = X32Recv(r_buf, TIMEOUT);
		}

		struct channel *channel = getChannelInfo(1);
		FILE * file= fopen(channel->config.scribble.name, "wb");
		if (file != NULL) {
			fwrite(channel, sizeof(struct channel), 1, file);
			fclose(file);
		}
		
		// printf("\n");
		// s_len = X32Send(c_buf, 20);
		// if (s_len) {
		// 	r_len = X32Recv(r_buf, 100);
		// }

		// char* arg1 = "Chan1";
		// //int32_t arg2 = htonl(2);
		// char** args = malloc(1*sizeof(char*));
		// args[0] = arg1;
		// // args[0] = (char *) &arg2;

		// s_len = generateAndSendMessageWithArgs(c_buf, "s", args);
		// if (s_len) {
		// 	r_len = X32Recv(r_buf, 100);
		// 	printBuffer(r_buf,r_len);
		// }

		s_len = generateAndSendMessage(c_buf);
		if (s_len) {
			r_len = X32Recv(r_buf, TIMEOUT);
			printBuffer(r_buf,r_len);
		}

		// copyChannelConfig(1,5);
	}

	walkTree(r_buf, 0, &top);


	printf("\n");
	return 0;
} 