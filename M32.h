#ifndef M32_H
#define M32_H

#include <stdlib.h>
#include <stdbool.h>

int X32Connect(char *ip_str, int port);

int generateAndSendMessageWithArgs(char* address, char* argtypes, char** args);
int generateAndSendMessage(char* address);
int getIntValue(char* address);
float getFloatValue(char* address);
char *getStringValue(char* address);
int sendIntValue(char *address, int value);
int sendFloatValue(char *address, float value);
int sendStringValue(char *address, char *value);

struct scribble_strip{
	char name[13];
	uint8_t icon; // 1-74
	uint8_t color; // 0-15
};

struct config{
	struct scribble_strip scribble;
	uint8_t source; // 0-64
};

struct delay{
	bool on;
	float time;
};

struct preamp{
	float trim;
	bool invert;
	bool hpon; // phantom or high pass?
	uint8_t hpslope; // {12,18,24};
	float hpf;
};

struct gate{
	bool on;
	uint8_t mode; // 0-4 {EXP2, EXP3, EXP4, GATE, DUCK}
	float thr;
	float range;
	float attack;
	float hold;
	float release;
	uint8_t keysrc; // 0-64
	bool filter_on;
	uint8_t filter_type; //0-8
	float filter_f;
};

struct dyn{
	bool on;
	uint8_t mode; // {COMP, EXP}
	uint8_t det; // {PEAK, RMS}
	uint8_t env; // {LIN, LOG}
	float thr;
	uint8_t ratio; // 0-11 {1.1, 1.3, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 7.0, 10, 20, 100}
	float knee;
	float mgain;
	float attack;
	float hold;
	float release;
	uint8_t pos; // {PRE, POST}
	uint8_t keysrc; // 0-64
	float mix;
	bool _auto;
	bool filter_on;
	uint8_t filter_type; // 0-8 {LC6, LC12, HC6, HC12, 1.0, 2.0, 3.0, 5.0, 10.0}
	float filter_f;
};

struct insert{
	bool on;
	uint8_t pos; // {PRE, POST}
	uint8_t sel; // 0-22 {OFF, FX1L, FX1R, FX2L, FX2R, FX3L, FX3R, FX4L, FX4R, FX5L, FX5R, FX6L, FX6R, FX7L, FX7R, FX8L, FX8R, AUX1, AUX2, AUX3, AUX4, AUX5, AUX6}
};

struct eq_band{
	uint8_t type; // 0-5 {LCut, LShv, PEQ, VEQ, HShv, HCut}
	float f;
	float g;
	float q; 
};

struct chan_eq{
	struct eq_band band_1;
	struct eq_band band_2;
	struct eq_band band_3;
	struct eq_band band_4;
};

struct mix{

};

struct channel{
	struct config config;
	struct delay delay;
	struct preamp preamp;
	struct gate gate;
	struct dyn dyn;
	struct insert insert;
	bool eq_on;
	struct chan_eq eq;
};



#endif