/* All defs and constants */

#define HTTP_PORT 3000
#define BACKLOG 128
#define _GNU_SOURCE
#define MAX_EVENTS 500
#define MAX_READ_BUFFER 1024
#define MAX_CONNS 100
#define KA_TIMEOUT 2

void hdlRead();
void hdlSend();
void hdl_sigint(int sig);
void prepSQL();
void deltimeout();

enum cnst {

 CREATED,
 READING,
 READ,
 SENDING,
 SENT,
 CLOSED  

};




