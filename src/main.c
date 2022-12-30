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
#include <pthread.h>
#include <sys/ioctl.h>

#include "message_buffer.h"
#include "message_queue.h"

#ifdef _DEBUG
#define DEBUG(msg...) fprintf(stderr, "%u: ", (int)(pthread_self())); fprintf(stderr, msg)
#else
#define DEBUG(mag...)
#endif


/*
 * Число буферов для обмена сообщениями между потоками
 */
static const size_t   TO_PROCESS_QUEUE_SIZE = 10; /* Для передачи сообщения на обработку */
static const size_t FROM_PROCESS_QUEUE_SIZE = 20; /* Для передачи сообщения после обработки */

/*
 * Данные потока
 */
struct thread_context_t
{
  int      port_number;
  int      sock_id;

  ev_io    io_watcher;

  ev_async * stop_watcher;
  ev_async to_process_watcher;
  ev_async from_process_watcher;

  message_queue_t *   to_process_queue;
  message_queue_t * from_process_queue;

  struct ev_loop * loop;
  struct ev_loop * main_loop;

  message_buffer_t * current_send_buffer;
}; // struct thread_context_t
typedef struct thread_context_t thread_context_t;

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

  if (bind(fd, (const struct sockaddr *)addr, sizeof(struct sockaddr_in)) < 0)
  {
    fflush(stdout);
    fprintf(stderr, "Ошибка назначения адреса сокету: %s (%d)\n", strerror(errno), errno);
    return -1;
  }

  return fd;
}

/*
 * Получить кол-во байт, доступных для чтения из сокета
 */
static size_t get_bytes_available(int sock_id)
{
  int bytes_available = 0;
  if (ioctl(sock_id, FIONREAD, &bytes_available) == -1)
  {
    return 0;
  }
  return bytes_available;
}

/*
 * Закрыть соединение и освободить память
*/
static void release_context(thread_context_t * context)
{
  fprintf(stderr, "Соединение по сокету %d закрыто!\n", context->sock_id);

  ev_async_send(context->main_loop, context->stop_watcher);

  ev_io_stop   (context->loop, &(context->io_watcher));
  ev_async_stop(context->loop, &(context->from_process_watcher));

  ev_async_stop(context->main_loop, &(context->to_process_watcher));

  ev_break(context->loop, EVBREAK_ALL);
//  ev_unloop(context->loop, EVUNLOOP_ALL);
//  ev_loop_destroy (context->main_loop);

//  ev_break(context->main_loop, EVBREAK_ALL);
//  ev_unloop(context->main_loop, EVUNLOOP_ALL);

  close(context->sock_id);

  message_queue_destroy(context->to_process_queue);
  message_queue_destroy(context->from_process_queue);
}

/*
 * 
 */
static void stop_main_loop(struct ev_loop *loop, ev_async *watcher, int revents)
{
  ev_break(loop, EVBREAK_ALL);
}

/*
 * Запись данных в сокет
 */
static int send_data(int fd, message_buffer_t *buffer)
{
  int bytes = 0;
  int snt = 0;

  DEBUG("%s\n", __FUNCTION__);

  if (buffer == NULL || buffer->buffer == NULL || fd <= 0)
    return -1;

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

  DEBUG("%s done rc = %d\n", __FUNCTION__, bytes);
  return bytes;
}

/*
 * Чтение данных из сокета
 */
int read_data(int fd, message_buffer_t *buffer)
{
  int bytes = 0;
  int received;

  DEBUG("%s\n", __FUNCTION__);

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

  DEBUG("%s done. rc = %d\n", __FUNCTION__, bytes);
  return bytes;
}

/*
 * Действия при готовности сокета для чтения
 */
static void on_socket_ready_to_read(struct ev_loop *loop, ev_io *watcher, int revents);

/*
 * Действия при готовности сокета для записи
 */
static void on_socket_ready_to_write(struct ev_loop *loop, ev_io *watcher, int revents)
{
  int rc, sock_id = watcher->fd;
  thread_context_t *context = (thread_context_t *)(watcher->data);
  message_buffer_t *buffer = context->current_send_buffer;

  DEBUG("%s\n", __FUNCTION__);

  if (revents & EV_ERROR)
  {
    fprintf(stderr, "Внутренняя ошибка libev при вызове обработчика готовности сокета %d к записи\n", sock_id);
    release_context(context);
    return;
  }

  if (buffer->size > 0)
  {
    rc = send_data(sock_id, buffer);

    if (rc < 0)
    {
      release_context(context);
      return;
    }
  }

  if (buffer->size > 0)
  {
    // не все данные были отправлены
    DEBUG("не все данные были отправлены\n");
  }
  else
  {
    DEBUG("В буфере нет данных\n");
    // возвращаем "обычную" схему работы
    ev_io_stop(loop, watcher);
    context->current_send_buffer = NULL;
    ev_io_init(&(context->io_watcher), on_socket_ready_to_read, sock_id, EV_READ);
    ev_async_start(context->loop, &(context->from_process_watcher));
    ev_io_start(loop, &(context->io_watcher));
  }

  DEBUG("%s done\n", __FUNCTION__);
}

/*
 * Действия при готовности сокета для чтения
 */
static void on_socket_ready_to_read(struct ev_loop *loop, ev_io *watcher, int revents)
{
  int rc, sock_id = watcher->fd;
  thread_context_t *context = (thread_context_t *)(watcher->data);
  message_buffer_t *buffer = NULL;

  DEBUG("%s\n", __FUNCTION__);

  if (revents & EV_ERROR)
  {
    fprintf(stderr, "Внутренняя ошибка libev при вызове обработчика чтения данных из сокета %d\n", sock_id);
    release_context(context);
    return;
  }

  if (revents & EV_READ)
  {
    buffer = message_queue_get_free_buffer(context->to_process_queue);
    if (buffer != NULL)
    {
      int bytes = get_bytes_available(sock_id);
      if (message_buffer_resize(buffer, bytes) != 0)
      {
        fflush(stdout);
        fprintf(stderr, "Ошибка выденения памати для размещения данных из сокета: %s (%d)\n", strerror(errno), errno);
        release_context(context);
        return;
      }

      rc = read_data(sock_id, buffer);

      if (rc < 0)
      {
        fflush(stdout);
        fprintf(stderr, "Ошибка чтения из сокета: %s (%d)\n", strerror(errno), errno);
        release_context(context);
        return;
      }

      DEBUG("[%d] RECEIVED: %.*s\n", sock_id, buffer->size, buffer->buffer);
      message_queue_add_ready_buffer(context->to_process_queue, buffer);
    }
    else
    {
      DEBUG("Нет свободного буфера\n");
    }
  }

  if (buffer != NULL && buffer->size > 0)
  {
    context->to_process_watcher.data = context;
    ev_async_send(context->main_loop, &(context->to_process_watcher));
  }
  else
  {
    DEBUG("Нет данных для передачи на обработку\n");
  }

  DEBUG("%s done\n", __FUNCTION__);
}

/*
 * Действия при готовности данных для записи в сокет
 */
static void send_processed_data(struct ev_loop *loop, ev_async *watcher, int revents)
{
  int rc;
  thread_context_t *context = (thread_context_t *)(watcher->data);
  int sock_id = context->sock_id;
  message_buffer_t *buffer = NULL;

  DEBUG("%s\n", __FUNCTION__);

  if (revents & EV_ERROR)
  {
    fprintf(stderr, "Внутренняя ошибка libev при вызове обработчика записи данных в сокет %d\n", sock_id);
    release_context(context);
    return;
  }

  buffer = message_queue_get_ready_buffer(context->from_process_queue);
  if (buffer == NULL)
  {
    DEBUG("Нет готовых данных для записи в сокет\n");
    return;
  }

  if (buffer->size == 0)
  {
    DEBUG("Пустой буфер для записи в сокет\n");
    message_queue_release_buffer(context->from_process_queue, buffer);
    ev_async_start(context->main_loop, &(context->to_process_watcher));
    return;
  }

  rc = send_data(sock_id, buffer);

  if (rc < 0)
  {
    release_context(context);
    return;
  }

  if (buffer->size > 0)
  {
    // не все данные были отправлены
    DEBUG("не все данные были отправлены\n");
    // ждем освобождения сокета
    ev_async_stop(loop, watcher);                      // не принимаем обработанные данные 
    ev_io_stop(context->loop, &(context->io_watcher)); // не принимаем данные из сокета
    context->current_send_buffer = buffer;
    ev_io_init(&(context->io_watcher), on_socket_ready_to_write, sock_id, EV_WRITE);
    ev_io_start(loop, &(context->io_watcher));         // ждем освобождения сокета для записи
  }
  else
  {
    DEBUG("В буфере нет данных\n");
    message_queue_release_buffer(context->from_process_queue, buffer);
    if (!ev_is_active(&(context->to_process_watcher)))
    {
      ev_async_start(context->main_loop, &(context->to_process_watcher));
    }
  }

  DEBUG("%s done\n", __FUNCTION__);
}

/*
 * Действия при появлении нового подключения
 */
static void accept_connection(struct ev_loop *loop, ev_io *watcher, int revents)
{
  thread_context_t *context = NULL;
  struct sockaddr_in sa;
  socklen_t sa_len = sizeof(sa);
  int sock_id;

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

  context = (thread_context_t*)(watcher->data);
  context->sock_id = sock_id;
  context->io_watcher.data = context;
  ev_io_init(&(context->io_watcher), on_socket_ready_to_read, sock_id, EV_READ);
  ev_io_start(loop, &(context->io_watcher));
}

/*
 * Поток работы с сокетом
 * Чтение данных из сокета
 * Отправка данных на обработку
 * Получение данных после обработки
 * Запись данных в сокет
 */
static void * socket_routine(void * params)
{
  thread_context_t  * context;
  struct sockaddr_in sock = {0};
  int sock_id;
  ev_io sock_watcher;

  DEBUG("%s\n", __FUNCTION__);
  context = (thread_context_t*)(params);

  socket_init(&sock, context->port_number);
  sock_id = socket_create(&sock);
  if (sock_id < 0)
  {
    error(EXIT_FAILURE, 0, "Ошибка создания сокета");
  }

  ev_io_init(&sock_watcher, accept_connection, sock_id, EV_READ);
  sock_watcher.data = context;
  ev_io_start(context->loop, &sock_watcher);

  if (listen(sock_id, 1) < 0)
  {
    int err = errno;
    ev_loop_destroy(context->loop);
    error(EXIT_FAILURE, err, "Ошибка прослушивания сокета");
  }

  DEBUG("Прослушивание по порту %d запущено\n", context->port_number);

  ev_loop(context->loop, 0);

  DEBUG("%s done\n", __FUNCTION__);

  return NULL;
}

/*
 * Обработка данных в основном потоке
 */
static void process_data(struct ev_loop * loop, ev_async *watcher, int revents)
{
  thread_context_t * context;
  message_buffer_t * read_buffer = NULL;
  message_buffer_t * write_buffer = NULL;

  context = (thread_context_t*)(watcher->data);

  DEBUG("%s\n", __FUNCTION__);

  write_buffer = message_queue_get_free_buffer(context->from_process_queue);
  if (write_buffer == NULL)
  {
    DEBUG("Нет свободного буфера для записи результа\n");
    DEBUG("Приостанавливаем обработку данных\n");

    ev_async_stop(context->main_loop, &(context->to_process_watcher));
    return;
  }

  read_buffer = message_queue_get_ready_buffer(context->to_process_queue);
  if (read_buffer != NULL)
  {
    DEBUG("PROCESSOR RECEIVED: %.*s\n", read_buffer->size, read_buffer->buffer);
    if (message_buffer_resize(write_buffer, read_buffer->size) != 0)
    {
      fflush(stdout);
      fprintf(stderr, "Ошибка выденения памяти для размещения данных после обработки: %s (%d)\n", strerror(errno), errno);
      release_context(context);
      return;
    }

    for (int i = 0; i < read_buffer->size/2+1; ++i)
    {
      write_buffer->buffer[i] = read_buffer->buffer[read_buffer->size-1 - i];
      write_buffer->buffer[read_buffer->size-1 - i] = read_buffer->buffer[i];
    }
    write_buffer->size = read_buffer->size;
    write_buffer->offset = write_buffer->size;
    DEBUG("PROCESSOR RESULT: %.*s\n", write_buffer->size, write_buffer->buffer);
    message_queue_add_ready_buffer(context->from_process_queue, write_buffer);
    message_queue_release_buffer(context->to_process_queue, read_buffer);
    DEBUG("send from process watcher context=%p\n", context);
    context->from_process_watcher.data = context;
    ev_async_send(context->loop, &(context->from_process_watcher));
    DEBUG("send from process watcher done\n");
  }
  else
  {
    DEBUG("Нет готового буфера\n");
    message_queue_release_buffer(context->from_process_queue, write_buffer);
  }

  DEBUG("%s done\n", __FUNCTION__);
}

int main (int argc, const char * argv[])
{
  struct ev_loop   *main_loop = NULL;
  int               port_number = -1;
  int               thread_status;
  pthread_t         thread_id;
  thread_context_t  thread_context;
  pthread_attr_t    attr;
  ev_async          stop_watcher;

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

  thread_context.port_number = port_number;
  thread_context.loop = ev_loop_new(EVFLAG_AUTO);
  thread_context.stop_watcher = &stop_watcher;

  if (thread_context.loop == NULL)
  {
    err(EXIT_FAILURE, "Ошибка создания цикла событий для потока чтения");
  }

  main_loop = ev_default_loop(0);
  if (main_loop == NULL)
  {
    err(EXIT_FAILURE, "Ошибка создания цикла событий");
  }
  thread_context.main_loop = main_loop;

  thread_context.to_process_queue = message_queue_create(TO_PROCESS_QUEUE_SIZE);
  if (thread_context.to_process_queue == NULL)
  {
    err(EXIT_FAILURE, "Ошибка выделения памяти для передачи данных на обработку");
  }

  thread_context.from_process_queue = message_queue_create(FROM_PROCESS_QUEUE_SIZE);
  if (thread_context.from_process_queue == NULL)
  {
    err(EXIT_FAILURE, "Ошибка выделения памяти для передачи данных на после обработки на запт=ись в сокет");
  }

  pthread_attr_init(&attr);
  thread_status = pthread_create(&thread_id, &attr, socket_routine, (void *)(&thread_context));
  if (thread_status != 0)
  {
    error(EXIT_FAILURE, thread_status, "Ошибка создания потока");
  }
  pthread_attr_destroy(&attr);

  ev_async_init (&(thread_context.to_process_watcher),   &process_data);
  ev_async_init (&(thread_context.from_process_watcher), &send_processed_data);
  ev_async_init (&stop_watcher, &stop_main_loop);

  ev_async_start(main_loop, &stop_watcher);
  ev_async_start(main_loop, &(thread_context.to_process_watcher));
  ev_async_start(thread_context.loop, &(thread_context.from_process_watcher));

  ev_run(main_loop, 0);

  exit(EXIT_SUCCESS);
}

