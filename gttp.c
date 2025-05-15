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
sqlite3_stmt *selcn;
sqlite3_stmt *delcn;
sqlite3_stmt *inscn;
sqlite3_stmt *selcou;

const char status_200[] = "HTTP/1.1 200 OK\r\n";
const char status_500[] = "HTTP/1.1 500 Internal Server Error\r\n";
const char cont_json[] = "Content-Type: text/json\r\n";
const char cont_txt[] = "Content-Type: text/plain\r\n";

int main(int argc, char *argv[]) {

  (void)argc;
  (void)argv;

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

  // create the table
  sqlite3_stmt *crtbl;
  sqlite3_prepare_v2(db, "delete from connections", -1, &crtbl, NULL);
  if (sqlite3_step(crtbl) != SQLITE_DONE) {
    perror("Truncate tbl");
  } else {
    // printf("Cleared!\n");
  }
  sqlite3_finalize(crtbl);

  // prepare the insert in advance!
  int res_ins = sqlite3_prepare_v2(
      db, "insert into connections(cnfd,cncrdt,cnon) values(?1,?2,1)", -1,
      &inscn, NULL);
  if (res_ins != SQLITE_OK) {
    printf("Error in insert stmt\n");
  }

  // prepare the select in advance!
  int res_sel = sqlite3_prepare_v2(
      db, "select cnfd from connections WHERE cncrdt < ?1 AND cnon = 1", -1,
      &selcn, NULL);
  if (res_sel != SQLITE_OK) {
    printf("Error in select stmt\n");
  }

  // prepare the select in advance!
  int res_selcou =
      sqlite3_prepare_v2(db,
                         "select "
                         "json_group_array(json_object('cocou',cocou,'conm1',"
                         "conm1,'cocur',cocur)) from countries",
                         -1, &selcou, NULL);
  if (res_selcou != SQLITE_OK) {
    printf("Error in country select\n");
  }

  // prepare the update statement
  // sqlite3_stmt *updcn;
  int res_upd = sqlite3_prepare_v2(
      db, "update connections set cncrdt = ?1 where cnfd = ?2", -1, &updcn,
      NULL);
  if (res_upd != SQLITE_OK) {
    printf("Error in update stmt\n");
  }

  // prepare the select in advance!
  int res_del = sqlite3_prepare_v2(
      db, "update connections set cnupdt = $1, cnon=0 WHERE cnfd = ?2", -1,
      &delcn, NULL);
  if (res_del != SQLITE_OK) {
    printf("Error in Delete stmt\n");
  }

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

  /* Accept loop */

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
  mev = (evtrack_t *)calloc(1, sizeof(evtrack_t)); // not freed?
  // ----------------------------------------------------ADD Main socket
  mev->evfd = sock_fd; // the main one.
  mev->evid = 0;
  mev->evst = 0;
  mev->evbr = 0;

  event.data.ptr = mev;
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
  while (shutDown == 1) {

    // free(events);
    events = (struct epoll_event *)calloc(
        MAX_EVENTS, sizeof(struct epoll_event)); // how to free?
    conns = epoll_wait(epfd, events, MAX_EVENTS, 1000);
    if (conns == -1) {

      if (errno == EINTR) {
        // printf("epoll_wait interrupted by signal\n");
        exit(0); // since on signal

      } else {
        perror("Wait");
        exit(-1);
      }
    }

    // printf("No of fds: %d\n", conns);

    for (n = 0; n < conns; ++n) {

      evtrack_t *evt;
      evt = events[n].data.ptr;

      // printf("Loop#: %d\n", n);
      // printf("fd: %d; id: %d; st: %d; Events: %s %s %s %s %s %s \n",
      // evt->evfd,
      //        evt->evid, evt->evst, (events[n].events & EPOLLIN) ? "IN" : "",
      //        (events[n].events & EPOLLOUT) ? "OUT" : "",
      //        (events[n].events & EPOLLRDHUP) ? "RDHUP" : "",
      //        (events[n].events & EPOLLHUP) ? "HUP" : "",
      //        (events[n].events & EPOLLPRI) ? "PRI" : "",
      //        (events[n].events & EPOLLERR) ? "ERR" : "");

      // Check if the originating socket is existing conn or original sock_fd
      // If original, treat it as new conn, add to watch list

      if (evt->evfd == sock_fd) {

        // ----------------------------------------------------------------------------------accept
        int conn_sock = accept4(sock_fd, (struct sockaddr *)&client_addr,
                                &client_addr_len, SOCK_NONBLOCK);

        if (conn_sock == -1) {

          if (errno == EINTR) {
            printf("Accept interrupted by signal\n");
            exit(0);
          }

          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;

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
        printf("Accept: fd: %d; IP: %s; port:%s\n", conn_sock, name, port);

        // insert connection into db
        time_t t1 = time(NULL); // stores current time
        sqlite3_bind_int64(inscn, 1, (sqlite3_int64)conn_sock); // cnfd
        sqlite3_bind_int64(inscn, 2, (sqlite3_int64)t1);        // cndt
        if (sqlite3_step(inscn) != SQLITE_DONE) {
          perror("Ins");
        } else {
          // printf("Inserted!\n");
        }
        sqlite3_reset(inscn);

        // This IS same event variable as earlier
        // as the struct gets stored in the epoll table, guess it is oK.
        // The data member is the only one that is saved and returned.
        event.events = EPOLLIN | EPOLLET | EPOLLOUT | EPOLLRDHUP;

        // create a new memory loc for this event
        // Here I am just asking memory from random locations, should be
        // organised in array? How to keep track of each?
        //
        evtrack_t *cev;
        cev = (evtrack_t *)calloc(1, sizeof(evtrack_t));
        // create mem for a struct holding event data.
        cev->evid = ctr++;
        cev->evst = 1; // just setup
        cev->evfd = conn_sock;
        cev->evbr = 0;

        event.data.ptr = cev;

        // event.data.fd = conn_sock;

        // printf("Event addr 2 %p\n",(void *)&event);
        res = epoll_ctl(epfd, EPOLL_CTL_ADD, conn_sock, &event);
        if (res == -1) {
          perror("Add");
          exit(-1);
        }

      } else {

        // Print the events that came in.

        evtrack_t *evt2;
        evt2 = events[n].data.ptr; // error here!

        // printf("Test: %d\n", events[n].data.fd);

        // If existing, process read

        if (events[n].events & EPOLLRDHUP) {
          close(evt2->evfd);
          printf("Closed due to RDHUP\n");
          break;
        }

        if (events[n].events & EPOLLERR) {
          close(evt2->evfd);
          printf("Closed due to ERR\n");
          break;
        }

        if (events[n].events & EPOLLIN) {
          hdlRead(evt2);
        }

        if (events[n].events & EPOLLOUT) {
          hdlSend(evt2);
        }

        // handle EPOLLPRI etc., later.
        //

        // IF sent, then free up the memory for event data

        if (evt2->evst == 3) {
          // free up memory
          // Dont watch anymore
          int del = epoll_ctl(epfd, EPOLL_CTL_DEL, evt2->evfd, NULL);
          if (del == -1) {
            perror("Del");
            exit(-1);
          }

          // Delete record
          // int b3 =
          //     sqlite3_bind_int64(delcn, 1, (sqlite3_int64)evt2->evfd); //
          //     time
          // if (b3 != SQLITE_OK) {
          //   printf(sqlite3_errmsg(db));
          // }
          // sqlite3_step(delcn);
          // sqlite3_reset(delcn);

          free(evt2);
          // printf("freed memory!\n");
        }

        // If sent then close the connection?

        // res = epoll_ctl(epfd,EPOLL_CTL_DEL,events[n].data.fd,NULL);
        // if (res == -1) {
        //   perror("Del");
        //   exit(-1);
        // }
        // printf("Conn deleted: %d\n",events[n].data.fd);

      } // else

    } // for loop thru conns caught by wait.

    free(events);
    // printf("Back to main loop!\n");

    // Try conn clean up here

    time_t tq = time(NULL); // stores current time
    // printf("time now %ld\n", tq);
    // printf("timeout: %d\n",KA_TIMEOUT);
    tq -= KA_TIMEOUT;
    // printf("time less timeout %ld\n", tq);
    // printf("qry time: %ld", tq);

    int b1 = sqlite3_bind_int64(selcn, 1, (sqlite3_int64)tq); // time
    if (b1 != SQLITE_OK) {
      printf(sqlite3_errmsg(db));
    }

    // int b2 = sqlite3_step(selcn);
    // if (b2 != SQLITE_OK) {
    //   printf(sqlite3_errmsg(db));
    // }

    while (sqlite3_step(selcn) != SQLITE_DONE) {

      // printf("fd: %d, ", sqlite3_column_int(selcn, 0));
      // printf("\n");
      // Delete that connection socket.
      int delfd = sqlite3_column_int(selcn, 0);
      // printf("Deleting %d\n", delfd);

      // Delete from watch list
      int d4 = epoll_ctl(epfd, EPOLL_CTL_DEL, delfd, NULL);
      if (d4 == -1) {
        if (errno != ENOENT) {
          // ignoring ENOENT since it may hv been deleted earlier aft send.
          perror("ecd");
        }
      }
      // Figure out how to get the relevant evt2 to clear?!;

      close(delfd);

      // Delete record
      time_t t2 = time(NULL); // stores current time
      int b2 = sqlite3_bind_int64(delcn, 1, (sqlite3_int64)t2); // del date
      if (b2 != SQLITE_OK) {
        printf(sqlite3_errmsg(db));
      }
      b2 = sqlite3_bind_int64(delcn, 2, (sqlite3_int64)delfd); // fd
      if (b2 != SQLITE_OK) {
        printf(sqlite3_errmsg(db));
      }
      sqlite3_step(delcn);
      sqlite3_reset(delcn);

    } // while loop of sqlite3

    sqlite3_reset(selcn);

  } // infinite while loop.
}

void hdlRead(evtrack_t *evt) {

  // Write the server so it can handle multiple requests on each accepted
  // socket, Loop reading requests and sending responses until
  // - peer closes the connection,
  // - read timeout occurs reading the request,
  // - socket error occurs.

  // printf("Reading: %d from conn# %d \n", evt->evfd, evt->evid);

  ssize_t br;
  uint8_t buffer[MAX_READ_BUFFER] = {0};

  // -----------------------------Loop for the recv
  while (1) {

    br = read(evt->evfd, buffer, MAX_READ_BUFFER - 1);

    if (br < 0) {

      if (errno == EINTR) {
        printf("Read interrupted by signal!\n");
        exit(0); // since signal
      }

      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      } else {
        perror("Read");
        // free(events);
        exit(-1);
      }
    }

    if (br >= 0) {
      evt->evbr += br;
      evt->evst = 2; // Reading (when complete?!)

      // update the time for that connection.
      time_t tr = time(NULL); // stores current time
      sqlite3_bind_int64(updcn, 1, (sqlite3_int64)evt->evfd); // cnfd
      sqlite3_bind_int64(updcn, 2, (sqlite3_int64)tr);        // cndt
      if (sqlite3_step(updcn) != SQLITE_DONE) {
        perror("Upd");
      } else {
        // printf("Updated!\n");
      }
      sqlite3_reset(updcn);
      break;
    }

  } // while loop

  // printf("Read: conn#: %d; fd: %d; bytes: %d \n", evt->evid, evt->evfd,
  //       evt->evbr);
  return;
}

void hdlSend(evtrack_t *evt) {

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

    bs = send(evt->evfd, http_response, strlen(http_response), MSG_NOSIGNAL);

    if (bs >= 0) {
      evt->evst = 3; // Sending or sent?!

      // update the time for this connection.

      time_t ts = time(NULL); // stores current time
      sqlite3_bind_int64(updcn, 1, (sqlite3_int64)evt->evfd); // cnfd
      sqlite3_bind_int64(updcn, 2, (sqlite3_int64)ts);        // cndt
      if (sqlite3_step(updcn) != SQLITE_DONE) {
        perror("Upd");
      } else {
        // printf("Updated!\n");
      }
      sqlite3_reset(updcn);

      break;

    } else {

      if (errno == EINTR) {
        printf("Send interrupted by signal!\n");
        exit(0); // since signal
      }

      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
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
  free(mev);      // epoll wait on main socket
  free(events);   // close events array
  close(epfd);    // close the epoll fd
  sqlite3_finalize(selcn);
  sqlite3_finalize(inscn);
  sqlite3_finalize(updcn);
  sqlite3_finalize(delcn);
  sqlite3_finalize(selcou);
  sqlite3_close(db); // close the db.
  printf("Closed everything!\n");

  // exit(0);
}
