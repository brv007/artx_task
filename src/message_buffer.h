/*
 * Буфер сообщения
 */

#ifndef __MESSAGE_BUFFER_H__
#define __MESSAGE_BUFFER_H__

#include <stdlib.h>

struct message_buffer_t
{
    char *buffer;     // Сообщение
    int size;         // Размер данных в буфере
    int offset;       // Указатель на начало при записи
    size_t capacity;  // Выделенный размер буфера
}; // struct message_buffer_t

typedef struct message_buffer_t message_buffer_t;

// инициализация буфера - выделение памяти
int message_buffer_init(message_buffer_t * buffer, size_t capacity);
// освобождение памяти
void message_buffer_destroy(message_buffer_t * buffer);

#endif // __MESSAGE_BUFFER_H__