#include "task_params.h"

#include <ev.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <error.h>
#include <err.h>
#include <errno.h>
#include <ctype.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef _DEBUG
#define DEBUG(msg...) fprintf(stderr, msg)
#else
#define DEBUG(mag...)
#endif

/*
 * Закрыть сокет
 */
void close_socket(int sock_id)
{
  shutdown(sock_id, SHUT_RDWR);
  close(sock_id);
}

/*
 * Запись данных
 */
static int send_data(int fd, char * buffer, int size)
{
  int bytes = 0;
  int snt = 0;

  DEBUG("send data\n");
  while (size > 0)
  {
    snt = send(fd, buffer + bytes, size, 0);

    if (snt <= 0)
    {
      if (errno != EAGAIN && errno != EWOULDBLOCK)
      {
        return -1;
      }
      usleep(100);
      continue;
    }

    size  -= snt;
    bytes += snt;
  }

  DEBUG("send %d bytes\n", bytes);
  return bytes;
}

/*
 * Чтение данных
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
 * Чтение данных
 */
int read_data(int fd, char * buffer, int size, double timeout_sec)
{
  int received = 0;

  int timeout_usec = (timeout_sec * 1000000); // сек -> мксек
  int timeout = timeout_usec / 10;

  while (size > 0) // пока не прочитали все запрашиваемые данные
  {
    size_t bytes = get_bytes_available(fd);
    if (bytes == 0)
    {
      // нет доступных данных
      if (timeout_usec <= 0)
      {
        // и их уже не нужно ждать
        break;
      }

      usleep(timeout);
      timeout_usec -= timeout;
      continue;
    }

    if (bytes > size) // доступно больше данных, чем нужно
      bytes = size;

    bytes = recv(fd, buffer + received, bytes, 0);
    if (bytes < 0)
    {
      return -1;
    }
    received += bytes;
    size     -= bytes;
  }

  return received;
}

/*
 *
 */
int main (int argc, const char * argv[])
{
  TaskParams params;
  struct sockaddr_in addr = {0};
  int sock_id;
  char * data;
  char * data2;

  if (ProcessCmdLine(&params, argc, argv) != 0)
  {
    return 1;
  }

  DEBUG("start program\n");

  addr.sin_family = AF_INET;
  addr.sin_port = htons(params.port_);

  if (inet_aton(params.ip_, &(addr.sin_addr)) == 0)
  {
    err(EXIT_FAILURE, "Указан неверный адрес");
  }

  sock_id = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_id < 0)
  {
    err(EXIT_FAILURE, "Ошибка создания сокета");
  }

  if (connect(sock_id, (const struct sockaddr*)(&addr), sizeof(struct sockaddr_in)) != 0)
  {
    close(sock_id);
    err(EXIT_FAILURE, "Ошибка подключения к %s:%d", params.ip_, params.port_);
  }

  DEBUG("Подключились к %s:%d\n", params.ip_, params.port_);

  data = calloc(params.messageSize_, sizeof(char));
  if (data == NULL)
  {
    close_socket(sock_id);
    err(EXIT_FAILURE, "Ошибка выделения памяти для записи данных сообщения");
  }
  for (int i = 0; i < params.messageSize_; ++i)
  {
    data[i] = (i%10) + '0';
  }

  DEBUG("Запсь сообщения размером %d байт\n", params.messageSize_);
  if (send_data(sock_id, data, params.messageSize_) != params.messageSize_)
  {
    int err = errno;
    free(data);
    close_socket(sock_id);
    error(EXIT_FAILURE, err, "Ошибка отправки данных");
  }
  DEBUG("Запсь сообщения размером %d байт завершена\n", params.messageSize_);

  data2 = calloc(params.messageSize_, sizeof(char));
  if (data2 == NULL)
  {
    free(data);
    close_socket(sock_id);
    err(EXIT_FAILURE, "Ошибка выделения памяти для приема сообщения");
  }

  DEBUG("Чтение сообщения размером %d байт\n", params.messageSize_);
  if (read_data(sock_id, data2, params.messageSize_, 5.0) != params.messageSize_)
  {
    int err = errno;
    free(data);
    free(data2);
    close_socket(sock_id);
    error(EXIT_FAILURE, err, "Ошибка чтения данных");
  }

  DEBUG("Получено сообщение\n");
#ifdef _DEBUG
  if (memcmp(data, data2, params.messageSize_) != 0)
  {
    fprintf(stderr, "Отправленное и полученное сообщения не совпадают\n");
  }
#endif

  free(data);
  free(data2);
  close_socket(sock_id);

  exit(EXIT_SUCCESS);
}
