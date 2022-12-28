#include <ev.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <error.h>
#include <err.h>
#include <errno.h>
#include <ctype.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>

#include "message_buffer.h"

#ifdef _DEBUG
#define DEBUG(msg...) fprintf(stderr, msg)
#else
#define DEBUG(mag...)
#endif


/*
 * Размер буфера для сообщения
 */
static const size_t BUFFER_SIZE = 1024;

/*
 *  Инициализация сокета
 */
static void socket_init(struct sockaddr_in * addr, int port_number)
{
  addr->sin_port = htons(port_number);
  addr->sin_addr.s_addr = htonl(INADDR_ANY);
  addr->sin_family = AF_INET;
}

/*
 *  Создание сокета
 */
static int socket_create(struct sockaddr_in *addr)
{
  int fd;
  int on = 1;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    fflush(stdout);
    fprintf(stderr, "Ошибка создания сокета: %s (%d)\n", strerror(errno), errno);
    return -1;
  }

  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  /*setsockopt(listen_fd, IPPROTO_TCP, TCP_QUICKACK, &on, sizeof(on));*/
  /*setsockopt(listen_fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));*/

  if (bind(fd, (const struct sockaddr *)addr, sizeof(struct sockaddr_in)) < 0)
  {
    fflush(stdout);
    fprintf(stderr, "Ошибка назначения адреса сокету: %s (%d)\n", strerror(errno), errno);
    return -1;
  }

  return fd;
}

/*
 * Закрыть соединение и освободить память
*/
static void release_connection(int sock_id, struct ev_loop *loop, ev_io *watcher)
{
  message_buffer_t *buffer = (message_buffer_t *)(watcher->data);

  fprintf(stderr, "Соединение по сокету %d закрыто!\n", sock_id);
  ev_io_stop(loop, watcher);
  close(sock_id);
  free(watcher);
  message_buffer_destroy(buffer);
  free(buffer);
}

/*
 * Запись данных
 */
static int send_data(int fd, message_buffer_t *buffer)
{
  int bytes = 0;
  int snt = 0;

  if (buffer == NULL || buffer->buffer == NULL || fd <= 0)
    return -1;

  DEBUG("send data\n");
  while (buffer->size)
  {
    snt = send(fd, buffer->buffer + (buffer->offset - buffer->size), buffer->size, 0);

    if (snt <= 0)
    {
      if (errno != EAGAIN && errno != EWOULDBLOCK)
        return -1;

      DEBUG("send would block\n");
      break;
    }

    buffer->size -= snt;
    bytes        += snt;
  }

  if (buffer->size == 0)
  {
    DEBUG("buffer is empty\n");
    buffer->offset = 0;
  }

  DEBUG("send rc = %d\n", bytes);
  return bytes;
}

/*
 * Чтение данных
 */
int read_data(int fd, message_buffer_t *buffer)
{
  int bytes = 0;
  int received;

  if (buffer == NULL || buffer->buffer == NULL || fd <= 0)
    return -1;

  while (buffer->offset < (int)(buffer->capacity))
  {
    received = recv(fd, buffer->buffer + buffer->offset, buffer->capacity - buffer->offset, 0);

    if (received < 0)
    {
      if (errno != EAGAIN && errno != EWOULDBLOCK)
      {
        bytes = -1;
      }
      else
      {
        DEBUG("read would block\n");
      }
      break;
    }

    if (received == 0)
    {
      bytes = -1;
      break;
    }

    buffer->size   += received;
    buffer->offset += received;
    bytes += received;
  }

  DEBUG("read rc = %d\n", bytes);
  return bytes;
}

/*
 * Действия при готовности данных для чтения/записи
 */
static void on_data_ready(struct ev_loop *loop, ev_io *watcher, int revents);

/*
 * Действия при готовности сокета для записи
 */
static void on_ready_to_write(struct ev_loop *loop, ev_io *watcher, int revents)
{
  int rc, sock_id = watcher->fd;
  message_buffer_t *buffer = (message_buffer_t *)(watcher->data);

  DEBUG("on_ready_to_write\n");

  if (revents & EV_ERROR)
  {
    fprintf(stderr, "Внутренняя ошибка libev при вызове обработчика готовности сокета %d к записи\n", sock_id);
    release_connection(sock_id, loop, watcher);
    return;
  }

  if (revents & EV_WRITE)
  {
    DEBUG("EV_WRITE\n");
  }

  if (buffer->size > 0)
  {
    rc = send_data(sock_id, buffer);
    DEBUG("send_data.rc = %d size = %d\n", rc, buffer->size);

    if (rc < 0)
    {
      release_connection(sock_id, loop, watcher);
      return;
    }
  }
  else
  {
    DEBUG("В буфере нет данных\n");
  }

  if (buffer->size > 0)
  {
    // не все данные были отправлены
    DEBUG("не все данные были отправлены\n");
  }

  if (buffer->offset < (int)(buffer->capacity)) // есть место для записи данных из сокета
  {
    ev_io_stop(loop, watcher);
    ev_io_init(watcher, on_data_ready, sock_id, EV_READ);
    ev_io_start(loop, watcher);
    if (buffer->size > 0)
    {
      ev_io_set(watcher, sock_id, EV_READ);
    }
  }

  DEBUG("on_ready_to_write done\n");
}

/*
 * Действия при готовности данных для чтения/записи
 */
static void on_data_ready(struct ev_loop *loop, ev_io *watcher, int revents)
{
  int rc, sock_id = watcher->fd;
  message_buffer_t *buffer = (message_buffer_t *)(watcher->data);

  DEBUG("on_data_ready\n");

  if (revents & EV_ERROR)
  {
    fprintf(stderr, "Внутренняя ошибка libev при вызове обработчика чтения данных из сокета %d\n", sock_id);
    release_connection(sock_id, loop, watcher);
    return;
  }

  if (revents & EV_READ)
  {
    DEBUG("EV_READ\n");
    if (buffer->offset < (int)(buffer->capacity))
    {
      rc = read_data(sock_id, buffer);
      DEBUG("read_data.rc = %d size = %d\n", rc, buffer->size);

      if (rc < 0)
      {
        fflush(stdout);
        fprintf(stderr, "Ошибка чтения из сокета: %s (%d)\n", strerror(errno), errno);
        release_connection(sock_id, loop, watcher);
        return;
      }

      DEBUG("[%d] RECEIVED: %.*s\n", sock_id, buffer->size, buffer->buffer);
    }
    else
    {
      DEBUG("В буфере нет места\n");
    }
  }

  if (buffer->size > 0)
  {
    rc = send_data(sock_id, buffer);
    DEBUG("send_data.rc = %d size = %d\n", rc, buffer->size);

    if (rc < 0)
    {
      release_connection(sock_id, loop, watcher);
      return;
    }

  }
  else
  {
    DEBUG("В буфере нет данных\n");
  }

  if (buffer->size > 0)
  {
    // не все данные были отправлены
    DEBUG("не все данные были отправлены\n");

    if (buffer->offset == (int)(buffer->capacity)) // В буфере нет места для записи новых данных из сокета
    {
      ev_io_stop(loop, watcher);
      ev_io_init(watcher, on_ready_to_write, sock_id, EV_WRITE);
      ev_io_start(loop, watcher);
    }
    else
    {
      ev_io_set(watcher, sock_id, EV_READ);
    }
  }

  DEBUG("on_data_ready done\n");
}

/*
 * Действия при появлении нового подключения
 */
void accept_connection(struct ev_loop *loop, ev_io *watcher, int revents)
{
  ev_io *io_watcher = NULL;
  struct sockaddr_in sa;
  socklen_t sa_len = sizeof(sa);
  int sock_id;
  message_buffer_t *buffer = NULL;

  sock_id = accept(watcher->fd, (struct sockaddr *)&sa, &sa_len);
  if (sock_id <= 0)
  {
    fflush(stdout);
    fprintf(stderr, "Ошибка приема соединения: %s (%d)\n", strerror(errno), errno);
    exit(1);
    return;
  }

  fcntl(sock_id, F_SETFL, O_NONBLOCK);

  DEBUG("Принято новое подключение %s:%d\n", inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));

  buffer = calloc(1, sizeof(message_buffer_t));
  if (buffer == NULL)
  {
    err(EXIT_FAILURE, "Ошибка выделения памяти для буфера сообщения");
  }

  if (message_buffer_init(buffer, BUFFER_SIZE) < 0)
  {
    err(EXIT_FAILURE, "Ошибка выделения памяти для записи данных сообщения");
  }

  io_watcher = calloc(1, sizeof(struct ev_io));
  if (io_watcher == NULL)
  {
    err(EXIT_FAILURE, "Ошибка выделения памяти");
  }

  ev_io_init(io_watcher, on_data_ready, sock_id, EV_READ);
  io_watcher->data = buffer;
  ev_io_start(loop, io_watcher);
}

/*
 *
 */
int main (int argc, const char * argv[])
{
  struct sockaddr_in sock = {0};
  int sock_id;
  int port_number = -1;

  struct ev_loop *main_loop = NULL;
  ev_io sock_watcher;

  if (argc > 1)
  {
    for (int i = 0; argv[1][i] != 0; ++i)
    {
      if (!isdigit(argv[1][i]))
      {
        error(EXIT_FAILURE, 0, "Недопустимый символ в номера порта: '%s'", argv[1]);
      }
    }
    port_number = atoi(argv[1]);
    if (port_number <= 0)
    {
      error(EXIT_FAILURE, 0, "Указан недопустимый номер порта: '%s'", argv[1]);
    }
  }
  if (port_number <= 0)
  {
    error(EXIT_FAILURE, 0, "Не указан номер порта");
  }

  socket_init(&sock, port_number);
  sock_id = socket_create(&sock);
  if (sock_id < 0)
  {
    error(EXIT_FAILURE, 0, "Ошибка создания сокета");
  }

  main_loop = ev_loop_new(EVFLAG_NOENV | EVBACKEND_EPOLL);
  if (main_loop == NULL)
  {
    int err = errno;
    close(sock_id);
    error(EXIT_FAILURE, err, "Ошибка создания цикла событий");
  }

  ev_io_init(&sock_watcher, accept_connection, sock_id, EV_READ);
  ev_io_start(main_loop, &sock_watcher);

  if (listen(sock_id, 1) < 0)
  {
    int err = err;
    ev_loop_destroy(main_loop);
    error(EXIT_FAILURE, err, "Ошибка прослушивания сокета");
  }

  DEBUG("Прослушивание по порту %d запущено\n", port_number);

  ev_run(main_loop, 0);

  exit(EXIT_SUCCESS);
}
