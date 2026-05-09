#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t *buffer;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
} ring_buffer_t;

void ring_buffer_init(ring_buffer_t *ring, uint8_t *buffer, uint32_t size);
uint32_t ring_buffer_available(const ring_buffer_t *ring);
uint32_t ring_buffer_free(const ring_buffer_t *ring);
uint32_t ring_buffer_free_space(const ring_buffer_t *ring);
bool ring_buffer_write(ring_buffer_t *ring, const uint8_t *data, uint32_t len);
uint32_t ring_buffer_peek(const ring_buffer_t *ring, uint8_t *data, uint32_t len);
uint32_t ring_buffer_read(ring_buffer_t *ring, uint8_t *data, uint32_t len);
void ring_buffer_drop(ring_buffer_t *ring, uint32_t len);
void ring_buffer_clear(ring_buffer_t *ring);

#endif /* RING_BUFFER_H */
