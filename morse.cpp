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
	{".-.-.-", '\xA4'},
	{".---", '\xA6'},
	{".--.-", '\xB0'},
	{"--.--", '\xB1'},
	{".-", '\xB2'},
	{"..-", '\xB3'},
	{"-.---", '\xB4'},
	{".-...", '\xB5'},
	{".-..", '\xB6'},
	{"-.-..", '\xB7'},
	{"...-", '\xB8'},
	{"-.--", '\xB9'},
	{"----", '\xBA'},
	{"-.-.-", '\xBB'},
	{"--.-.", '\xBC'},
	{"---.-", '\xBD'},
	{".---.", '\xBE'},
	{"---.", '\xBF'},
	{"-.", '\xC0'},
	{"..-.", '\xC1'},
	{".--.", '\xC2'},
	{".-.--", '\xC3'},
	{"..-..", '\xC4'},
	{".-.", '\xC5'},
	{"-.-.", '\xC6'},
	{"....", '\xC7'},
	{"--.-", '\xC8'},
	{"..--", '\xC9'},
	{"-...", '\xCA'},
	{"--..-", '\xCB'},
	{"--..", '\xCC'},
	{".", '\xCD'},
	{"-..", '\xCE'},
	{"-..-", '\xCF'},
	{"..-.-", '\xD0'},
	{"-", '\xD1'},
	{"-...-", '\xD2'},
	{"-..-.", '\xD3'},
	{".--", '\xD4'},
	{"-..--", '\xD5'},
	{"--", '\xD6'},
	{"...", '\xD7'},
	{"--.", '\xD8'},
	{"-.--.", '\xD9'},
	{"---", '\xDA'},
	{".-.-", '\xDB'},
	{"-.-", '\xDC'},
	{".-.-.", '\xDD'},
	{"..", '\xDE'},
	{"..--.", '\xDF'},
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
	{"...-.",	'\b'},
	{"-..---",	CHAR_SEND},
	{".-.-..",	CHAR_CLEAR},
	{NULL, '\0'}
};

void start_morse(){
	act_tsk(MORSE_TASK);
}

char morse_getc(){
	char input;
	rcv_dtq(INPUTCHAR_DTQ, (intptr_t*)(&input));
	return input;
}

static void decode_morse(const char sequence[], bool *is_prev_space){
	morse_entry *ptr = morse_table;

	while(ptr->sequence != NULL){
		if(strcmp(ptr->sequence, sequence) == 0){
			snd_dtq(INPUTCHAR_DTQ, (intptr_t)ptr->character);
			switch((intptr_t)ptr->character){
			case ' ':
			case '\b':
			case 0x03:
			case 0x04:
				*is_prev_space=true;
				break;
			default:
				*is_prev_space=false;
				break;
			}
		}

		ptr++;
	}
	return;
}

void morse_task(intptr_t exinf){
	FLGPTN ptn;
	SYSTIM prevtim, currtim;
	char sequence[MAX_CODE_LEN+1] = {NODATA}; //末尾は必ずNULL
	int seq_next_index = 0;
	bool is_prev_space = true;

	get_tim(&prevtim);

	while(true){
		//待ち解除時にフラグはクリアされる（生成時の属性で指定した）
		if(twai_flg(BUTTON_FLG, 0x3, TWF_ORW, &ptn, DOT_UNIT*3) == E_TMOUT){
			//間隔があいたので文字を区切る
			decode_morse(sequence, &is_prev_space);
			memset(sequence, NODATA, MAX_CODE_LEN);
			seq_next_index = 0;
			if(twai_flg(BUTTON_FLG, 0x3, TWF_ORW, &ptn, DOT_UNIT*4) == E_TMOUT){
				//次の単語
				if(!is_prev_space){
					snd_dtq(INPUTCHAR_DTQ, ' ');
					is_prev_space=true;
				}
				continue;
			}
		}

		get_tim(&currtim);
		SYSTIM gap;
		gap = currtim - prevtim;

		switch(ptn){
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
