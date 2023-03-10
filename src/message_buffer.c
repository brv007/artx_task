
#include "message_buffer.h"

/*
 * Буфер сообщения
 */

// инициализация буфера - выделение памяти
int message_buffer_init(message_buffer_t * buffer, size_t capacity)
{
  buffer->size = buffer->offset = 0;
  buffer->capacity = capacity;
  if (capacity > 0)
  {
    buffer->buffer = calloc(capacity, sizeof(char));
    if (buffer->buffer == NULL)
      return -1;
  }
  else
  {
    buffer->buffer = NULL;
  }
  return 0;
}

// освобождение памяти
void message_buffer_destroy(message_buffer_t * buffer)
{
  if (buffer->buffer)
    free(buffer->buffer);

  buffer->buffer = NULL;
  buffer->size = buffer->offset = 0;
  buffer->capacity = 0;
}

// изменение размера буфера
int message_buffer_resize(message_buffer_t * buffer, size_t capacity)
{
  if (capacity > buffer->capacity)
  {
    char * ptr = NULL;
    ptr = realloc(buffer->buffer, capacity * sizeof(char));
    if (ptr == NULL)
      return -1;
    buffer->buffer = ptr;
    buffer->capacity = capacity;
  }
  else if (capacity == 0)
  {
    message_buffer_destroy(buffer);
  }
  buffer->size = buffer->offset = 0;
  return 0;
}
