#ifndef ULTRASONIC_H
#define ULTRASONIC_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <pwm_z42.h>

/* =========================================================
   CONFIGURAÇÕES
========================================================= */

/* TRIG */
#define ULTRA_TRIG_NODE DT_NODELABEL(gpioa)
#define ULTRA_TRIG_PIN  12

/* ECHO */
#define ULTRA_ECHO_PORT GPIOE
#define ULTRA_ECHO_PIN  20

/* TPM */
#define ULTRA_TPM           TPM1
#define ULTRA_IRQ_LINE      TPM1_IRQn
#define ULTRA_IRQ_PRIORITY  1

/* =========================================================
   VARIÁVEIS GLOBAIS
========================================================= */

volatile uint16_t ultra_t_start = 0;
volatile uint16_t ultra_t_end   = 0;

volatile uint8_t ultra_estado = 0;
volatile uint8_t ultra_pronto = 0;

static const struct device *ultra_trig_dev;

/* =========================================================
   ISR
========================================================= */

void ultrasonic_isr(void *arg)
{
    if (ULTRA_TPM->STATUS & TPM_STATUS_CH0F_MASK)
    {
        ULTRA_TPM->STATUS |= TPM_STATUS_CH0F_MASK;

        if (ultra_estado == 0)
        {
            ultra_t_start = ULTRA_TPM->CONTROLS[0].CnV;
            ultra_estado = 1;
        }
        else
        {
            ultra_t_end = ULTRA_TPM->CONTROLS[0].CnV;

            ultra_pronto = 1;
            ultra_estado = 0;
        }
    }
}

/* =========================================================
   GERA PULSO TRIG
========================================================= */

static inline void ultrasonic_trigger(void)
{
    gpio_pin_set(ultra_trig_dev, ULTRA_TRIG_PIN, 0);
    k_busy_wait(2);

    gpio_pin_set(ultra_trig_dev, ULTRA_TRIG_PIN, 1);
    k_busy_wait(10);

    gpio_pin_set(ultra_trig_dev, ULTRA_TRIG_PIN, 0);
}

/* =========================================================
   CONVERSÃO TEMPO -> DISTÂNCIA
========================================================= */

static inline float ultrasonic_calculate(uint16_t start,
                                         uint16_t end)
{
    uint16_t delta;

    if (end >= start)
        delta = end - start;
    else
        delta = (65535 - start) + end;

    /*
       PS_128
       Tick ≈ 2.67 us
    */
    float tempo_us = delta * 2.67f;

    /*
       velocidade do som:
       0.0343 cm/us
    */
    float distancia = (tempo_us * 0.0343f) / 2.0f;

    return distancia;
}

/* =========================================================
   INICIALIZAÇÃO
========================================================= */

static inline int ultrasonic_init(void)
{
    ultra_trig_dev = DEVICE_DT_GET(ULTRA_TRIG_NODE);

    if (!device_is_ready(ultra_trig_dev))
    {
        printk("Erro TRIG\n");
        return -1;
    }

    gpio_pin_configure(
        ultra_trig_dev,
        ULTRA_TRIG_PIN,
        GPIO_OUTPUT
    );

    /* IRQ */

    IRQ_CONNECT(
        ULTRA_IRQ_LINE,
        ULTRA_IRQ_PRIORITY,
        ultrasonic_isr,
        NULL,
        0
    );

    irq_enable(ULTRA_IRQ_LINE);

    /* TPM */

    pwm_tpm_Init(
        ULTRA_TPM,
        TPM_PLLFLL,
        65535,
        TPM_CLK,
        PS_128,
        EDGE_PWM
    );

    /* Input Capture */

    pwm_tpm_Ch_Init(
        ULTRA_TPM,
        0,
        TPM_INPUT_CAPTURE_BOTH |
        TPM_CHANNEL_INTERRUPT,
        ULTRA_ECHO_PORT,
        ULTRA_ECHO_PIN
    );

    return 0;
}

/* =========================================================
   LEITURA DA DISTÂNCIA
========================================================= */

static inline float ultrasonic_read_cm(void)
{
    ultra_pronto = 0;

    ultrasonic_trigger();

    int timeout = 100000;

    while (!ultra_pronto && timeout--)
    {
        k_busy_wait(1);
    }

    if (!ultra_pronto)
    {
        return -1.0f;
    }

    return ultrasonic_calculate(
        ultra_t_start,
        ultra_t_end
    );
}

#endif