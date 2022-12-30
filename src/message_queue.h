/*
 * Очередь сообщений
 */

#ifndef __MESSAGE_QUEUE_H__
#define __MESSAGE_QUEUE_H__

#include <stdlib.h>

#include "message_buffer.h"

/*
 * Очередь сообщений
 */
struct message_queue_t;
typedef struct message_queue_t message_queue_t;

/*
 * инициализация очереди сообщений
 */
message_queue_t * message_queue_create(size_t size);

/*
 * освободить память, выделенную для очереди сообщений
 */
void message_queue_destroy(message_queue_t * queue);

/*
 * получить свободный буфер из очереди сообщений
 * если свободных буферов нет, возвращается NULL
 */
message_buffer_t * message_queue_get_free_buffer(message_queue_t * queue);

/*
 * получить заполненный буфер из очереди сообщений
 * если буферов нет, возвращается NULL
 */
message_buffer_t * message_queue_get_ready_buffer(message_queue_t * queue);

/*
 * пометить буфер как "заполненный"
 */
void message_queue_add_ready_buffer(message_queue_t * queue, message_buffer_t * buffer);

/*
 * Вернуть неиспользуемый буфер в очередь
 */
void message_queue_put_back_buffer(message_queue_t * queue, message_buffer_t * buffer);

/*
 * пометить буфер как "свободный"
 */
void message_queue_release_buffer(message_queue_t * queue, message_buffer_t * buffer);

#endif // __MESSAGE_QUEUE_H__
