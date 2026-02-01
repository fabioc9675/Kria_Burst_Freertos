#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include <stdint.h>

#define SHARED_MEM_BASE 0x70000000
#define LOG_BUFFER_SIZE 256
#define MSG_LEN         512

#define DATA_BUFFER_SIZE 48000  // Data size
#define NUM_BUFFERS      4     // Pipeline de 4 etapas (Ping-Pong doble)

/* *****************************************************************
 * **** Estructura para el pipeline
 * *****************************************************************/

typedef enum {
	BUF_EMPTY,      // Listo para que Core 0 escriba
	BUF_READY,      // Lleno, listo para que Core 3 procese
	BUF_PROCESSING  // Core 3 lo esta usando actualmente
} buffer_status_t;

typedef struct {
	uint32_t payload[DATA_BUFFER_SIZE];
	volatile uint32_t status; // buffer_status_t
	uint32_t sequence_id;     // Para detectar si perdemos paquetes
	uint32_t checksum;
} data_packet_t;

typedef struct {
	data_packet_t packets[NUM_BUFFERS];
	volatile uint32_t write_idx; // Usado por Core 0
	volatile uint32_t read_idx;  // Usado por Core 3
} pipeline_data_t;

/* *****************************************************************
 * **** Estructura para el Log
 * *****************************************************************/

typedef struct {
	char text[MSG_LEN];
	uint32_t core_id;
	uint32_t counter; // Opcional: para mantener el conteo
} log_entry_t;

typedef struct {
	log_entry_t buffer[LOG_BUFFER_SIZE];
	volatile uint32_t head;
	volatile uint32_t tail;
	// Puedes mantener esto si aún quieres ver contadores individuales
	uint32_t counters[6];
} shared_log_t;

// define para el log
#define SHARED_LOG ((volatile shared_log_t *)(uintptr_t)SHARED_MEM_BASE)

// Nueva base para el pipeline de datos (desplazada de los logs)
#define PIPELINE_BASE (SHARED_MEM_BASE + sizeof(shared_log_t))
#define DATA_PIPELINE ((volatile pipeline_data_t *)(uintptr_t)PIPELINE_BASE)

#endif
