#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit
    Close(connfd);  // line:netp:tiny:close
  }
  //printf("%s", user_agent_hdr);
  return 0;
}

void doit(int fd)
/*  */
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers: \n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  /* strcasecmp(대소문자 구분 없이 문자열 구분), method가 GET이 아닌 경우 */
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }
  
  read_requesthdrs(&rio);

  /* URI를 파일 이름과 CGI 인자 스트링으로 분석*/
  is_static = parse_uri(uri, filename, cgiargs);
  /* 디스크상에 해당 파일이 없다면 */
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't read the file");
    return;
  }
  if (is_static) {
    /* 파일이 보통 파일인지 확인 & 권한 확인. 권한이 없으면 리턴 */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    /* 정적 컨텐츠 제공 */
    serve_static(method, fd, filename, sbuf.st_size);
  }
  else {
    /* 파일이 보통 파일인지 확인 & 권한 확인. 권한이 없으면 리턴 */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    /* 동적 컨텐츠 제공 */
    serve_dynamic(method, fd, filename, cgiargs);
  }
}