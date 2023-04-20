#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct node
{
  char file_path[MAXLINE];
  char content[MAX_OBJECT_SIZE];
  int content_length;
  struct node *prev;
  struct node *next;
} node;

typedef struct cache
{
  int total_size;
  node *root;
  node *tail;
} cache;

cache *c;

node *find(cache *c, char *file_path);
void *thread(void *vargp);
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
  int listenfd, *connfdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  c = (cache *)Malloc(sizeof(cache));
  c->root = NULL;
  c->tail = NULL;
  c->total_size = 0;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <paazort>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfdp);
  }
  printf("%s", user_agent_hdr);
  return 0;
}

void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(connfd);
  Close(connfd);
  return NULL;
}

void doit(int fd)
{
  int proxyfd;
  int *content_length;
  struct stat sbuf;
  char client_buf[MAXLINE], server_buf[MAXLINE], content_buffer[MAX_OBJECT_SIZE];
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char file_path[MAXLINE], hostname[MAXLINE], port_num[MAXLINE];
  rio_t client_rio, server_rio;

  /* Read request line and headers */
  Rio_readinitb(&client_rio, fd);
  Rio_readlineb(&client_rio, client_buf, MAXLINE);
  sscanf(client_buf, "%s %s %s", method, uri, version);
  printf("--------From Client--------\n");
  printf("%s", client_buf);

  /* strcasecmp(대소문자 구분 없이 문자열 구분), method가 GET이 아닌 경우 */
  if (strcasecmp(method, "GET"))
  {
    printf("Proxy does not implement this method\n");
    return;
  }
  parse_uri(uri, hostname, port_num, file_path);
  read_requesthdrs(server_buf, hostname, port_num, file_path, content_length, &client_rio);

  // file_path로 cache에 저장된 게 있는지 확인
  node *temp_node = find(c, file_path);
  if (temp_node)
  {
    /* cache에 있는 경우 */
    hit(c, temp_node);
    printf("%s", temp_node->content);
    Rio_writen(fd, temp_node->content, sizeof(temp_node->content));
    return;
  }
  else
  {
    /* cache에 없는 경우 */
    proxyfd = Open_clientfd(hostname, port_num);
    Rio_writen(proxyfd, server_buf, strlen(server_buf));
    Rio_readinitb(&server_rio, proxyfd);

    size_t n;
    size_t cont_size = 0;
    while ((n = Rio_readlineb(&server_rio, server_buf, MAXLINE)) != 0)
    {
      /* 서버에 콘텐츠 요청 후 받은 콘텐츠를 client에 전달 */
      printf("%s", server_buf);
      Rio_writen(fd, server_buf, n);
      /* content-length가 있는지 확인, 있으면 변수에 저장*/
      if (strstr(server_buf, "Content-length:"))
        content_length = atoi(strchr(server_buf, ':') + 1);
      strcat(content_buffer, server_buf);
    }
    //printf("%d: cont_size, %d: content-len", cont_size, content_length);

    printf("%s%d%s\n", file_path, content_length, content_buffer);
    // cache 공간이 충분하지 않다면
    while (!is_cache_enough(c, content_length))
    {
      // 마지막 원소 삭제
      delete (c, c->tail);
    }
    // cache에 insert
    //detach_header(content_buffer);
    insert(c, file_path, content_length, content_buffer);
    Close(proxyfd);
  }
}

/* 헤더 제거를 위한 함수 */
void detach_header(char *content) {
  printf("=============detach header ===============");
  char new_header[MAXLINE];
  char *header = strstr(content, "\r\n\r\n");
  printf("%c\n", *header);
  header = header + 5;
  strncpy(new_header, content, header - content);
  printf("%s", new_header);
}

void parse_uri(char *uri, char *hostname, char *port_num, char *file_path)
{
  char *ptr = strstr(uri, "//");
  ptr = ptr != NULL ? ptr + 2 : uri;
  char *ptr_host = ptr;
  char *ptr_colon = strchr(ptr, ':');
  char *ptr_slash = strchr(ptr, '/');

  /* port와 file path 모두 있는 경우 */
  if (ptr_colon && ptr_slash)
  { // localhost:5000/home.html
    strncpy(hostname, ptr_host, ptr_colon - ptr_host);
    strncpy(port_num, ptr_colon + 1, ptr_slash - ptr_colon - 1);
    strcpy(file_path, ptr_slash); // '/'도 포함되어야 하기 때문
  }
  /* port는 없고 file path는 있는 경우 */
  else if (!ptr_colon && ptr_slash)
  { // localhost/home.html
    strncpy(hostname, ptr_host, ptr_slash - ptr_host);
    strcpy(port_num, "8000"); // 기본 포트 번호
    strcpy(file_path, ptr_slash);
  }
  /* port는 있고 file path는 없는 경우 */
  else if (ptr_colon && !ptr_slash)
  { // localhost:5000
    strncpy(hostname, ptr_host, ptr_colon - ptr_host);
    strcpy(port_num, ptr_colon);
    strcpy(file_path, "");
  }
  /* port도 없고 file path도 없는 경우 */
  else
  { // localhost
    strcpy(hostname, ptr_host);
    strcpy(port_num, "8000");
    strcpy(file_path, "");
  }

  return;
}

/* 요청 헤더를 읽기 */
void read_requesthdrs(char *http_header, char *hostname, int port_num, char *file_path, int *content_length, rio_t *server_rio)
{
  char buf[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];

  sprintf(http_header, "GET %s HTTP/1.0\r\n", file_path);

  while (Rio_readlineb(server_rio, buf, MAXLINE) > 0)
  {
    if (strcmp(buf, "\r\n") == 0)
      break;

    if (!strncasecmp(buf, "Host", strlen("Host"))) // Host:
    {
      strcpy(host_hdr, buf);
      continue;
    }

    /* Connection/Proxy-Connection/User-Agent가 아니면 other hdr에 추가. */
    if (strncasecmp(buf, "Connection", strlen("Connection")) && strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection")) && strncasecmp(buf, "User-Agent", strlen("User-Agent")))
    {
      strcat(other_hdr, buf);
    }
  }

  /* host_hdr가 없으면 만들어 준다. */
  if (strlen(host_hdr) == 0)
  {
    sprintf(host_hdr, "Host: %s:%d\r\n", hostname, port_num);
  }

  sprintf(http_header, "%s%s%s%s%s%s%s", http_header, host_hdr, "Connection: close\r\n", "Proxy-Connection: close\r\n", user_agent_hdr, other_hdr, "\r\n");
  return;
}

void insert(cache *c, char *file_path, int content_length, char *content_buf)
{
  // 새로운 노드를 만든 후에 filepath, content_length, content_buf 담아 주기
  node *new_node = (node *)Malloc(sizeof(node));
  // 새로운 node 구조체에 함수의 매개변수로 받은 file path, content length, content 넣기
  strcpy(new_node->file_path, file_path);
  new_node->content_length = content_length;
  strcpy(new_node->content, content_buf);

  node *node_root = c->root;
  if (c->root == NULL)
  {
    // 새로운 노드를 루트 노드로
    c->root = new_node;
  }
  else
  {
    // 기존 root 노드와 새로운 노드 연결, 루트 갱신
    node_root->prev = new_node;
    new_node->next = node_root;
    c->root = new_node;
  }
  // cache 사이즈 갱신
  c->total_size = c->total_size + content_length;
}

void delete(cache *c, node *target)
{
  /* 맨 뒤 노드를 삭제하는 경우 */
  // 처음인 경우
  if (target->prev == NULL)
  {
    c->root = target->next;
    target->next->prev = NULL;
  }
  // 끝인 경우
  else if (target->next == NULL)
  {
    c->tail = target->prev;
    target->prev->next = NULL;
  }
  // 중간인 경우
  else
  {
    target->next->prev = target->prev;
    target->prev->next = target->next;
  }
  // cache 사이즈 갱신 및 메모리 free
  c->total_size = c->total_size - target->content_length;
  free(target);
}

node *find(cache *c, char *file_path)
{
  /* filepath를 기준으로 원하는 노드 찾기 */
  node *curr = c->root;
  while (curr != NULL)
  {
    if (strcmp(curr->file_path, file_path) == 0)
    {
      return curr;
    }
    curr = curr->next;
  }
  return NULL;
}

void hit(cache *c, node *target)
{
  /* cache에서 찾은 경우, 캐시를 삭제한 후 맨 앞에 삽입 */
  char *file_path = target->file_path;
  char *content_buf = target->content;
  int content_length = target->content_length;

  if (target == c->root)
    return;
  delete (c, target);
  insert(c, file_path, content_length, content_buf);
}

int is_cache_enough(cache *c, int size)
{
  if (c->total_size + size > MAX_CACHE_SIZE)
    return 0;
  else
    return 1;
}