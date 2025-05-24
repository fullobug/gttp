/*
 * For now no HTTPS
 * ONLY Linux
 * ONLY IPV6 (for fun)
 * Testing with send header back
 */

#include "gttp.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <sys/uio.h>

int shutDown = 1;
struct epoll_event *events;
int sock_fd;
//evtrack_t *mev;
int epfd;

sqlite3 *db;
sqlite3_stmt *updcn;
sqlite3_stmt *updbr;
sqlite3_stmt *selcn;
sqlite3_stmt *delcn;
sqlite3_stmt *inscn;
sqlite3_stmt *selcou;
sqlite3_stmt *selfd;
sqlite3_stmt *upclose;

char status_200[] = "HTTP/1.1 200 OK\r\n";
char status_500[] = "HTTP/1.1 500 Internal Server Error\r\n";
char cont_json[] = "Content-Type: text/json\r\n";
char cont_txt[] = "Content-Type: text/plain\r\n";

int main(int argc, char *argv[]) {

  (void)argc;
  (void)argv;

  prepSQL();


  // SIGNAL
  //
  struct sigaction act;

  memset(&act, '\0', sizeof(act));
  act.sa_handler = hdl_sigint;
  sigemptyset(&act.sa_mask);
  act.sa_flags = SA_RESTART;
  sigaction(SIGINT, &act, NULL);

  // int sock_fd = socket(AF_INET6, SOCK_STREAM, 0);
  // right here we can set the main socket as nonblocking.
  // ---------------------------------------------------------------socket
  sock_fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (sock_fd < 0) {
    perror("Socket");
    exit(-1);
  }

  /* Bind to socket */

  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr)); // cleanup memory area
  memcpy(&addr.sin6_addr, &in6addr_any, sizeof(in6addr_any));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(HTTP_PORT);

  int rc;
  // ---------------------------------------------------------------bind
  rc = bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr));
  if (rc < 0) {
    perror("Bind");
    exit(-1);
  }

  /* Listen */
  // ----------------------------------------listen
  rc = listen(sock_fd, BACKLOG);
  if (rc < 0) {
    perror("Listen");
    exit(-1);
  }

  // Create epoll instance
  epfd = epoll_create1(0);
  if (epfd == -1) {
    perror("Epoll:");
  }

  struct epoll_event event;
  int n; // connections caught by wait.
  int conns;
  struct sockaddr_in6 client_addr;
  socklen_t client_addr_len = sizeof(client_addr);
  // int ctr = 1;

  event.data.fd = sock_fd;

  event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
  int res = epoll_ctl(epfd, EPOLL_CTL_ADD, sock_fd, &event);
  if (res < 0) {
    if (errno == EEXIST) {
      printf("fd already registered\n");
    } else if (errno == EBADF) {
      printf("fd bad\n%d\n", sock_fd);
    } else if (errno == ENOMEM) {
      printf("no memory\n");
    } else if (errno == ENOSPC) {
      printf("enospc\n");
    }
    exit(-1);
  }

  // Wait for incoming conns
  // struct epoll_event *events =
  // 
  events = (struct epoll_event *)calloc(MAX_EVENTS, sizeof(struct epoll_event));

  while (shutDown == 1) {

    conns = epoll_wait(epfd, events, MAX_EVENTS, 500);

    if (conns == -1) {

      if (errno == EINTR) {
        // printf("epoll_wait interrupted by signal\n");
        exit(0); // since on signal

      } else {
        perror("Wait");
        exit(-1);
      }
    }

    // main connection processing loop - epoll wait
    for (n = 0; n < conns; n++) {

      int evfd = events[n].data.fd;
      
      if (evfd == sock_fd) {

        int conn_sock;
        // ----------------------------------------------------------------------------------accept
        while (1) {

          conn_sock = accept4(sock_fd, (struct sockaddr *)&client_addr,
                              &client_addr_len, SOCK_NONBLOCK);

          if (conn_sock == -1) {

            if (errno == EINTR) {
              printf("Accept interrupted by signal\n");
              exit(0);
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {

              break; // go to next connection in the for loop

            } else {

              perror("Accept");
              exit(-1);
            }
          }

          char name[INET6_ADDRSTRLEN];
          char port[10];

          getnameinfo((struct sockaddr *)&client_addr, sizeof(client_addr),
                      name, sizeof(name), port, sizeof(port),
                      NI_NUMERICHOST | NI_NUMERICSERV);

         printf("Accept: fd: %d; IP: %s; port:%s \n", conn_sock, name,
                 port);

          // printf("Aft accept: %ld\n",clock());
          // This IS same event variable as earlier
          // as the struct gets stored in the epoll table, guess it is oK.
          // The data member is the only one that is saved and returned.
          // removed EPOLLOUT from here (16/5)
          event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
          event.data.fd = conn_sock;

          // printf("Event addr 2 %p\n",(void *)&event);
          res = epoll_ctl(epfd, EPOLL_CTL_ADD, conn_sock, &event);
          if (res == -1) {
            perror("Add");
            exit(-1);
          }

          // increment the counter.
          // ctr++;

        } // while loop for accept

      } else {

        // fetch conn records from db
        int evfd = events[n].data.fd;
        
        // If existing, process read

        if (events[n].events & EPOLLRDHUP) {
          // close(evfd);
          printf("Closed due to RDHUP\n");
          // break;
          continue;
        }

        if (events[n].events & EPOLLERR) {
          close(evfd);
          printf("Closed due to ERR\n");
          break;
        }

        if (events[n].events & EPOLLIN) {

         // ds = time(NULL);
          hdlRead(evfd);

          hdlSend(evfd);

          // delete the fd from watch list
          int d4 = epoll_ctl(epfd, EPOLL_CTL_DEL, evfd, NULL);
          if (d4 == -1) {
             perror("ecd");
          }
          close(evfd);  // close socket
    
        }

      } // else case

    } // for loop

  } // while loop

} // int main ends here

void hdlRead(int evfd) {

  ssize_t br;
  uint8_t buffer[MAX_READ_BUFFER] = {0};

  // -----------------------------Loop for the recv
  while (1) {

    br = read(evfd, buffer, MAX_READ_BUFFER - 1);

    if (br < 0) {

      if (errno == EINTR) {
        printf("Read interrupted by signal!\n");
        exit(0); // since signal
      }

      if (errno == EAGAIN || errno == EWOULDBLOCK) {

        printf("Breaking due to EAGAIN!\n");
        break;

      } else {

        perror("Read");
        // free(events);
        exit(-1);
      }
    }

    if (br >= 0) {

     break;
    }

  } // while loop

  return;
}

void hdlSend(int evfd) {

  ssize_t bs;

  char cont_len[50];
  char http_response[1024];
  struct iovec vector[6] = {{ 0 }};
  
  // ssize_t bl;
  // ssize_t *bl2;
      
  const unsigned char *res2 = {0};
  // char *res2a;
  
  char json_size[30];

  int res1 = sqlite3_step(selcou);
  if (res1 == SQLITE_ROW) {

    // res2 is const unsigned char *
    // it is a zero terminated string
    // res2 = sqlite3_column_text(selcou, 0);
    res2 = sqlite3_column_text(selcou, 0);
    if (res2 == NULL) {
      perror("Null");
      exit(-1);
    } else {
      // size of output
      sprintf(json_size, "%ld", strlen((char *)res2));
      // sizeof gets size of ptr not content!
    }

    // printf("Size was: %s,%ld\n",json_size,strlen(json_size));

    // char res3[] = "[{\"cocou\":\"IN\",\"conm1\":\"India\",\"cocur\":\"INR\"}]";
        
    vector[0].iov_base = status_200;
    vector[0].iov_len = strlen(status_200);

    vector[1].iov_base = cont_json;
    vector[1].iov_len = strlen(cont_json);

    vector[2].iov_base = "Content-Length: ";
    vector[2].iov_len = 16;

    // bl = strlen((char *)res2);
    // bl2 = &bl;

    vector[3].iov_base = json_size;
    vector[3].iov_len = strlen(json_size); 

    // vector[3].iov_base = "46";
    // vector[3].iov_len = 2;

    vector[4].iov_base="\r\n\r\n"; // last line of header
    vector[4].iov_len = 4;

    // printf("%s\n", res2); // works ok.
    
    vector[5].iov_base = (unsigned char *)res2;
    vector[5].iov_len = strlen((char *)res2);

    // vector[5].iov_base = res3;
    // vector[5].iov_len = strlen(res3);

    // strcpy(http_response, status_200);
    // strcat(http_response, cont_json);
    // bl = strlen((char *)res2);
    // sprintf(cont_len, "Content-Length: %ld\r\n", bl);
    // strcat(http_response, cont_len);
    // strcat(http_response, "\r\n");
    // strcat(http_response, (char *)res2);
    // strcat(http_response,"\r\n");

  } else {

    // printf("Send 2: %ld\n",clock());
    
    const char *err = sqlite3_errmsg(db);
    // bl = strlen((char *)err);

    strcpy(http_response, status_500);
    strcat(http_response, cont_txt);
    // sprintf(cont_len, "Content-Length: %ld\r\n", bl);
    strcat(http_response, cont_len);
    strcat(http_response, "\r\n");
    strcat(http_response, (char *)err);
    // strcat(http_response,"\r\n");
  }


  // printf("Send 3: %ld\n",clock());
  // sqlite3_reset(selcou);

  // -----------------------------Loop for the send
  while (1) {

     // printf("Send 1: %ld\n",clock());
     // bs = send(evfd, http_response, strlen(http_response), MSG_NOSIGNAL);
     bs = writev(evfd,vector,6);
     // printf("Write: %ld\n", bs);
     
     // printf("Send 2: %ld\n",clock());  //takes about 70-100ms

    if (bs >= 0) {

     break;

    } else {

      if (errno == EINTR) {
        printf("Send interrupted by signal!\n");
        exit(0); // since signal
      }

      if (errno == EAGAIN || errno == EWOULDBLOCK) {

        // write state and break TBD
        printf("Breaking SEND aft EAGAIN!\n");
        break;
      }

      if (errno = EPIPE) {
        break;
      } else {
        perror("Send");
        exit(-1);
      }
    }

  // printf("Send 6: %ld\n",clock());
  
  } // while infinite

  // printf("Sent: conn#: %d; fd: %d; bytes: %ld \n", evt->evid, evt->evfd, bs);
  sqlite3_reset(selcou);
  return;
}

void hdl_sigint(int sig) {

  printf("Caught signal %d\n", sig);
  shutDown = 1;

  free(events);
  close(sock_fd); // main socket
  close(epfd);    // close the epoll fd
  sqlite3_finalize(selcou);
  sqlite3_close(db); // close the db.
  // free(events);
  printf("Closed everything!\n");

  // exit(0);
}

void prepSQL() {

  // Open Sqlite connection

  //   int rc = sqlite3_open_v2("cn.db", &db, SQLITE_OPEN_CREATE |
  //   need to consider the program execution directory!
  int sc = sqlite3_open_v2("db/gttp.db", &db,
                           SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE, NULL);
  if (sc == SQLITE_OK) {
    // printf("Opened SQlite DB!\n");
  } else {
    perror("SQlite");
  }

  // truncate table at start
  sqlite3_stmt *crtbl;
  sqlite3_prepare_v2(db, "delete from conns", -1, &crtbl, NULL);
  if (sqlite3_step(crtbl) != SQLITE_DONE) {
    perror("Truncate tbl");
  }
  sqlite3_finalize(crtbl);

  // JSON Select 1
  int res_selcou =
      sqlite3_prepare_v2(db,
                         "select "
                         "json_group_array(json_object('cocou',cocou,'conm1',"
                         "conm1,'cocur',cocur)) from countries",
                         -1, &selcou, NULL);
  if (res_selcou != SQLITE_OK) {
    printf("Error in country select\n");
  }

} // end prepSQL
