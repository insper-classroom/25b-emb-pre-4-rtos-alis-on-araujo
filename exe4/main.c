#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include "pico/stdlib.h"
#include <stdio.h>

const int BTN_PIN_R = 28;
const int BTN_PIN_G = 26;

const int LED_PIN_R = 4;
const int LED_PIN_G = 6;

QueueHandle_t xQueueButId;   // Fila do LED R
QueueHandle_t xQueueBtn2;    // Fila do LED G

// guarda o próximo delay de cada botão (atualizado na ISR)
static volatile int next_delay_r = 0;
static volatile int next_delay_g = 0;

static inline int bump_delay(volatile int *d) {
    int v = *d;
    if (v < 1000) v += 100;
    else v = 100;
    *d = v;
    return v;
}

void btn_callback(uint gpio, uint32_t events) {
    if ((events & GPIO_IRQ_EDGE_FALL) == 0) return;  // só na borda de descida

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (gpio == BTN_PIN_R) {
        int val = bump_delay(&next_delay_r);
        xQueueSendFromISR(xQueueButId, &val, &xHigherPriorityTaskWoken);
    } else if (gpio == BTN_PIN_G) {
        int val = bump_delay(&next_delay_g);
        xQueueSendFromISR(xQueueBtn2, &val, &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void led_task_red(void *p) {
    gpio_init(LED_PIN_R);
    gpio_set_dir(LED_PIN_R, GPIO_OUT);

    int delay_ms = 0;

    for (;;) {
        // se chegou novo delay, usa; se não, reaplica o atual
        if (xQueueReceive(xQueueButId, &delay_ms, pdMS_TO_TICKS(10)) == pdTRUE) {
            printf("[R] novo delay: %d ms\n", delay_ms);
        }
        if (delay_ms > 0) {
            gpio_put(LED_PIN_R, 1);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            gpio_put(LED_PIN_R, 0);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void led_task_green(void *p) {
    gpio_init(LED_PIN_G);
    gpio_set_dir(LED_PIN_G, GPIO_OUT);

    int delay_ms = 0;

    for (;;) {
        if (xQueueReceive(xQueueBtn2, &delay_ms, pdMS_TO_TICKS(10)) == pdTRUE) {
            printf("[G] novo delay: %d ms\n", delay_ms);
        }
        if (delay_ms > 0) {
            gpio_put(LED_PIN_G, 1);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            gpio_put(LED_PIN_G, 0);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static void btn_init(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
    gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_FALL, true);
}

int main() {
    stdio_init_all();
    printf("Start RTOS\n");

    // filas para os dois LEDs (até 32 valores pendentes)
    xQueueButId = xQueueCreate(32, sizeof(int));
    xQueueBtn2  = xQueueCreate(32, sizeof(int));

    // inicia botões e registra a mesma ISR para ambos
    btn_init(BTN_PIN_R);
    btn_init(BTN_PIN_G);
    gpio_set_irq_enabled_with_callback(BTN_PIN_R, GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    // como o callback já foi registrado, basta habilitar no G:
    gpio_set_irq_enabled(BTN_PIN_G, GPIO_IRQ_EDGE_FALL, true);

    // tasks dos LEDs
    xTaskCreate(led_task_red,   "LED_R", 256, NULL, 1, NULL);
    xTaskCreate(led_task_green, "LED_G", 256, NULL, 1, NULL);

    vTaskStartScheduler();
    while (true) { /* nunca chega aqui */ }
}
