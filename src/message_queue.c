/*
 * Очередь сообщений
 */


#include "message_queue.h"
#include "message_buffer.h"

#include <errno.h>
#include <pthread.h>
#include <assert.h>

#ifdef _DEBUG
#include <stdio.h>
#include <pthread.h>
#define DEBUG(msg...) fprintf(stderr, "%u: ", (int)(pthread_self())); fprintf(stderr, msg)
#else
#define DEBUG(mag...)
#endif

/*
 * Внутренние структуры
 *
 * Однонаправленный список
 */
struct buffers_list_element_t;
typedef struct buffers_list_element_t buffers_list_element_t;

/*
 * Внутренние структуры
 *
 * Элемент однонаправленного списка
 */
struct buffers_list_t
{
  buffers_list_element_t * first;
  buffers_list_element_t * last;
}; // struct buffers_list_t
typedef struct buffers_list_t buffers_list_t;

/*
 * Очередь сообщений
 */
struct message_queue_t
{
  pthread_mutex_t lock;
  buffers_list_element_t * buffers;
  size_t size;

  buffers_list_t free_buffers;
  buffers_list_t ready_buffers;
  buffers_list_t busy_buffers;
}; // struct message_buffers_set_t

/*
 * Внутренние структуры
 *
 * Однонаправленный список. Реализация
 */
struct buffers_list_element_t
{
  buffers_list_t * list;
  message_buffer_t buffer;
  buffers_list_element_t * next;
  buffers_list_element_t * prev;
}; // struct buffers_list_element_t
typedef struct buffers_list_element_t buffers_list_element_t;

static int buffers_list_element_init(buffers_list_element_t * element)
{
  if (message_buffer_init(&(element->buffer), 0) != 0)
    return -1;
  element->list = NULL;
  element->next = element->prev = NULL;
  return 0;
}

static void buffers_list_element_destroy(buffers_list_element_t * element)
{
  message_buffer_destroy(&(element->buffer));
}

static void buffers_list_init(buffers_list_t * list)
{
  list->first = list->last = NULL;
}

static void buffers_list_push_back(buffers_list_t * list, buffers_list_element_t * element)
{
  element->list = list;
  if (list->last == NULL)
  { // последнего элемента нет - список пуст (first тоже пустой)
    list->first = element;
    list->last = list->first;
    element->next = element->prev = NULL;
  }
  else
  {
    list->last->next = element;
    element->prev = list->last;
    element->next = NULL;
    list->last = element;
  }
}

static void buffers_list_push_front(buffers_list_t * list, buffers_list_element_t * element)
{
  element->list = list;
  if (list->last == NULL)
  { // последнего элемента нет - список пуст (first тоже пустой)
    list->first = element;
    list->last = list->first;
    element->next = element->prev = NULL;
  }
  else
  {
    list->first->prev = element;
    element->prev = NULL;
    element->next = list->first;
    list->first = element;
  }
}

static void buffers_list_remove_element(buffers_list_t * list, buffers_list_element_t * element)
{
  DEBUG("buffers_list_remove_element(buffers_list_t * list = %p, buffers_list_element_t * element = %p)\n", list, element);
  assert(element->list == list);

  if (element == list->first || element == list->last)
  {
    if (element == list->first)
    {
      DEBUG("remove first\n");
      list->first = element->next;
    }
    if (element == list->last)
    {
      DEBUG("remove last\n");
      list->last = element->prev;
    }
  }
  else
  {
    element->prev->next = element->next;
    element->next->prev = element->prev;
  }
  element->prev = element->next = NULL;
  element->list = NULL;
  DEBUG("buffers_list_remove_element(buffers_list_t * list = %p, buffers_list_element_t * element = %p) done\n", list, element);
}

/*
 * инициализация очереди сообщений
 */
message_queue_t * message_queue_create(size_t size)
{
  message_queue_t * queue;
  pthread_mutexattr_t lock_attr;

  queue = calloc(1, sizeof(message_queue_t));
  if (queue == 0)
    return NULL;

  if (pthread_mutexattr_init(&lock_attr) != 0)
  {
    free(queue);
    return NULL;
  }

  if (pthread_mutex_init(&(queue->lock), &lock_attr) != 0)
  {
    int error = errno;
    pthread_mutexattr_destroy(&lock_attr);
    free(queue);
    errno = error;
    return NULL;
  }
  pthread_mutexattr_destroy(&lock_attr);

  queue->buffers = calloc(size, sizeof(buffers_list_element_t));
  if (queue->buffers == NULL)
  {
    int error = errno;
    pthread_mutex_destroy(&(queue->lock));
    free(queue);
    errno = error;
    return NULL;
  }

  buffers_list_init(&(queue->free_buffers));
  buffers_list_init(&(queue->ready_buffers));
  buffers_list_init(&(queue->busy_buffers));

  queue->size = size;
  for (int i = 0; i < size; ++i)
  {
    buffers_list_element_t * buffer = queue->buffers + i;
    if (buffers_list_element_init(buffer) != 0)
    {
      int error = errno;
      pthread_mutex_destroy(&(queue->lock));
      free(queue);
      errno = error;
      return NULL;
    }
    buffers_list_push_back(&(queue->free_buffers), buffer);
  }

  return queue;
}

/*
 * освободить память, выделенную для очереди сообщений
 */
void message_queue_destroy(message_queue_t * queue)
{
  if (queue->buffers != NULL)
  {
    for (int i = 0; i < queue->size; ++i)
    {
      buffers_list_element_destroy(queue->buffers + i);
    }
    free(queue->buffers);
  }
  pthread_mutex_destroy(&(queue->lock));
  free(queue);
}

/*
 * получить свободный буфер из очереди сообщений
 * если свободных буферов нет, возвращается NULL
 */
message_buffer_t * message_queue_get_free_buffer(message_queue_t * queue)
{
  buffers_list_element_t * element = NULL;

  pthread_mutex_lock(&(queue->lock));
  element = queue->free_buffers.first;
  if (element != NULL)
  {
    buffers_list_remove_element(&(queue->free_buffers), element);
    buffers_list_push_back(&(queue->busy_buffers), element);
  }
  pthread_mutex_unlock(&(queue->lock));
  return element != NULL ? &(element->buffer) : NULL;
}

/*
 * получить заполненный буфер из очереди сообщений
 * если буферов нет, возвращается NULL
 */
message_buffer_t * message_queue_get_ready_buffer(message_queue_t * queue)
{
  buffers_list_element_t * element = NULL;

  pthread_mutex_lock(&(queue->lock));
  DEBUG("message_queue_get_ready_buffer(message_queue_t * queue = %p)\n", queue);
  element = queue->ready_buffers.first;
  if (element != NULL)
  {
    buffers_list_remove_element(&(queue->ready_buffers), element);
    buffers_list_push_back(&(queue->busy_buffers), element);
  }
  else
  {
    DEBUG("message_queue_get_ready_buffer ready_buffers is empty\n");
  }
  pthread_mutex_unlock(&(queue->lock));

  return element != NULL ? &(element->buffer) : NULL;
}

/*
 * пометить буфер как "заполненный"
 */
void message_queue_add_ready_buffer(message_queue_t * queue, message_buffer_t * buffer)
{
  buffers_list_element_t * element = NULL;

  pthread_mutex_lock(&(queue->lock));
  DEBUG("message_queue_add_ready_buffer(message_queue_t * queue = %p, message_buffer_t * buffer = %p)\n", queue, buffer);
  element = queue->busy_buffers.last;
  while (element)
  {
    if (buffer == &(element->buffer))
      break;
    element = element->prev;
  }
  assert(element);

  buffers_list_remove_element(&(queue->busy_buffers), element);
  buffers_list_push_back(&(queue->ready_buffers), element);
  DEBUG("message_queue_add_ready_buffer(message_queue_t * queue = %p, message_buffer_t * buffer = %p) ready.first = %p done\n",
        queue, buffer, queue->ready_buffers.first);
  pthread_mutex_unlock(&(queue->lock));
}

/*
 * 
 */
void message_queue_put_back_buffer(message_queue_t * queue, message_buffer_t * buffer)
{
  buffers_list_element_t * element = NULL;

  if (buffer->size == 0)
    message_queue_release_buffer(queue, buffer);

  pthread_mutex_lock(&(queue->lock));
  element = queue->busy_buffers.last;
  while (element)
  {
    if (buffer == &(element->buffer))
      break;
    element = element->prev;
  }
  assert(element);

  buffers_list_remove_element(&(queue->busy_buffers), element);
  buffers_list_push_front(&(queue->ready_buffers), element);
  pthread_mutex_unlock(&(queue->lock));
}

/*
 * пометить буфер как "свободный"
 */
void message_queue_release_buffer(message_queue_t * queue, message_buffer_t * buffer)
{
  buffers_list_element_t * element = NULL;

  pthread_mutex_lock(&(queue->lock));
  DEBUG("message_queue_release_buffer(message_queue_t * queue = %p, message_buffer_t * buffer = %p)\n", queue, buffer);
  element = queue->busy_buffers.last;
  while (element)
  {
    if (buffer == &(element->buffer))
      break;
    element = element->prev;
  }
  assert(element);

  buffers_list_remove_element(&(queue->busy_buffers), element);
  buffers_list_push_front(&(queue->free_buffers), element);
  DEBUG("message_queue_release_buffer(message_queue_t * queue = %p, message_buffer_t * buffer = %p) done\n", queue, buffer);
  pthread_mutex_unlock(&(queue->lock));
}
