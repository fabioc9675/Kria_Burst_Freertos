#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include <stdint.h>

#define SHARED_MEM_BASE 0x70000000
#define LOG_BUFFER_SIZE 512
#define MSG_LEN         512

#define DATA_BUFFER_SIZE 4096  // Data size
#define NUM_BUFFERS      32     // Pipeline de 4 etapas (Ping-Pong doble)

/* *****************************************************************
 * **** Estructura para el pipeline
 * *****************************************************************/

typedef enum {
	BUF_EMPTY,          // Libre para Core 0
	BUF_READY_FOR_FFT,  // Lleno por Core 0, esperando al Core 1
	BUF_READY_FOR_OUT,  // Procesado por Core 1, esperando al Core 3
} buffer_status_t;

typedef struct {
	uint32_t payload[DATA_BUFFER_SIZE];
	volatile uint32_t status; // buffer_status_t
	uint32_t sequence_id;     // Para detectar si perdemos paquetes
	uint32_t checksum;
} data_packet_t;

typedef struct {
	data_packet_t packets[NUM_BUFFERS];
	volatile uint32_t write_idx; // Solo Core 0 (Productor)
	volatile uint32_t fft_idx;   // Solo Core 1 (Procesador) <-- NUEVO
	volatile uint32_t read_idx;  // Solo Core 3 (Consumidor)
} pipeline_data_t;

typedef struct {
	volatile uint32_t core0_ready;
	volatile uint32_t core1_ready;
	volatile uint32_t core3_ready;
	volatile uint32_t system_go; // El "pistoletazo" de salida
} sync_barrier_t;

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

// 1. LOGS: Ocupa desde 0x70000000 hasta 0x7002FFFF (~192 KB)
#define SHARED_LOG      ((volatile shared_log_t *)(uintptr_t)SHARED_MEM_BASE)

// 2. BARRERA: La movemos a un offset de 256 KB (0x40000)
// Aquí no hay riesgo de que los logs lleguen nunca.
#define SYNC_BARRIER    ((volatile sync_barrier_t *)(SHARED_MEM_BASE + 0x40000))

// 3. PIPELINE: Lo movemos a un offset de 320 KB (0x50000)
// Esto da espacio de sobra y esta alineado a nivel de hardware.
#define PIPELINE_BASE   (SHARED_MEM_BASE + 0x50000)
#define DATA_PIPELINE   ((volatile pipeline_data_t *)(uintptr_t)PIPELINE_BASE)

#endif
