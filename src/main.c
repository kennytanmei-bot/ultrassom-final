#include <zephyr/kernel.h> // Kernel do Zephyr (k_msleep, k_busy_wait, etc.)
#include <zephyr/device.h> // Acesso a dispositivos (GPIO, etc.)
#include <zephyr/drivers/gpio.h>
#include <pwm_z42.h>

#include <ultrassonic.h>

#define TPM_MODULE 1000
#define m1 700
#define m2 750                                                                                  

int main(void)
{   
    ultrasonic_init();

    pwm_tpm_Init(TPM0,
                 TPM_PLLFLL,
                 TPM_MODULE,
                 TPM_CLK,
                 PS_64,
                 EDGE_PWM);

    pwm_tpm_Ch_Init(TPM0, 3, TPM_PWM_H, GPIOC, 4);
    pwm_tpm_Ch_Init(TPM0, 5, TPM_PWM_H, GPIOD, 5);

    pwm_tpm_Ch_Init(TPM0, 1, TPM_PWM_H, GPIOA, 4);
    pwm_tpm_Ch_Init(TPM0, 2, TPM_PWM_H, GPIOA, 5);

    while (1)
    {
        float d = ultrasonic_read_cm();

        if (d < 0)
        {
            printk("Timeout\n");
            continue;
        }

        printk("Distancia: %d cm\n", (int)d);

        if (d > 23.5f)
        {
            pwm_tpm_CnV(TPM0, 1, 0);
            pwm_tpm_CnV(TPM0, 2, m1);

            pwm_tpm_CnV(TPM0, 3, m2);
            pwm_tpm_CnV(TPM0, 5, 0);
        }
        else if (d < 21.5f)
        {
            pwm_tpm_CnV(TPM0, 1, m1 + 50);
            pwm_tpm_CnV(TPM0, 2, 0);

            pwm_tpm_CnV(TPM0, 3, 0);
            pwm_tpm_CnV(TPM0, 5, m2);
        }
        else
        {
            pwm_tpm_CnV(TPM0, 1, 0);
            pwm_tpm_CnV(TPM0, 2, 0);

            pwm_tpm_CnV(TPM0, 3, 0);
            pwm_tpm_CnV(TPM0, 5, 0);
        }
    }
}