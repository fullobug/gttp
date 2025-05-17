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

int shutDown = 1;
struct epoll_event *events;
int sock_fd;
evtrack_t *mev;
int epfd;

sqlite3 *db;
sqlite3_stmt *updcn;
sqlite3_stmt *updbr;
sqlite3_stmt *selcn;
sqlite3_stmt *delcn;
sqlite3_stmt *inscn;
sqlite3_stmt *selcou;
sqlite3_stmt *selfd;

const char status_200[] = "HTTP/1.1 200 OK\r\n";
const char status_500[] = "HTTP/1.1 500 Internal Server Error\r\n";
const char cont_json[] = "Content-Type: text/json\r\n";
const char cont_txt[] = "Content-Type: text/plain\r\n";

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
  int n, conns; // connections caught by wait.
  struct sockaddr_in6 client_addr;
  socklen_t client_addr_len = sizeof(client_addr);
  int ctr = 1;

  event.data.u64 = 9999; 

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
  events =
      (struct epoll_event *)calloc(MAX_EVENTS, sizeof(struct epoll_event)); //

  while (shutDown == 1) {

    printf("Start wait\n");
    conns = epoll_wait(epfd, events, MAX_EVENTS, 2000);

    if (conns == -1) {

      if (errno == EINTR) {
        // printf("epoll_wait interrupted by signal\n");
        exit(0); // since on signal

      } else {
        perror("Wait");
        exit(-1);
      }
    }

    printf("No of conns: %d\n", conns);

    // main connection processing loop - epoll wait
    for (n = 0; n < conns; n++) {

      printf("Handling conn# %d\n", n);
      int evid = events[n].data.u64;
      printf("EVID was #%d\n",evid);

      if (evid == 9999) {

        // new customer!

        // ----------------------------------------------------------------------------------accept
        int conn_sock = accept4(sock_fd, (struct sockaddr *)&client_addr,
                                &client_addr_len, SOCK_NONBLOCK);

        if (conn_sock == -1) {

          if (errno == EINTR) {
            printf("Accept interrupted by signal\n");
            exit(0);
          }

          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            continue; // go to next connection in the for loop

          } else {

            perror("Accept");
            exit(-1);
          }
        }

        char name[INET6_ADDRSTRLEN];
        char port[10];

        getnameinfo((struct sockaddr *)&client_addr, sizeof(client_addr), name,
                    sizeof(name), port, sizeof(port),
                    NI_NUMERICHOST | NI_NUMERICSERV);

        // insert connection into db
        time_t t1 = time(NULL); // stores current time
        sqlite3_bind_int64(inscn, 1, (sqlite3_int64)ctr);      // cnid
        sqlite3_bind_int64(inscn, 2, (sqlite3_int64)conn_sock); // cnfd
        sqlite3_bind_int64(inscn, 3, (sqlite3_int64)CREATED);   // cnst
        sqlite3_bind_int64(inscn, 4, (sqlite3_int64)t1);        // cndt

        if (sqlite3_step(inscn) != SQLITE_DONE) {
          perror("Ins");
        } else {
          // printf("Inserted!\n");
        }
        sqlite3_reset(inscn);

        printf("Accept: fd: %d; IP: %s; port:%s ctr: %d\n", conn_sock, name, port, ctr);
        
        // This IS same event variable as earlier
        // as the struct gets stored in the epoll table, guess it is oK.
        // The data member is the only one that is saved and returned.
        // removed EPOLLOUT from here (16/5)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        event.data.u64 = ctr;

        // printf("Event addr 2 %p\n",(void *)&event);
        res = epoll_ctl(epfd, EPOLL_CTL_ADD, conn_sock, &event);
        if (res == -1) {
          perror("Add");
          exit(-1);
        }

        // increment the counter.
        ctr++;
        
      } else {

        // existing connection
        printf("Existing conn ID# %d\n", evid);

        // fetch conn records from db
        int evfd;
        int b2 = sqlite3_bind_int64(selfd, 1, evid); // cnid
        if (b2 != SQLITE_OK) {
          printf(sqlite3_errmsg(db));
        }
       
        while (sqlite3_step(selfd) != SQLITE_DONE) {
          evfd = sqlite3_column_int(selfd, 0);
        } // should be only 1 record anyway

        sqlite3_reset(selfd);

        printf("Conn ID# %d has fd of %d\n",evid, evfd);
        
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

          printf("Despatching to read!\n");
          hdlRead(evfd,evid);
          hdlSend(evfd,evid);
       }


        // printf("evfd is %d \n", evfd);
    
       // int d4 = epoll_ctl(epfd, EPOLL_CTL_DEL, evfd, NULL);
       // if (d4 == -1) {
       // if (errno != ENOENT) {
        // ignoring ENOENT since it may hv been deleted earlier aft send.
        // perror("ecd");
       // }
       // }

      } // else case

    } // for loop

    
  } // while loop

  deltimeout();
  // free(events);

} // int main ends here

void hdlRead(int evfd, int evid) {

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

        // at this point just assume nothing more to read
        // revisit this later.
        
        // write state and break not continue.
        time_t tr = time(NULL); // stores current time
        sqlite3_bind_int64(updcn, 1, (sqlite3_int64)evid); // cnid
        // sqlite3_bind_int64(updcn, 2, (sqlite3_int64)evfd); // cnfd
        sqlite3_bind_int64(updcn, 2, (sqlite3_int64)READ); // cnst
        sqlite3_bind_int64(updcn, 3, (sqlite3_int64)tr);        // cnupdt
        if (sqlite3_step(updcn) != SQLITE_DONE) {
          perror("Upd");
        } else {
          // printf("Updated!\n");
        }
        sqlite3_reset(updcn);

        printf("Breaking due to EAGAIN!\n");
        break;

      } else {

        perror("Read");
        // free(events);
        exit(-1);
      }
    }

    if (br >= 0) {

      // update the time for that connection.
      time_t tr = time(NULL); // stores current time
      sqlite3_bind_int64(updbr, 1, (sqlite3_int64)evid); // cnid
      sqlite3_bind_int64(updbr, 2, (sqlite3_int64)br); // cnbr (replacing for now)
      sqlite3_bind_int64(updbr, 3, (sqlite3_int64)READING); // cnst
      sqlite3_bind_int64(updbr, 4, (sqlite3_int64)tr);        // cnupdt
      if (sqlite3_step(updbr) != SQLITE_DONE) {
        perror("Upd");
      } else {
        // printf("Updated conn id %d br: %ld status: %d tr: %ld \n",evid,br,READING,tr);
      }
      sqlite3_reset(updbr);
      printf("Breaking aft read!\n");
      break;
    }

  } // while loop

  return;
}

void hdlSend(int evfd, int evid) {

  // printf("Sending: %d to conn# %d\n", evt->evfd, evt->evid);
  ssize_t bs;

  char cont_len[50];
  char http_response[1024];

  ssize_t bl;
  const unsigned char *res2;

  int res1 = sqlite3_step(selcou);
  if (res1 == SQLITE_ROW) {

    res2 = sqlite3_column_text(selcou, 0);
    if (res2 == NULL) {
      perror("Null");
      exit(-1);
    }

    strcpy(http_response, status_200);
    strcat(http_response, cont_json);
    bl = strlen((char *)res2);
    sprintf(cont_len, "Content-Length: %ld\r\n", bl);
    strcat(http_response, cont_len);
    strcat(http_response, "\r\n");
    strcat(http_response, (char *)res2);
    // strcat(http_response,"\r\n");

  } else {

    const char *err = sqlite3_errmsg(db);
    bl = strlen((char *)err);

    strcpy(http_response, status_500);
    strcat(http_response, cont_txt);
    sprintf(cont_len, "Content-Length: %ld\r\n", bl);
    strcat(http_response, cont_len);
    strcat(http_response, "\r\n");
    strcat(http_response, (char *)err);
    // strcat(http_response,"\r\n");
  }

  sqlite3_reset(selcou);

  // -----------------------------Loop for the send
  while (1) {

    bs = send(evfd, http_response, strlen(http_response), MSG_NOSIGNAL);

    if (bs >= 0) {
      
      // update the time for this connection.

      time_t ts = time(NULL); // stores current time
      sqlite3_bind_int64(updcn, 1, (sqlite3_int64)evid); // cnid
      sqlite3_bind_int64(updcn, 2, (sqlite3_int64)SENT); // cnst
      sqlite3_bind_int64(updcn, 3, (sqlite3_int64)ts);   // cnupdt
      if (sqlite3_step(updcn) != SQLITE_DONE) {
        perror("Upd");
      } else {
        // printf("Updated!\n");
      }
      sqlite3_reset(updcn);

      printf("Breaking SEND normally!\n");
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

  } // while infinite

  // printf("Sent: conn#: %d; fd: %d; bytes: %ld \n", evt->evid, evt->evfd, bs);
  return;
}

void hdl_sigint(int sig) {

  printf("Caught signal %d\n", sig);
  shutDown = 1;

  close(sock_fd); // main socket
  close(epfd);    // close the epoll fd
  sqlite3_finalize(selcn);
  sqlite3_finalize(selfd);
  sqlite3_finalize(inscn);
  sqlite3_finalize(updcn);
  sqlite3_finalize(updbr);
  sqlite3_finalize(delcn);
  sqlite3_finalize(selcou);
  sqlite3_close(db); // close the db.
  free(events);
  printf("Closed everything!\n");

  // exit(0);
}

void deltimeout() {

  // Delete all records past timeout
  // -------------------------------------------------------------------------------------

  time_t tq = time(NULL); // stores current time
  tq -= KA_TIMEOUT;
 
  int b1 = sqlite3_bind_int64(selcn, 1, (sqlite3_int64)tq); // time
  if (b1 != SQLITE_OK) {
    printf(sqlite3_errmsg(db));
  }

  while (sqlite3_step(selcn) != SQLITE_DONE) {

    int delfd = sqlite3_column_int(selcn, 0);
    int d4 = epoll_ctl(epfd, EPOLL_CTL_DEL, delfd, NULL);
    if (d4 == -1) {
       perror("ecd");
    }

    close(delfd);

  } // while loop of sqlite3

  sqlite3_reset(selcn);

}

void prepSQL() {

  // Open Sqlite connection

  //   int rc = sqlite3_open_v2("cn.db", &db, SQLITE_OPEN_CREATE |
  //   SQLITE_OPEN_READWRITE | SQLITE_OPEN_MEMORY );
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

  // Insert statement 1
  int res_ins = sqlite3_prepare_v2(
      db, "insert into conns(cnid,cnfd,cnst,cncrdt) values(?1,?2,?3,?4)", -1,
      &inscn, NULL);
  if (res_ins != SQLITE_OK) {
    printf("Error in insert stmt\n");
  }

  // Select statement 1
  int res_sel = sqlite3_prepare_v2(
      db, "select cnfd from conns WHERE cnupdt < ?1", -1, &selcn,
      NULL);
  if (res_sel != SQLITE_OK) {
    printf("Error in select stmt selcn\n");
  }

  // Select statement 2
  int res_sel2 = sqlite3_prepare_v2(
      db, "select cnfd from conns WHERE cnid = ?1", -1, &selfd, NULL);
  if (res_sel2 != SQLITE_OK) {
    printf("Error in select stmt selfd\n");
  }

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

  // Update status
  int res_upd = sqlite3_prepare_v2(
      db, "update conns set cnupdt = ?3, cnst = ?2 where cnid = ?1", -1, &updcn, NULL);
  if (res_upd != SQLITE_OK) {
    printf("Error in update stmt updcn\n");
  }

  // Update bytes read
  // Try to merge this with above.
  int res_updbr = sqlite3_prepare_v2(
      db, "update conns set cnupdt = ?4, cnbr = ?2, cnst = $3 where cnid = ?1", -1, &updbr, NULL);
  if (res_updbr != SQLITE_OK) {
    printf("Error in update stmt updbr\n");
  }

  // Update updt
  int res_del = sqlite3_prepare_v2(
      db, "update conns set cnupdt = $1 WHERE cnfd = ?2", -1, &delcn,
      NULL);
  if (res_del != SQLITE_OK) {
    printf("Error in Delete stmt delcn\n");
  }
  
}  // end prepSQL
