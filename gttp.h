/* All defs and constants */

#define HTTP_PORT 3000
#define BACKLOG 64
#define _GNU_SOURCE
#define MAX_EVENTS 100
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

struct evtrack_s {
  int evid;  // some running id
  int evbr;  // bytes read
  int evst;  // status for now use number
  int evfd;  // fd assoc w the event
  
};

typedef struct evtrack_s evtrack_t;


  // Add the sock_fd to it
  /*
       int epoll_ctl(int epfd, int op, int fd,struct epoll_event *_Nullable
     event);

       struct epoll_event {
           uint32_t      events;
           epoll_data_t  data;
       };

       union epoll_data {
           void     *ptr;
           int       fd;
           uint32_t  u32;
           uint64_t  u64;
       };

       typedef union epoll_data  epoll_data_t;
  */

  // Use the data member to store some useful info
  // For now just track fd.

  // typedef struct req_s {

  //   int fd; // file descriptor of the socket
  //   int br; // bytes read
  //   buffer of read bytes
  //   client IP address
  //   client socket

  // } req_t;

  // Alloc memory for that
  // req_t *req = (req_t *)malloc(sizeof(req_t));
  // req->fd = sock_fd;

        // addr,socklen_t *_Nullable restrict addrlen, int flags); here we can
        // use nonblocking connection socket. Then read/write must take that
        // into account int conn_sock = accept4(sock_fd,(struct sockaddr *)
        // &client_addr, &client_addr_len, SOCK_NONBLOCK);

        /*
           struct sockaddr_in6 {
             sa_family_t     sin6_family;   // AF_INET6
             in_port_t       sin6_port;     // port number
             uint32_t        sin6_flowinfo; // IPv6 flow information
             struct in6_addr sin6_addr;     // IPv6 address
             uint32_t        sin6_scope_id; // Scope ID (new in Linux 2.4)
         };

         struct in6_addr {
             unsigned char   s6_addr[16];   // IPv6 address
         };

         inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),s,maxlen);

        char name[INET6_ADDRSTRLEN];
        char port[10];
        getnameinfo(sa, sizeof(sa), name, sizeof(name), port, sizeof(port),
        NI_NUMERICHOST | NI_NUMERICSERV); printf("%s:%s\n", name, port);

        */

  /*
  sqlite3_prepare_v2(db, "select * from expenses", -1, &stmt, NULL);
  while (sqlite3_step(stmt) != SQLITE_DONE) {
    int i;
    int num_cols = sqlite3_column_count(stmt);

    for (i = 0; i < num_cols; i++) {
      switch (sqlite3_column_type(stmt, i)) {
      case (SQLITE3_TEXT):
        printf("%s, ", sqlite3_column_text(stmt, i));
        break;
      case (SQLITE_INTEGER):
        printf("%d, ", sqlite3_column_int(stmt, i));
        break;
      case (SQLITE_FLOAT):
        printf("%g, ", sqlite3_column_double(stmt, i));
        break;
      default:
        break;
      }
    }
    printf("\n");

  } // while loop of sqlite3

  sqlite3_finalize(stmt);

  sqlite3_close(db);
  */

