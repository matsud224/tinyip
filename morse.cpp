#include "arduino_app.h"
#include "mbed.h"
#include <kernel.h>

#include <cstring>

#include "morse.h"
#include "util.h"

#define NODATA '\0'
#define DOT '.'
#define DASH '-'

#define MAX_CODE_LEN 8 //符号の最大長
#define NULL 0

struct morse_entry{
	const char *sequence;
	char character;
};

static morse_entry morse_table[] = {
	{".-",		'A'},
	{"-...",	'B'},
	{"-.-.",	'C'},
	{"-..",		'D'},
	{".",		'E'},
	{"..-.",	'F'},
	{"--.",		'G'},
	{"....",	'H'},
	{"..",		'I'},
	{".---",	'J'},
	{"-.-",		'K'},
	{".-..",	'L'},
	{"--",		'M'},
	{"-.",		'N'},
	{"---",		'O'},
	{".--.",	'P'},
	{"--.-",	'Q'},
	{".-.",		'R'},
	{"...",		'S'},
	{"-",		'T'},
	{"..-",		'U'},
	{"...-",	'V'},
	{".--",		'W'},
	{"-..-",	'X'},
	{"-.--",	'Y'},
	{"--..",	'Z'},
	{".----",	'1'},
	{"..---",	'2'},
	{"...--",	'3'},
	{"....-",	'4'},
	{".....",	'5'},
	{"-....",	'6'},
	{"--...",	'7'},
	{"---..",	'8'},
	{"----.",	'9'},
	{"-----",	'0'},
	{".-.-.-",	'.'},
	{"--..--",	','},
	{"..--..",	'?'},
	{"-.-.--",	'!'},
	{"-....-",	'-'},
	{"-..-.",	'/'},
	{".--.-.",	'@'},
	{"-.--.",	'('},
	{"-.--.-",	')'},
	{NULL, '\0'}
};

static void decode_morse(const char sequence[]){
	morse_entry *ptr = morse_table;

	while(ptr->sequence != NULL){
		if(strcmp(ptr->sequence, sequence) == 0){
			snd_dtq(INPUTCHAR_DTQ, (intptr_t)ptr->character);
			break;
		}

		ptr++;
	}
}

void morse_task(intptr_t exinf){
	FLGPTN ptn;
	SYSTIM prevtim, currtim;
	char sequence[MAX_CODE_LEN+1] = {NODATA}; //末尾は必ずNULL
	int seq_next_index = 0;

	get_tim(&prevtim);

	while(true){
		//待ち解除時にフラグはクリアされる（生成時の属性で指定した）
		if(twai_flg(BUTTON_FLG, 0x3, TWF_ORW, &ptn, DELIM_UNIT) == E_TMOUT){
			//間隔があいたので文字を区切る
			goto delim;
		}

		get_tim(&currtim);
		SYSTIM gap;
		gap = currtim - prevtim;

		switch(ptn){
		case PTN_FALL:
			if(gap >= DELIM_UNIT){
				//文字の区切り
delim:
				decode_morse(sequence);
				memset(sequence, NODATA, MAX_CODE_LEN);
				seq_next_index = 0;
			}
			break;
		case PTN_RISE:
			if(seq_next_index >= MAX_CODE_LEN) break;
			if(gap <= DOT_UNIT){
				sequence[seq_next_index++] = DOT;
            }else{
            	sequence[seq_next_index++] = DASH;
            }
			break;
		}

		prevtim = currtim;
	}
}
