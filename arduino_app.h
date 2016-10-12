#ifndef _APP_H_
#define _APP_H_

#include <stdint.h>

#define ADDITIONAL_LOOP_NUM 0 /* number of additional loops */

/*
 *  Definition of priority and stack size
 *
 *  Define priority and stack size for each task used in this
 *  application. These definitions are refered from system the
 *  confguration file (.cfg).
 */
#ifndef MAIN_TASK_PRI
#define MAIN_TASK_PRI  5
#endif /* MAIN_TASK_PRI */

#ifndef MAIN_TASK_STACK_SIZE
#define MAIN_TASK_STACK_SIZE 1024
#endif  /* MAIN_TASK_STACK_SIZE */

#ifndef USER_TASK_PRI
#define USER_TASK_PRI  5
#endif /* USER_TASK_PRI */

#ifndef USER_TASK_STACK_SIZE
#define USER_TASK_STACK_SIZE (1024*32)
#endif  /* USER_TASK_STACK_SIZE */

#ifndef ETHERRECV_TASK_PRI
#define ETHERRECV_TASK_PRI  5
#endif /* ETHERRECV_TASK_PRI */

#ifndef ETHERRECV_TASK_STACK_SIZE
#define ETHERRECV_TASK_STACK_SIZE 1024
#endif  /* ETHERRECV_TASK_STACK_SIZE */

#ifndef TIMEOUT_10SEC_TASK_PRI
#define TIMEOUT_10SEC_TASK_PRI  5
#endif /* TIMEOUT_10SEC_TASK_PRI */

#ifndef TIMEOUT_10SEC_TASK_STACK_SIZE
#define TIMEOUT_10SEC_TASK_STACK_SIZE 1024
#endif  /* TIMEOUT_10SEC_TASK_STACK_SIZE */

#ifndef TCP_TIMER_TASK_PRI
#define TCP_TIMER_TASK_PRI  5
#endif /* TCP_TIMER_TASK_PRI */

#ifndef TCP_TIMER_TASK_STACK_SIZE
#define TCP_TIMER_TASK_STACK_SIZE 1024
#endif  /* TCP_TIMER_TASK_STACK_SIZE */

#ifndef TCP_SEND_TASK_PRI
#define TCP_SEND_TASK_PRI  5
#endif /* TCP_SEND_TASK_PRI */

#ifndef TCP_TIMER_SEND_STACK_SIZE
#define TCP_SEND_TASK_STACK_SIZE 1024
#endif  /* TCP_SEND_TASK_STACK_SIZE */

#ifndef HTTPD_TASK_PRI
#define HTTPD_TASK_PRI  5
#endif /* HTTPD_TASK_PRI */

#ifndef HTTPD_STACK_SIZE
#define HTTPD_TASK_STACK_SIZE 4096
#endif  /* HTTPD_TASK_STACK_SIZE */

#ifndef MORSE_TASK_PRI
#define MORSE_TASK_PRI  5
#endif /* MORSE_TASK_PRI */

#ifndef MORSE_STACK_SIZE
#define MORSE_TASK_STACK_SIZE 4096
#endif  /* MORSE_TASK_STACK_SIZE */

#ifndef DHCLIENT_TASK_PRI
#define DHCLIENT_TASK_PRI  5
#endif /* DHCLIENT_TASK_PRI */

#ifndef DHCLIENT_STACK_SIZE
#define DHCLIENT_TASK_STACK_SIZE 4096
#endif  /* DHCLIENT_TASK_STACK_SIZE */

#ifndef SNTPCLIENT_TASK_PRI
#define SNTPCLIENT_TASK_PRI  5
#endif /* SNTPCLIENT_TASK_PRI */

#ifndef SNTPCLIENT_STACK_SIZE
#define SNTPCLIENT_TASK_STACK_SIZE 1024
#endif  /* SNTPCLIENT_TASK_STACK_SIZE */

/*
 *  Definition of memory size for RTOS
 *
 *  Define size of memory which used by RTOS at runtime.
 */

#ifndef KMM_SIZE
#define	KMM_SIZE	(MAIN_TASK_STACK_SIZE * 16)
#endif /* KMM_SIZE */

/*
 *  Declaration of handlers and tasks
 *
 *  When you define a new handler/task in .cfg, add its prototype
 *  declaration here.
 */

#ifdef __cplusplus
extern "C" {
#endif

extern void	cyclic_handler(intptr_t exinf);
extern void main_task(intptr_t exinf);
extern void user_task(intptr_t exinf);
extern void etherrecv_task(intptr_t exinf);
extern void timeout_10sec_task(intptr_t exinf);
extern void timeout_10sec_cyc(intptr_t exinf);
extern void tcp_timer_task(intptr_t exinf);
extern void tcp_timer_cyc(intptr_t exinf);
extern void tcp_send_task(intptr_t exinf);
extern void tcp_send_cyc(intptr_t exinf);
extern void httpd_task(intptr_t exinf);
extern void morse_task(intptr_t exinf);
extern void dhclient_task(intptr_t exinf);
extern void dhclient_alarm_handler(intptr_t exinf);
extern void sntpclient_task(intptr_t exinf);


#ifdef __cplusplus
}
#endif

#endif /* _APP_H_ */
