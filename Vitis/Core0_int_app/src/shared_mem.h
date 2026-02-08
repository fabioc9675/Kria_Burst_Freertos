#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include <stdint.h>

#define SHARED_MEM_BASE 0x70000000
#define LOG_BUFFER_SIZE 25
#define MSG_LEN         128

typedef struct {
    char text[MSG_LEN];
    uint32_t core_id;
    uint32_t counter; // Opcional: para mantener el conteo
} log_entry_t;

typedef struct {
    log_entry_t buffer[LOG_BUFFER_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t is_ready;
    uint32_t counters[6];
} shared_log_t;

#define SHARED_LOG ((volatile shared_log_t *)(uintptr_t)SHARED_MEM_BASE)

#endif
