#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/coding.h>
#include <wolfssl/wolfcrypt/logging.h>
#include <wolfssl/ssl.h>
#include <wolfssl/options.h>
#include "mbed.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "twitter_keys.h"
#include "errnolist.h"
#include "netlib.h"
#include "util.h"
#include "nameresolver.h"

#define HTTPS_PORT 443
const char *TWITTER_STATUS_UPDATE_URI = "https://api.twitter.com/1.1/statuses/update.json";
const char *TWITTER_API_FQDN = "api.twitter.com";

struct param {
	const char *key;
	const char *value; //valueがNULLの場合はkeyが非NULLでもスキップされる
};

static bool is_unreserved_char(char c){
	return isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~';
}

char *percent_encode(const char *str){
	int after_len = strlen(str)+1; //エンコーディング後の長さ
	for(const char *orig=str; *orig!='\0'; orig++){
		if(!is_unreserved_char(*orig))
			after_len+=2;
	}
	char *encoded = new char[after_len];
	char *ptr=encoded;
	for(const char *orig=str; *orig!='\0'; orig++){
		if(is_unreserved_char(*orig)){
			ptr[0]=*orig;
			ptr++;
		}else{
			ptr[0]='%';
			sprintf(ptr+1, "%02X", *orig);
			ptr+=3;
		}
	}
	*ptr='\0';
	return encoded;
}

//application/x-www-form-urlencoded用
char *form_urlencode(const char *str){
	int after_len = strlen(str)+1; //エンコーディング後の長さ
	for(const char *orig=str; *orig!='\0'; orig++){
		if(!is_unreserved_char(*orig) && *orig!=' ')
			after_len+=2;
	}
	char *encoded = new char[after_len];
	char *ptr=encoded;
	for(const char *orig=str; *orig!='\0'; orig++){
		if(is_unreserved_char(*orig)){
			ptr[0]=*orig;
			ptr++;
		}else if(*orig==' '){
			ptr[0]='+';
			ptr++;
		}else{
			ptr[0]='%';
			sprintf(ptr+1, "%02X", *orig);
			ptr+=3;
		}
	}
	*ptr='\0';
	return encoded;
}

//署名作成用
//paramsのキーはアルファベット順になっている必要がある
//keyが非NULLでかつvalueがNULLの場合はスキップ
static char *make_sig_params_str(param params[]){
	int len = 0;
	for(param *ptr=params; ptr->key!=NULL; ptr++){
		if(ptr->value!=NULL)
			len += strlen(ptr->key)+2+strlen(ptr->value);
	}
	len++; //書き込みの時にちょっとはみ出るから余分に取る
    char *temp_str = new char[len];
    char *write_head = temp_str;
    for(param *ptr=params; ptr->key!=NULL; ptr++){
    	if(ptr->value!=NULL)
			write_head+=sprintf(write_head, "%s=%s&", ptr->key, ptr->value);
    }
    temp_str[len-2] = '\0';
    char *urlencoded = form_urlencode(temp_str);
    delete [] temp_str;
    return urlencoded;
}


static char *make_signature(param *params, const char *req_method, const char *req_uri){
	int keylen = strlen(TW_CONSUMER_SECRET)+strlen(TW_ACCESS_TOKEN_SECRET)+1;
	char *key = new char[keylen+1];
	sprintf(key, "%s&%s", TW_CONSUMER_SECRET, TW_ACCESS_TOKEN_SECRET);

	char *encoded_method= percent_encode(req_method);
	char *encoded_uri = percent_encode(req_uri);
	char *encoded_params = make_sig_params_str(params);

	int datalen = strlen(encoded_method)+strlen(encoded_uri)+strlen(encoded_params)+2;
	char *data = new char[datalen+1];
	sprintf(data, "%s&%s&%s", encoded_method, encoded_uri, encoded_params);

	delete [] encoded_params;
	delete [] encoded_method;
	delete [] encoded_uri;

	//LOG("key: %s\ndata: %s\n", key, data);

	Hmac hmac;
	byte hash[SHA_DIGEST_SIZE];
	if(wc_HmacSetKey(&hmac, SHA, (byte*)key, keylen) != 0){
		delete [] data;
		delete [] key;
		return NULL;
	}
	delete [] key;

	if(wc_HmacUpdate(&hmac, (byte*)data, datalen) != 0){
		delete [] data;
		return NULL;
	}
	delete [] data;

	if(wc_HmacFinal(&hmac, hash) != 0)
		return NULL;

	word32 base64_encoded_len;
	Base64_Encode(hash, SHA_DIGEST_SIZE, NULL, &base64_encoded_len);
	byte *base64_encoded = new byte[base64_encoded_len+1];
	if(Base64_Encode(hash, SHA_DIGEST_SIZE, base64_encoded, &base64_encoded_len) != 0){
		delete [] base64_encoded;
		return NULL;
	}
	base64_encoded[base64_encoded_len-1] = 0;
	return (char*)base64_encoded;
}

static void set_param_value(param params[], const char *key, const char *value){
	for(param *ptr=params; ptr->key!=NULL; ptr++){
		if(strcmp(ptr->key, key)==0){
			ptr->value=value;
			return;
		}
	}
}

static void delete_param_value(param params[], const char *key){
	for(param *ptr=params; ptr->key!=NULL; ptr++){
		if(strcmp(ptr->key, key)==0){
			if(ptr->value!=NULL) delete [] ptr->value;
			ptr->value=NULL;
			return;
		}
	}
}

static const char *get_param_value(param params[], const char *key){
	for(param *ptr=params; ptr->key!=NULL; ptr++){
		if(strcmp(ptr->key, key)==0){
			return ptr->value;
		}
	}
	return NULL;
}

static char *make_oauth_nonce(){
	char *str = new char[33]; //64bitの場合の最大桁数
	for(int i=0; i<4; i++)
		sprintf(&str[i*8], "%08x", rand());
	return str;
}

static char *make_oauth_timestamp(){
	char *str = new char[21]; //64bitの場合の最大桁数
	sprintf(str, "%l", time(NULL));
	return str;
}

static int sock_recv_callback(WOLFSSL* ssl, char *buf, int sz, void *ctx)
{
	return recv(*(int*)ctx, buf, sz, 0, TIMEOUT_NOTUSE);
}

static int sock_send_callback(WOLFSSL* ssl, char *buf, int sz, void *ctx)
{
	return send(*(int*)ctx, buf, sz, 0, TIMEOUT_NOTUSE);
}

static void wolfssl_logging_callback(const int loglevel, const char *const msg){
	LOG("%s", msg);
	logtask_flush(0);
	return;
}

int tweet(const char *status){
	int err;
	uint8_t server_addr[IP_ADDR_LEN];

	addrinfo *dnsres = NULL;
	if((err=getaddrinfo(TWITTER_API_FQDN, &dnsres))!=0 || dnsres==NULL)
		return err;

	memcpy(server_addr, dnsres->addr, IP_ADDR_LEN);

	static param params[] = {
		{"oauth_consumer_key", TW_CONSUMER_KEY},
		{"oauth_nonce", NULL},
		{"oauth_signature", NULL},
		{"oauth_signature_method", "HMAC-SHA1"},
		{"oauth_timestamp", NULL},
		{"oauth_token", TW_ACCESS_TOKEN},
		{"oauth_version", "1.0"},
		{"status", NULL},
		{NULL, NULL}
	};

	WOLFSSL_CTX *ctx;
	WOLFSSL *ssl;
	wolfSSL_Init();
	//wolfSSL_Debugging_ON();
	wolfSSL_SetLoggingCb(wolfssl_logging_callback);

	if((ctx = wolfSSL_CTX_new(wolfTLSv1_2_client_method())) == NULL){
		LOG("wolfSSL_CTX_new error");
		wolfSSL_Cleanup();
		return ETLS;
	}

	if((err=wolfSSL_CTX_load_verify_locations(ctx, NULL, "/sd/certs")) != SSL_SUCCESS){
		LOG("Can't load CA certificates(%d).", err);
		wolfSSL_CTX_free(ctx);
		wolfSSL_Cleanup();
		return ETLS;
	}
	//wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, 0);

	wolfSSL_SetIORecv(ctx, sock_recv_callback);
	wolfSSL_SetIOSend(ctx, sock_send_callback);

	int sock;
	sock = socket(SOCK_STREAM);
	if((err=connect(sock, server_addr, HTTPS_PORT, TIMEOUT_NOTUSE)) < 0){
		LOG("connect() failed(%d).", err);
		wolfSSL_CTX_free(ctx);
		wolfSSL_Cleanup();
		return ETLS;
	}

	if((ssl = wolfSSL_new(ctx)) == NULL){
		LOG("wolfSSL_new error");
		close(sock);
		wolfSSL_CTX_free(ctx);
		wolfSSL_Cleanup();
		return ETLS;
	}

	wolfSSL_SetIOReadCtx(ssl, (void*)&sock);
	wolfSSL_SetIOWriteCtx(ssl, (void*)&sock);

	wolfSSL_set_fd(ssl, sock);

	//送信をはじめる
	set_param_value(params, "status", percent_encode(status));
	set_param_value(params, "oauth_nonce", make_oauth_nonce());
	set_param_value(params, "oauth_timestamp", make_oauth_timestamp());

	char *signature = make_signature(params, "POST", TWITTER_STATUS_UPDATE_URI);
	delete_param_value(params, "status");
	if(signature==NULL){
		delete_param_value(params, "oauth_timestamp");
		delete_param_value(params, "oauth_nonce");
		close(sock);
		wolfSSL_CTX_free(ctx);
		wolfSSL_Cleanup();
		return ETLS;
	}
	set_param_value(params, "oauth_signature", signature);

	static char msghdr_buf[1024];
	char *bufptr = msghdr_buf;
	bufptr += sprintf(msghdr_buf, "POST %s HTTP/1.1\x0d\x0a"
								  "Authorization: OAuth ", TWITTER_STATUS_UPDATE_URI);
	for(param *ptr=params; ptr->key!=NULL; ptr++){
		if(ptr->value!=NULL){
			char *encoded=percent_encode(ptr->value);
			bufptr += sprintf(msghdr_buf, "%s=\"%s\"", ptr->key, percent_encode(ptr->value));
			if((ptr+1)->key!=NULL)
				bufptr += sprintf(msghdr_buf, ",");
			else
				bufptr += sprintf(msghdr_buf, "\x0d\x0a");
			delete [] encoded;
		}
	}
	delete_param_value(params, "oauth_signature");
	delete_param_value(params, "oauth_timestamp");
	delete_param_value(params, "oauth_nonce");

	char *st_encoded=form_urlencode(status);
	bufptr += sprintf(msghdr_buf, "Connection: close\x0d\x0a"
								  "Content-Type: application/x-www-form-urlencoded\x0d\x0a"
								  "Content-Length: %d\x0d\x0a\x0d\x0a"
								  "status=", strlen(st_encoded));
	int result = 0;

	int bufptr_len = strlen(bufptr);
	int stenc_len = strlen(st_encoded);
	if(wolfSSL_write(ssl, bufptr, bufptr_len)!=bufptr_len
		|| wolfSSL_write(ssl, st_encoded, stenc_len)!=stenc_len){

		LOG("wolfSSL_write() failed.");
		result = ETLS;
	}
	delete [] st_encoded;

	if(recv_line(sock, msghdr_buf, sizeof(msghdr_buf), 0, 1000)>0){
		char *httpver_str = strtok(msghdr_buf, " ");
		char *code_str = strtok(NULL, " ");
		int code = atoi(code_str);
		if(code/100 != 2)
			result = EFAIL; //200番台でない
	}else{
		result = EFAIL;
	}

	wolfSSL_free(ssl);
	close(sock);
	wolfSSL_CTX_free(ctx);
	wolfSSL_Cleanup();
	return result;
}
