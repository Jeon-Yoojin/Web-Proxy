#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void *thread (void *vargp);
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
  int listenfd, *connfdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <paazort>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfdp);
  }
  printf("%s", user_agent_hdr);
  return 0;
}

void *thread (void *vargp)
{
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(connfd);
  Close(connfd);
  return NULL;
}

void doit(int fd)
/*
소켓을 연결하고, hostname과 port 번호가 지정되었으므로
1. 파싱 (호스트 이름, 경로~쿼리 등)
2. 해당 호스트 이름에 접속하고, 파일경로와 쿼리를 보냄 (HTTP ver 1.0)
3. 추가적인 헤더 보내기 (static/dynamic 공통)
4. 그 외 수신된 헤더가 있으면 그건 그대로 처리한다
 */
{
  int proxyfd;
  struct stat sbuf;
  char client_buf[MAXLINE], server_buf[MAXLINE];
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char file_path[MAXLINE], hostname[MAXLINE], port_num[MAXLINE];
  rio_t client_rio, server_rio;

  /* Read request line and headers */
  Rio_readinitb(&client_rio, fd);
  Rio_readlineb(&client_rio, client_buf, MAXLINE);
  sscanf(client_buf, "%s %s %s", method, uri, version);
  printf("--------From Client--------\n");
  printf("Request headers: \n");
  printf("%s", client_buf);

  // 새로운 소켓 생성
  /* strcasecmp(대소문자 구분 없이 문자열 구분), method가 GET이 아닌 경우 */
  if (strcasecmp(method, "GET")) {
    printf("Proxy does not implement this method\n");
    return;
  }
  parse_uri(uri, hostname, port_num, file_path);

  read_requesthdrs(server_buf, hostname, port_num, file_path, &client_rio);

  proxyfd = Open_clientfd(hostname, port_num);

  // server측으로 받은 헤더에 추가 정보를 붙여서 전송
  Rio_writen(proxyfd, server_buf, strlen(server_buf));
  Rio_readinitb(&server_rio, proxyfd);

  size_t n;
  while((n = Rio_readlineb(&server_rio, server_buf, MAXLINE)) != 0) {
    printf("%s", server_buf);
    Rio_writen(fd, server_buf, n);
  }
  Close(proxyfd);
}

void parse_uri(char *uri, char *hostname, char *port_num, char *file_path)
{
  char *ptr = strstr(uri, "//");
  ptr = ptr != NULL ? ptr + 2 : uri;
  char *ptr_host = ptr;
  char *ptr_colon = strchr(ptr, ':');
  char *ptr_slash = strchr(ptr, '/');

  /* port와 file path 모두 있는 경우 */
  if (ptr_colon && ptr_slash) { //localhost:5000/home.html
    strncpy(hostname, ptr_host, ptr_colon - ptr_host);
    strncpy(port_num, ptr_colon + 1, ptr_slash - ptr_colon - 1);
    strcpy(file_path, ptr_slash);   // '/'도 포함되어야 하기 때문
  }
  /* port는 없고 file path는 있는 경우 */
  else if (!ptr_colon && ptr_slash) { //localhost/home.html
    strncpy(hostname, ptr_host, ptr_slash - ptr_host);
    strcpy(port_num, "8000");   // 기본 포트 번호
    strcpy(file_path, ptr_slash);
  }
  /* port는 있고 file path는 없는 경우 */
  else if (ptr_colon && !ptr_slash) { //localhost:5000
    strncpy(hostname, ptr_host, ptr_colon - ptr_host);
    strcpy(port_num, ptr_colon);
    strcpy(file_path, "");
  }
  /* port도 없고 file path도 없는 경우 */
  else {  //localhost
    strcpy(hostname, ptr_host);
    strcpy(port_num, "8000");
    strcpy(file_path, "");
  }

  return;
}

/* 헤더를 읽고 무시 */
void read_requesthdrs(char *http_header, char *hostname, int port_num, char *file_path, rio_t *server_rio)
{
  char buf[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];

  sprintf(http_header, "GET %s HTTP/1.0\r\n", file_path);

  while(Rio_readlineb(server_rio, buf, MAXLINE) > 0) {
    if (strcmp(buf, "\r\n") == 0) break;

    if (!strncasecmp(buf, "Host", strlen("Host")))  // Host: 
    {
      strcpy(host_hdr, buf);
      continue;
    }

    if (strncasecmp(buf, "Connection", strlen("Connection"))
        && strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection"))
        && strncasecmp(buf, "User-Agent", strlen("User-Agent")))
    {
      strcat(other_hdr, buf);
    }
  }

  if (strlen(host_hdr) == 0) {
    sprintf(host_hdr, "Host: %s:%d\r\n", hostname, port_num);
  }

  sprintf(http_header, "%s%s%s%s%s%s%s", http_header, host_hdr, "Connection: close\r\n", "Proxy-Connection: close\r\n", user_agent_hdr, other_hdr, "\r\n");
  return;
}
