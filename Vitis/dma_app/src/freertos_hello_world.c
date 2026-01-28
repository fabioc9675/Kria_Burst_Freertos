#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xgpio.h"
#include "xil_io.h"

// --- Definiciones Globales ---
#define BRAM_IDA_ADDR      0x82000000
#define SAMPLES            48000
#define PRINT_SAMPLES      1000

// Instancia global del GPIO para que sea visible en todas las funciones
XGpio Gpio;

// Buffer alineado para optimizar el uso de NEON con memcpy
s16 local_data[SAMPLES] __attribute__ ((aligned (64)));

// --- Función de Inicialización ---
int init_hw() {
    int status;
    // Inicializa el GPIO usando el ID del Address Editor (HPM0_LPD)
    status = XGpio_Initialize(&Gpio, XPAR_AXI_GPIO_0_DEVICE_ID);
    if (status != XST_SUCCESS) return XST_FAILURE;

    // Canal 1: Salida (Start), Canal 2: Entrada (Done)
    XGpio_SetDataDirection(&Gpio, 1, 0x0); // Todo salidas
    XGpio_SetDataDirection(&Gpio, 2, 0x1); // Bit 0 como entrada

    return XST_SUCCESS;
}

// --- Tarea de Impresión Serial ---
void vSerialPrintTask(void *pvParameters) {
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(10000); // 10 segundos

    xLastWakeTime = xTaskGetTickCount();
    xil_printf("Iniciando captura y lectura por memcpy...\r\n");

    // Prueba de escritura directa (bypass total de drivers)
    Xil_Out32(0x82000000, 0x12345678);
    uint32_t val = Xil_In32(0x82000000);

    if (val == 0x12345678) {
        xil_printf("¡El PS y la BRAM funcionan sin inicializar!\r\n");
    } else {
        xil_printf("ERROR: No se puede escribir en la BRAM.\r\n");
    }

    while (1) {
        // 1. Pulso de inicio al PL
        XGpio_DiscreteWrite(&Gpio, 1, 0x1);
        // XGpio_DiscreteWrite(&Gpio, 1, 0x0);

        // 2. Espera al hardware (Bit DONE)
        // El PL tardará aprox 480ms en llenar la BRAM a 100kHz
        while ((XGpio_DiscreteRead(&Gpio, 2) & 0x1) == 0) {
            vTaskDelay(pdMS_TO_TICKS(10)); // No bloqueamos el CPU totalmente
        }

        // 3. Copia masiva BRAM -> RAM (Muy rápido con memcpy)
//        memcpy(local_data, (void*)BRAM_IDA_ADDR, SAMPLES * sizeof(s16));
//
//        // 4. Impresión Serial (Operación lenta, por eso usamos memcpy antes)
//        xil_printf("--- Datos del DDS (Primeros 1000) ---\r\n");
//        for (int i = 0; i < PRINT_SAMPLES; i++) {
//            xil_printf("%d, ", (int)local_data[i]);
//        }

        uint32_t *mem_ptr = (uint32_t*)BRAM_IDA_ADDR;
        for(int i=0; i < 1000; i++) {
            // Leemos el registro de 32 bits y extraemos los 16 bajos
            int16_t sample = (int16_t)(mem_ptr[i] & 0xFFFF);
            xil_printf("%d, ", sample);
        }

        xil_printf("\r\n--- Fin de transmision ---\r\n");

        // Esperar hasta completar el ciclo de 10 segundos
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

int main() {
    // 1. Deshabilitar interrupciones durante la inicialización
    vPortEnterCritical();

    xil_printf("\r\n--- Iniciando Sistema Kria AMP (BRAM + FreeRTOS) ---\r\n");

    // 2. Inicializar Hardware (GPIO y perifericos)
    if (init_hw() != XST_SUCCESS) {
        xil_printf("ERROR: No se pudo inicializar el hardware.\r\n");
        while(1); // Bloqueo de seguridad
    }
    xil_printf("Hardware inicializado correctamente.\r\n");

    // 3. Crear la Tarea de Impresion Serial
    // Prioridad tskIDLE_PRIORITY + 1 es suficiente para esta tarea
    BaseType_t xReturned = xTaskCreate(
        vSerialPrintTask,       // Funcion que implementa la tarea
        "SerialTask",           // Nombre descriptivo
        2048,                   // Stack size (ajustado para xil_printf largo)
        NULL,                   // Parametros
        tskIDLE_PRIORITY + 1,   // Prioridad
        NULL                    // Handler
    );

    if (xReturned != pdPASS) {
        xil_printf("ERROR: No se pudo crear la tarea.\r\n");
        while(1);
    }

    // 4. Salir de seccion critica
    vPortExitCritical();

    xil_printf("Lanzando Scheduler de FreeRTOS...\r\n");

    // 5. Iniciar el Scheduler
    vTaskStartScheduler();

    // El codigo nunca deberia llegar aqui
    for (;;);
    return 0;
}
