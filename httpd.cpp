#include "arduino_app.h"
#include "mbed.h"
#include "SDFileSystem.h"

#include <cstring>

#include "netconf.h"
#include "netlib.h"
#include "util.h"

#define HTTP_BUF_LEN 1024

//SDFileSystem sd(P8_5, P8_6, P8_3, P8_4, "sd");

struct content_type_entry{
	const char *extension;
	const char *content_type;
};

const char *DOCUMENT_ROOT = "/sd/public_html/";
const char *ERROR_ROOT = "/sd/error_html/";

static content_type_entry content_type_dict[] = {
	{"txt", "text/plain"},
	{"csv", "text/csv"},
	{"htm", "text/html"},
	{"html", "text/html"},
	{"css", "text/css"},
	{"js", "text/javascript"},
	{"pdf", "application/pdf"},
	{"zip", "application/zip"},
	{"jpg", "image/jpeg"},
	{"jpeg", "image/jpeg"},
	{"png", "image/png"},
	{"gif", "image/gif"},
	{"bmp", "image/bmp"},
	{"mp3", "audio/mpeg"},
	{"mp4", "audio/mp4"},
	{NULL, "text/plain"}, //該当しないものはテキストとして送る
};

void start_httpd(){
	act_tsk(HTTPD_TASK);
}

static const char *get_content_type(const char *extension){
	content_type_entry *ptr = content_type_dict;
	while(ptr->extension != NULL && strcmp(ptr->extension, extension) != 0)
		ptr++;
	return ptr->content_type;
}


static int http_respond(int s, const char *st_code_str, const char *cont_str, const char *path){
	static char buf[HTTP_BUF_LEN];
	//sprintf使うと改行コード改変される
	static char httpver[] = "HTTP/1.1 ";
	static char hdr1[] = "\r\nConnection: close\r\n"
						 "Content-type: ";

	send(s, httpver, sizeof(httpver)-1, 0, TIMEOUT_NOTUSE);
	send(s, st_code_str, strlen(st_code_str), 0, TIMEOUT_NOTUSE);
	send(s, hdr1, sizeof(hdr1)-1, 0, TIMEOUT_NOTUSE);
	send(s, cont_str, strlen(cont_str), 0, TIMEOUT_NOTUSE);
	send(s, "\r\n\r\n", 4, 0, TIMEOUT_NOTUSE);

	FILE *fp = fopen(path, "rb");
	if(fp == NULL){
		return -1;
	}else{
		int readlen;
		while((readlen = fread(buf, 1, HTTP_BUF_LEN, fp)) > 0)
			if(send(s, buf, readlen, 0, TIMEOUT_NOTUSE)<0){
				break;
			}
	}
	fclose(fp);

	return 0;
}


void httpd_task(intptr_t exinf){
	LOG("httpd start");

    uint8_t clientaddr[IP_ADDR_LEN];
    uint16_t clientport;
    static char buf[512];
    static char path_buf[256];
    int s = socket(SOCK_STREAM);
    bind(s, 80);

    if(listen(s, 10)<0){
		LOG("httpd: listen() failed.");
		return;
    }

    while(true){
		int s2;

		if((s2=accept(s, clientaddr, &clientport, TIMEOUT_NOTUSE))<0){
			LOG("httpd: accept() failed.");
			continue;
		}

		if(recv(s2, buf, sizeof(buf), 0, 1000)>0){
			char *method = strtok(buf, " ");
			if(strncmp(method, "GET", 3) == 0){
				char *path = strtok(NULL, " ");
				if(strcmp("/", path) == 0){
					path = "/index.html";
				}
				sprintf(path_buf, "%s%s", DOCUMENT_ROOT, path+1);
				//LOG("httpd: method:%s, path:%s", method, path_buf);
				FILE *fp = fopen(path_buf, "rb"); //pathに1加えることで先頭の/を取り除く
				if(fp == NULL){
					sprintf(path_buf, "%s%s", ERROR_ROOT, "404.html");
					http_respond(s2, "400 Not Found", get_content_type("html"), path_buf);
				}else{
					char *ext; //拡張子を探す(手抜きだが、はじめに見つかったドット以降を拡張子とする)
					for(ext = path_buf+1; !(*ext == '\0' || *ext == '.'); ext++);
					if(*ext!=NULL)
						ext++;
					http_respond(s2, "200 OK", get_content_type(ext), path_buf);
				}
			}
		}
		close(s2);
    }
}
