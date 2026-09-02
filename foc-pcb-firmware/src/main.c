#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/mcpwm_prelude.h"
#include "esp_adc/adc_continuous.h"
#include "web_ui.h"
#include "wifi_credentials.h"

#define TAG "FOC_DRIVER"
#define MAX_STA_CONN   4


// =========================================================================
// 1. PIN DEFINITIONS (ESP32-S3-WROOM-1 N16R8 PINOUT)
// =========================================================================
// SPI Interface (DRV8323S Gate Driver Configuration)
#define PIN_SPI_CS          10  // SPI Chip Select (Active LOW)
#define PIN_SPI_CLK         11  // SPI Serial Clock
#define PIN_SPI_MOSI        12  // Master Out Slave In (SDI on DRV8323)
#define PIN_SPI_MISO        13  // Master In Slave Out (SDO on DRV8323)

// I2C Interface (AS5600 Magnetic Encoder)
#define PIN_I2C_SDA         1   // I2C Data
#define PIN_I2C_SCL         2   // I2C Clock

// DRV8323 Driver Control & Status
#define PIN_DRV_EN          8   // Gate Driver Enable (Active HIGH)
#define PIN_DRV_FAULT       9   // Fault Status from DRV (Active LOW, pullup)

// 3-Phase MCPWM High-Side Gate Drive Pins (DRV8323 INHx)
#define PIN_INHA            15  // Phase A High-Side PWM
#define PIN_INHB            16  // Phase B High-Side PWM
#define PIN_INHC            17  // Phase C High-Side PWM

// DRV8323 Low-Side Phase Enable Lines (DRV8323 INLx)
// In 3x PWM Mode, holding INLx HIGH enables complementary switching with auto dead-time
#define PIN_INLA            18  // Phase A Low-Side Enable
#define PIN_INLB            21  // Phase B Low-Side Enable
#define PIN_INLC            47  // Phase C Low-Side Enable

// ADC1 Current Sensing Channels (ADC1 on ESP32-S3: GPIO 1..10)
#define PIN_SOA_GPIO        4   // Phase A Current Sense Shunt
#define PIN_SOA_ADC_CH      ADC_CHANNEL_3

#define PIN_SOB_GPIO        5   // Phase B Current Sense Shunt
#define PIN_SOB_ADC_CH      ADC_CHANNEL_4

#define PIN_SOC_GPIO        6   // Phase C Current Sense Shunt
#define PIN_SOC_ADC_CH      ADC_CHANNEL_5

// =========================================================================
// 2. MOTOR & CONTROL LOOP PARAMETERS
// =========================================================================
#define POLE_PAIRS              7          // Number of magnetic pole pairs
#define SUPPLY_VOLTAGE          24.0f      // DC Bus Voltage (Volts)

// Current Sensing Constants
#define SHUNT_RESISTANCE_OHMS   0.010f     // 10 mOhm low-side current shunts
#define CSA_GAIN_V_PER_V        10.0f      // DRV8323 Current Sense Amp Gain (10 V/V)
#define CSA_BIAS_VOLTS          1.65f      // VREF/2 midpoint bias (1.65V at 3.3V VREF)

// Center-Aligned MCPWM Configuration (25 kHz switching frequency)
#define MCPWM_TIMER_RES_HZ      40000000   // 40 MHz MCPWM clock resolution
#define PWM_FREQ_HZ             25000      // 25 kHz PWM frequency
// Up-Down count mode: f_pwm = f_timer / (2 * period_ticks) => 40MHz / (2 * 25kHz) = 800 ticks
#define PWM_MAX_DUTY            (MCPWM_TIMER_RES_HZ / (2 * PWM_FREQ_HZ)) // 800 ticks

// =========================================================================
// 3. PERIPHERAL HANDLES & FOC STATE
// =========================================================================
static spi_device_handle_t      drv_spi;
static i2c_master_dev_handle_t  as5600_handle;
static mcpwm_cmpr_handle_t      comparators[3];
static adc_continuous_handle_t  adc_handle;

// FOC Targets, Measurements, and Runtime Tunable Parameters
volatile float    target_Vq            = 0.0f;  // Torque voltage target (V) - Runtime adjustable
volatile float    target_Vd            = 0.0f;  // Flux voltage target (V) - Runtime adjustable
volatile float    current_limit_amps   = 1.0f;  // Max continuous phase current limit (A) - Runtime adjustable
volatile float    emergency_trip_amps  = 1.5f;  // Emergency shutdown trip threshold (A) - Runtime adjustable

volatile float    measured_Iq          = 0.0f;  // Measured torque current (Amps)
volatile float    measured_Id          = 0.0f;  // Measured flux current (Amps)
volatile float    measured_I_mag       = 0.0f;  // Total current magnitude (Amps)
volatile float    electrical_angle     = 0.0f;  // Electrical angle [0, 2*PI)
volatile uint16_t as5600_zero_offset   = 0;     // Calibrated zero angle offset
volatile bool     overcurrent_tripped  = false; // Latched overcurrent trip flag
volatile bool     uart_stream_enabled  = false; // Periodic UART telemetry streaming flag (default: OFF for zero overhead)


// =========================================================================
// 4. DRV8323S SPI DRIVER
// =========================================================================
void drv_spi_init(void) {
    spi_bus_config_t buscfg = {
        .miso_io_num     = PIN_SPI_MISO,
        .mosi_io_num     = PIN_SPI_MOSI,
        .sclk_io_num     = PIN_SPI_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 32,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000,    // 1 MHz
        .mode           = 1,          // Mode 1 (CPOL=0, CPHA=1)
        .spics_io_num   = PIN_SPI_CS,
        .queue_size     = 1,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &drv_spi));
    ESP_LOGI(TAG, "DRV8323 SPI bus initialized on SPI2_HOST.");
}

uint16_t drv_read_reg(uint8_t addr) {
    // Read: MSB = 1, bits [14:11] = addr
    uint16_t tx = (1 << 15) | ((addr & 0x0F) << 11);
    spi_transaction_t t = {
        .flags   = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length  = 16,
        .tx_data = { (uint8_t)(tx >> 8), (uint8_t)(tx & 0xFF) }
    };
    spi_device_polling_transmit(drv_spi, &t);
    return ((uint16_t)t.rx_data[0] << 8) | t.rx_data[1];
}

void drv_write_reg(uint8_t addr, uint16_t val) {
    // Write: MSB = 0, bits [14:11] = addr, bits [10:0] = data
    uint16_t tx = ((addr & 0x0F) << 11) | (val & 0x7FF);
    spi_transaction_t t = {
        .flags   = SPI_TRANS_USE_TXDATA,
        .length  = 16,
        .tx_data = { (uint8_t)(tx >> 8), (uint8_t)(tx & 0xFF) }
    };
    spi_device_polling_transmit(drv_spi, &t);
}

bool drv_safety_configure(void) {
    ESP_LOGI(TAG, "Configuring DRV8323 hardware safety registers...");

    // Reg 0x03: Unlock registers + HS Slew Rate (120mA Source / 240mA Sink)
    // LOCK=011b (Unlock), IDRIVEP_HS=0100b (120mA), IDRIVEN_HS=0100b (240mA)
    uint16_t hs_val = (0x03 << 8) | (0x04 << 4) | (0x04);
    drv_write_reg(0x03, hs_val);

    // Reg 0x04: LS Gate Drive (Latched Fault, 2000ns TDRIVE, 120mA Source / 240mA Sink)
    // CBC=0 (Latched fault), TDRIVE=10b (2000ns), IDRIVEP_LS=0100b (120mA), IDRIVEN_LS=0100b (240mA)
    uint16_t ls_val = (0x00 << 10) | (0x02 << 8) | (0x04 << 4) | (0x04);
    drv_write_reg(0x04, ls_val);

    // Reg 0x05: OCP Control (200ns Dead-Time, Latched Shutdown Mode, 2us Deglitch, 0.06V lowest VDS threshold)
    // TRETRY=0 (4ms), DEAD_TIME=10b (200ns), OCP_MODE=00b (Latched fault), OCP_DEG=01b (2us), VDS_LVL=0000b (0.06V - maximum sensitivity)
    uint16_t ocp_val = (0x00 << 10) | (0x02 << 8) | (0x00 << 6) | (0x01 << 4) | (0x00);
    drv_write_reg(0x05, ocp_val);

    // Reg 0x06: CSA Control (Bidirectional VREF/2 Midpoint Bias, 10 V/V Gain)
    // CSA_FET=0 (SPx/SNx), VREF_DIV=0 (VREF/2 enabled), CSA_GAIN=01b (10 V/V), DIS_SEN=0
    uint16_t csa_val = (0x00 << 10) | (0x00 << 9) | (0x01 << 6);
    drv_write_reg(0x06, csa_val);

    // Reg 0x02: Driver Control (3x PWM Mode, Enable UVLO & Gate Fault monitoring, Report OTW)
    // DIS_CPUV=0, DIS_GDF=0, OTW_REP=1, PWM_MODE=001b (3x PWM), COAST=0, BRAKE=0, CLR_FLT=0
    uint16_t ctrl_val = (0x01 << 8) | (0x01 << 5);
    drv_write_reg(0x02, ctrl_val);

    // Read back and log all configuration registers
    ESP_LOGI(TAG, "--- DRV8323 Register Audit ---");
    for (uint8_t reg = 0x02; reg <= 0x06; reg++) {
        uint16_t r = drv_read_reg(reg) & 0x7FF;
        ESP_LOGI(TAG, "  Reg 0x%02X: Readback = 0x%04X", reg, r);
    }

    ESP_LOGI(TAG, "DRV8323 hardware safety configurations active (Dead Time: 200ns, OCP: Latched Shutdown, IDRIVE: 120mA/240mA).");
    return true;
}

// =========================================================================
// 5. AS5600 I2C DRIVER
// =========================================================================
void as5600_i2c_init(void) {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port                   = I2C_NUM_0,
        .sda_io_num                 = PIN_I2C_SDA,
        .scl_io_num                 = PIN_I2C_SCL,
        .clk_source                 = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt          = 7,
        .flags.enable_internal_pullup = true
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = 0x36,       // AS5600 standard I2C address
        .scl_speed_hz    = 400000      // 400 kHz Fast Mode
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &as5600_handle));
    ESP_LOGI(TAG, "AS5600 I2C master initialized at 400 kHz.");
}

int16_t read_as5600(void) {
    uint8_t reg = 0x0C; // Raw angle register
    uint8_t data[2] = {0};
    if (i2c_master_transmit_receive(as5600_handle, &reg, 1, data, 2, 50) == ESP_OK) {
        return (int16_t)(((data[0] & 0x0F) << 8) | data[1]);
    }
    return -1; // Communication failed
}

// =========================================================================
// 6. MCPWM 3-PHASE GENERATOR
// =========================================================================
void pwm_init(void) {
    mcpwm_timer_handle_t timer;
    mcpwm_timer_config_t t_cfg = {
        .group_id      = 0,
        .clk_src       = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = MCPWM_TIMER_RES_HZ,
        .period_ticks  = PWM_MAX_DUTY,
        .count_mode    = MCPWM_TIMER_COUNT_MODE_UP_DOWN // Center-aligned mode
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&t_cfg, &timer));

    const int pwm_pins[3] = { PIN_INHA, PIN_INHB, PIN_INHC };

    for (int i = 0; i < 3; i++) {
        mcpwm_oper_handle_t oper;
        mcpwm_operator_config_t o_cfg = { .group_id = 0 };
        ESP_ERROR_CHECK(mcpwm_new_operator(&o_cfg, &oper));
        ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

        mcpwm_comparator_config_t c_cfg = { .flags.update_cmp_on_tez = true };
        ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &c_cfg, &comparators[i]));
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparators[i], 0));

        mcpwm_gen_handle_t gen;
        mcpwm_generator_config_t g_cfg = { .gen_gpio_num = pwm_pins[i] };
        ESP_ERROR_CHECK(mcpwm_new_generator(oper, &g_cfg, &gen));

        // Center-aligned PWM actions
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(
            gen, MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparators[i], MCPWM_GEN_ACTION_LOW)));
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(
            gen, MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_DOWN, comparators[i], MCPWM_GEN_ACTION_HIGH)));
    }

    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
    ESP_LOGI(TAG, "MCPWM initialized: 25 kHz center-aligned (Resolution: %d MHz, Max Duty: %d ticks).",
             (int)(MCPWM_TIMER_RES_HZ / 1000000), (int)PWM_MAX_DUTY);
}

// =========================================================================
// 7. ADC CONTINUOUS CURRENT SENSE
// =========================================================================
void adc_init(void) {
    adc_continuous_handle_cfg_t h_cfg = {
        .max_store_buf_size = 1024,
        .conv_frame_size    = 256
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&h_cfg, &adc_handle));

    adc_continuous_config_t c_cfg = {
        .pattern_num    = 3,
        .sample_freq_hz = 20000,
        .conv_mode      = ADC_CONV_SINGLE_UNIT_1,
        .format         = ADC_DIGI_OUTPUT_FORMAT_TYPE2
    };

    adc_digi_pattern_config_t p_cfg[3] = {
        { .atten = ADC_ATTEN_DB_12, .channel = PIN_SOA_ADC_CH, .unit = ADC_UNIT_1, .bit_width = ADC_BITWIDTH_12 },
        { .atten = ADC_ATTEN_DB_12, .channel = PIN_SOB_ADC_CH, .unit = ADC_UNIT_1, .bit_width = ADC_BITWIDTH_12 },
        { .atten = ADC_ATTEN_DB_12, .channel = PIN_SOC_ADC_CH, .unit = ADC_UNIT_1, .bit_width = ADC_BITWIDTH_12 }
    };
    c_cfg.adc_pattern = p_cfg;

    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &c_cfg));
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
    ESP_LOGI(TAG, "ADC Continuous current sensing initialized on ADC1.");
}

// =========================================================================
// 8. FOC MATHEMATICAL TRANSFORMS & DUTY UPDATE
// =========================================================================
static inline void update_electrical_angle(uint16_t raw_as5600) {
    int32_t aligned_raw = (int32_t)raw_as5600 - as5600_zero_offset;
    if (aligned_raw < 0) {
        aligned_raw += 4096;
    }

    // Convert 12-bit sensor counts to mechanical angle [0, 2*PI)
    float mechanical_angle = ((float)aligned_raw / 4096.0f) * (2.0f * (float)M_PI);

    // Convert to electrical angle and wrap
    float theta_e = mechanical_angle * (float)POLE_PAIRS;
    theta_e = fmodf(theta_e, 2.0f * (float)M_PI);
    if (theta_e < 0.0f) {
        theta_e += 2.0f * (float)M_PI;
    }
    electrical_angle = theta_e;
}

static inline void foc_loop_tick(void) {
    // Check if system is in emergency tripped state
    if (overcurrent_tripped) {
        mcpwm_comparator_set_compare_value(comparators[0], (uint32_t)(0.5f * (float)PWM_MAX_DUTY));
        mcpwm_comparator_set_compare_value(comparators[1], (uint32_t)(0.5f * (float)PWM_MAX_DUTY));
        mcpwm_comparator_set_compare_value(comparators[2], (uint32_t)(0.5f * (float)PWM_MAX_DUTY));
        return;
    }

    // 1. Read Phase Currents from ADC DMA
    uint8_t result_buf[256];
    uint32_t ret_num = 0;
    float ia = 0.0f, ib = 0.0f, ic = 0.0f;
    int samples_a = 0, samples_b = 0, samples_c = 0;

    esp_err_t ret = adc_continuous_read(adc_handle, result_buf, sizeof(result_buf), &ret_num, 0);
    if (ret == ESP_OK && ret_num > 0) {
        for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
            adc_digi_output_data_t *p = (adc_digi_output_data_t*)&result_buf[i];
            uint32_t chan = p->type2.channel;
            uint32_t val  = p->type2.data;
            float v_sens = ((float)val / 4095.0f) * 3.3f;
            float current = (v_sens - CSA_BIAS_VOLTS) / (SHUNT_RESISTANCE_OHMS * CSA_GAIN_V_PER_V);

            if (chan == PIN_SOA_ADC_CH) { ia += current; samples_a++; }
            else if (chan == PIN_SOB_ADC_CH) { ib += current; samples_b++; }
            else if (chan == PIN_SOC_ADC_CH) { ic += current; samples_c++; }
        }
        if (samples_a > 0) ia /= (float)samples_a;
        if (samples_b > 0) ib /= (float)samples_b;
        if (samples_c > 0) ic /= (float)samples_c;
    }

    // 2. Clarke & Park Transforms for measured current
    float s = sinf(electrical_angle);
    float c = cosf(electrical_angle);

    float i_alpha = ia;
    float i_beta  = 0.577350269f * (ia + 2.0f * ib); // 1/sqrt(3) = 0.577350269

    measured_Id = i_alpha * c + i_beta * s;
    measured_Iq = -i_alpha * s + i_beta * c;
    measured_I_mag = sqrtf(measured_Id * measured_Id + measured_Iq * measured_Iq);

    // 3. HARDWARE PROTECTION: Instant Emergency Current Trip
    if (measured_I_mag > emergency_trip_amps) {
        overcurrent_tripped = true;
        gpio_set_level(PIN_DRV_EN, 0); // Immediately pull DRV8323 ENABLE LOW (kill gates)
        mcpwm_comparator_set_compare_value(comparators[0], 0);
        mcpwm_comparator_set_compare_value(comparators[1], 0);
        mcpwm_comparator_set_compare_value(comparators[2], 0);
        return;
    }

    // 4. SOFTWARE CURRENT LIMITING: Dynamic Voltage Clamping
    float vq_cmd = target_Vq;
    float vd_cmd = target_Vd;

    if (measured_I_mag > current_limit_amps && measured_I_mag > 0.001f) {
        float scale = current_limit_amps / measured_I_mag;
        vq_cmd *= scale;
        vd_cmd *= scale;
    }

    // 5. Inverse Park Transform (d-q -> alpha-beta)
    float v_alpha = vd_cmd * c - vq_cmd * s;
    float v_beta  = vd_cmd * s + vq_cmd * c;

    // 6. Inverse Clarke Transform (alpha-beta -> a-b-c)
    float v_a = v_alpha;
    float v_b = -0.5f * v_alpha + (0.8660254f * v_beta); // sqrt(3)/2 = 0.8660254
    float v_c = -0.5f * v_alpha - (0.8660254f * v_beta);

    // 7. Normalize and center duty cycle at 50%
    float duty_a = (v_a / SUPPLY_VOLTAGE) + 0.5f;
    float duty_b = (v_b / SUPPLY_VOLTAGE) + 0.5f;
    float duty_c = (v_c / SUPPLY_VOLTAGE) + 0.5f;

    // 8. Clamp outputs within [0.0, 1.0]
    if (duty_a < 0.0f) duty_a = 0.0f; else if (duty_a > 1.0f) duty_a = 1.0f;
    if (duty_b < 0.0f) duty_b = 0.0f; else if (duty_b > 1.0f) duty_b = 1.0f;
    if (duty_c < 0.0f) duty_c = 0.0f; else if (duty_c > 1.0f) duty_c = 1.0f;

    // 9. Update MCPWM compare registers
    mcpwm_comparator_set_compare_value(comparators[0], (uint32_t)(duty_a * (float)PWM_MAX_DUTY));
    mcpwm_comparator_set_compare_value(comparators[1], (uint32_t)(duty_b * (float)PWM_MAX_DUTY));
    mcpwm_comparator_set_compare_value(comparators[2], (uint32_t)(duty_c * (float)PWM_MAX_DUTY));
}

// =========================================================================
// 9. 5 kHz (200 µs) TIMER CALLBACK
// =========================================================================
static void IRAM_ATTR foc_timer_callback(void* arg) {
    int16_t raw_angle = read_as5600();
    if (raw_angle >= 0) {
        update_electrical_angle((uint16_t)raw_angle);
        foc_loop_tick();
    }
}

// =========================================================================
// 10. INTERACTIVE UART RUNTIME CLI TASK
// =========================================================================
static void cli_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(1000)); // Allow USB CDC enumeration
    printf("\r\n");
    printf("===============================================================\r\n");
    printf("        ESP32-S3 FOC MOTOR CONTROLLER RUNTIME CLI             \r\n");
    printf("===============================================================\r\n");
    printf(" Commands:\r\n");
    printf("   vq <volts>    - Set torque voltage target (e.g. 'vq 1.5')\r\n");
    printf("   vd <volts>    - Set flux voltage target   (e.g. 'vd 0.0')\r\n");
    printf("   limit <amps>  - Set max current limit     (e.g. 'limit 2.0')\r\n");
    printf("   trip <amps>   - Set emergency trip cutoff (e.g. 'trip 3.0')\r\n");
    printf("   stop          - Stop motor (set Vq=0, Vd=0)\r\n");
    printf("   reset         - Clear overcurrent trip and re-enable driver\r\n");
    printf("   stream on/off - Toggle live serial telemetry stream (default: OFF)\r\n");
    printf("   status / ?    - Display live telemetry snapshot on-demand\r\n");
    printf("===============================================================\r\n\r\n");
    printf("FOC> ");
    fflush(stdout);

    char line[128];
    int pos = 0;

    while (1) {
        int ch = getchar();
        if (ch != EOF && ch >= 0) {
            if (ch == '\r' || ch == '\n') {
                if (pos > 0) {
                    line[pos] = '\0';
                    printf("\r\n");

                    char *cmd = line;
                    while (isspace((unsigned char)*cmd)) cmd++;

                    if (strncmp(cmd, "vq", 2) == 0) {
                        float val = (float)atof(cmd + 2);
                        target_Vq = val;
                        printf("[OK] target_Vq set to %4.2f V\r\n", target_Vq);
                    } else if (strncmp(cmd, "vd", 2) == 0) {
                        float val = (float)atof(cmd + 2);
                        target_Vd = val;
                        printf("[OK] target_Vd set to %4.2f V\r\n", target_Vd);
                    } else if (strncmp(cmd, "limit", 5) == 0) {
                        float val = (float)atof(cmd + 5);
                        if (val > 0.1f) {
                            current_limit_amps = val;
                            printf("[OK] Current limit set to %4.2f A\r\n", current_limit_amps);
                        } else {
                            printf("[ERR] Limit must be > 0.1 A\r\n");
                        }
                    } else if (strncmp(cmd, "trip", 4) == 0) {
                        float val = (float)atof(cmd + 4);
                        if (val > current_limit_amps) {
                            emergency_trip_amps = val;
                            printf("[OK] Emergency trip set to %4.2f A\r\n", emergency_trip_amps);
                        } else {
                            printf("[ERR] Trip must be greater than current limit (%4.2f A)\r\n", current_limit_amps);
                        }
                    } else if (strncmp(cmd, "stream", 6) == 0) {
                        if (strstr(cmd, "on")) {
                            uart_stream_enabled = true;
                            printf("[OK] Serial telemetry streaming enabled (1 Hz).\r\n");
                        } else if (strstr(cmd, "off")) {
                            uart_stream_enabled = false;
                            printf("[OK] Serial telemetry streaming disabled (0%% overhead).\r\n");
                        } else {
                            printf("Stream is currently: %s. Use 'stream on' or 'stream off'.\r\n", uart_stream_enabled ? "ON" : "OFF");
                        }
                    } else if (strcmp(cmd, "stop") == 0 || strcmp(cmd, "s") == 0) {
                        target_Vq = 0.0f;
                        target_Vd = 0.0f;
                        printf("[OK] Motor stopped (Vq=0.0V, Vd=0.0V)\r\n");
                    } else if (strcmp(cmd, "reset") == 0 || strcmp(cmd, "r") == 0) {
                        overcurrent_tripped = false;
                        gpio_set_level(PIN_DRV_EN, 1);
                        gpio_set_level(PIN_INLA, 1);
                        gpio_set_level(PIN_INLB, 1);
                        gpio_set_level(PIN_INLC, 1);
                        printf("[OK] Trip cleared. Driver re-enabled.\r\n");
                    } else if (strcmp(cmd, "status") == 0 || strcmp(cmd, "ip") == 0 || strcmp(cmd, "?") == 0 || strcmp(cmd, "help") == 0) {
                        int fault = gpio_get_level(PIN_DRV_FAULT);
                        esp_netif_ip_info_t ip_info;
                        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                        printf("--- System Status ---\r\n");
                        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
                            printf("  Web Dashboard: http://" IPSTR "\r\n", IP2STR(&ip_info.ip));
                        } else {
                            printf("  Web Dashboard: http://192.168.4.1 (SoftAP)\r\n");
                        }
                        printf("  Vq Target:     %4.2f V\r\n", target_Vq);
                        printf("  Vd Target:     %4.2f V\r\n", target_Vd);
                        printf("  Angle:         %6.2f deg\r\n", electrical_angle * (180.0f / (float)M_PI));
                        printf("  Current Limit: %4.2f A\r\n", current_limit_amps);
                        printf("  Trip Current:  %4.2f A\r\n", emergency_trip_amps);
                        printf("  Measured I:    %4.2f A (Iq=%4.2f A, Id=%4.2f A)\r\n", measured_I_mag, measured_Iq, measured_Id);
                        printf("  State:         %s\r\n", overcurrent_tripped ? "TRIPPED (Type 'reset' to clear)" : (fault ? "RUNNING_OK" : "HW_FAULT"));
                        printf("  Serial Stream: %s\r\n", uart_stream_enabled ? "ON" : "OFF");
                    } else {
                        printf("[ERR] Unknown command: '%s'. Type 'help' for commands.\r\n", cmd);
                    }
                    pos = 0;
                    printf("FOC> ");
                    fflush(stdout);
                }
            } else if (ch == '\b' || ch == 127) {
                if (pos > 0) {
                    pos--;
                    printf("\b \b");
                    fflush(stdout);
                }
            } else if (pos < (int)(sizeof(line) - 1) && ch >= 32 && ch <= 126) {
                line[pos++] = (char)ch;
                putchar(ch);
                fflush(stdout);
            }
        }
    }
}

// =========================================================================
// 11. WEB SERVER & HTTP REST API HANDLERS
// =========================================================================
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    size_t total_len = strlen(index_html);
    size_t sent = 0;
    while (sent < total_len) {
        size_t chunk_len = total_len - sent;
        if (chunk_len > 2048) {
            chunk_len = 2048;
        }
        esp_err_t err = httpd_resp_send_chunk(req, index_html + sent, chunk_len);
        if (err != ESP_OK) {
            return err;
        }
        sent += chunk_len;
    }
    return httpd_resp_send_chunk(req, NULL, 0); // End of chunked stream
}

static esp_err_t status_get_handler(httpd_req_t *req) {
    char resp_str[256];
    int fault = gpio_get_level(PIN_DRV_FAULT);
    snprintf(resp_str, sizeof(resp_str),
             "{\"vq\":%.2f,\"vd\":%.2f,\"current\":%.2f,\"iq\":%.2f,\"id\":%.2f,\"limit\":%.2f,\"trip\":%.2f,\"angle_deg\":%.1f,\"tripped\":%s,\"hw_ok\":%s}",
             target_Vq, target_Vd, measured_I_mag, measured_Iq, measured_Id,
             current_limit_amps, emergency_trip_amps,
             electrical_angle * (180.0f / (float)M_PI),
             overcurrent_tripped ? "true" : "false",
             fault ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t set_post_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return httpd_resp_send_500(req);
    }
    buf[ret] = '\0';

    char *p = strstr(buf, "\"vq\":");
    if (p) target_Vq = (float)atof(p + 5);

    p = strstr(buf, "\"vd\":");
    if (p) target_Vd = (float)atof(p + 5);

    p = strstr(buf, "\"limit\":");
    if (p) {
        float l = (float)atof(p + 8);
        if (l > 0.1f) current_limit_amps = l;
    }

    p = strstr(buf, "\"trip\":");
    if (p) {
        float t = (float)atof(p + 7);
        if (t > current_limit_amps) emergency_trip_amps = t;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t stop_post_handler(httpd_req_t *req) {
    target_Vq = 0.0f;
    target_Vd = 0.0f;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"status\":\"stopped\"}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t reset_post_handler(httpd_req_t *req) {
    overcurrent_tripped = false;
    gpio_set_level(PIN_DRV_EN, 1);
    gpio_set_level(PIN_INLA, 1);
    gpio_set_level(PIN_INLB, 1);
    gpio_set_level(PIN_INLC, 1);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"status\":\"reset_ok\"}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t drv_regs_get_handler(httpd_req_t *req) {
    uint16_t r0 = drv_read_reg(0x00) & 0x7FF;
    uint16_t r1 = drv_read_reg(0x01) & 0x7FF;
    uint16_t r2 = drv_read_reg(0x02) & 0x7FF;
    uint16_t r3 = drv_read_reg(0x03) & 0x7FF;
    uint16_t r4 = drv_read_reg(0x04) & 0x7FF;
    uint16_t r5 = drv_read_reg(0x05) & 0x7FF;
    uint16_t r6 = drv_read_reg(0x06) & 0x7FF;

    char resp_str[256];
    snprintf(resp_str, sizeof(resp_str),
             "{\"r0\":%u,\"r1\":%u,\"r2\":%u,\"r3\":%u,\"r4\":%u,\"r5\":%u,\"r6\":%u}",
             r0, r1, r2, r3, r4, r5, r6);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t drv_write_post_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return httpd_resp_send_500(req);
    buf[ret] = '\0';

    char *pa = strstr(buf, "\"addr\":");
    char *pv = strstr(buf, "\"val\":");
    if (pa && pv) {
        uint8_t addr = (uint8_t)atoi(pa + 7);
        uint16_t val = (uint16_t)atoi(pv + 6);
        if (addr >= 0x02 && addr <= 0x06) {
            drv_write_reg(addr, val);
            ESP_LOGI(TAG, "DRV8323 Reg 0x%02X written with 0x%04X via Web Dashboard", addr, val);
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
}

static httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.max_open_sockets = 7;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    httpd_handle_t server = NULL;


    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_uri_t status_uri = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler };
        httpd_uri_t set_uri = { .uri = "/api/set", .method = HTTP_POST, .handler = set_post_handler };
        httpd_uri_t stop_uri = { .uri = "/api/stop", .method = HTTP_POST, .handler = stop_post_handler };
        httpd_uri_t reset_uri = { .uri = "/api/reset", .method = HTTP_POST, .handler = reset_post_handler };
        httpd_uri_t drv_regs_uri = { .uri = "/api/drv_regs", .method = HTTP_GET, .handler = drv_regs_get_handler };
        httpd_uri_t drv_write_uri = { .uri = "/api/drv_write", .method = HTTP_POST, .handler = drv_write_post_handler };

        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &status_uri);
        httpd_register_uri_handler(server, &set_uri);
        httpd_register_uri_handler(server, &stop_uri);
        httpd_register_uri_handler(server, &reset_uri);
        httpd_register_uri_handler(server, &drv_regs_uri);
        httpd_register_uri_handler(server, &drv_write_uri);
        ESP_LOGI(TAG, "HTTP Web Server started successfully with DRV8323 Register API.");
        return server;
    }
    ESP_LOGE(TAG, "Failed to start HTTP Web Server.");
    return NULL;
}

// =========================================================================
// 12. WIFI DUAL-MODE (STATION & SOFTAP FALLBACK) INITIALIZATION
// =========================================================================
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected. Reconnecting in background...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "==========================================================");
        ESP_LOGI(TAG, ">>> CONNECTED TO LOCAL WI-FI NETWORK! <<<");
        ESP_LOGI(TAG, ">>> Web Dashboard: http://" IPSTR " <<<", IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "==========================================================");
        printf("\r\n==========================================================\r\n");
        printf(">>> CONNECTED TO WI-FI: http://" IPSTR " <<<\r\n", IP2STR(&event->ip_info.ip));
        printf("==========================================================\r\n\r\n");
        fflush(stdout);
    }
}

static void wifi_init_network(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t sta_config = {
        .sta = {
            .ssid = STA_WIFI_SSID,
            .password = STA_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .capable = true, .required = false },
        },
    };

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_WIFI_SSID,
            .ssid_len = strlen(AP_WIFI_SSID),
            .channel = 1,
            .password = AP_WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .required = false },
        },
    };

    bool use_sta = (strlen(STA_WIFI_SSID) > 0 && strcmp(STA_WIFI_SSID, "YOUR_WIFI_SSID") != 0);

    if (use_sta) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ESP_LOGI(TAG, "Connecting to local Wi-Fi: '%s' (SoftAP '%s' also active as fallback)",
                 STA_WIFI_SSID, AP_WIFI_SSID);
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ESP_LOGI(TAG, "Local Wi-Fi credentials not configured. SoftAP active at http://192.168.4.1 (SSID: '%s', Pass: '%s')",
                 AP_WIFI_SSID, AP_WIFI_PASS);
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); // Disable Wi-Fi power saving for instant HTTP response
}

// =========================================================================
// 13. MAIN ENTRY POINT
// =========================================================================
void app_main(void) {
    ESP_LOGI(TAG, "Initializing FOC Motor Controller on ESP32-S3-WROOM-1...");

    // 1. Initialize NVS Flash (required for Wi-Fi storage)
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // 2. Configure DRV8323 Enable & 3x PWM Low-Side pins (held HIGH in 3x mode)
    const gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << PIN_DRV_EN) | (1ULL << PIN_INLA) | (1ULL << PIN_INLB) | (1ULL << PIN_INLC),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_conf));

    // Fault pin configuration (Input with internal pullup)
    const gpio_config_t in_conf = {
        .pin_bit_mask = (1ULL << PIN_DRV_FAULT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_conf));

    gpio_set_level(PIN_DRV_EN, 1); // Enable DRV8323
    gpio_set_level(PIN_INLA, 1);   // Enable Phase A half-bridge
    gpio_set_level(PIN_INLB, 1);   // Enable Phase B half-bridge
    gpio_set_level(PIN_INLC, 1);   // Enable Phase C half-bridge
    vTaskDelay(pdMS_TO_TICKS(15)); // DRV8323 power-on wake delay

    // 3. Initialize Hardware Peripherals
    drv_spi_init();
    as5600_i2c_init();
    pwm_init();
    adc_init();

    // 4. Configure DRV8323 Hardware Safety Registers (Dead Time, OCP, Slew Rate, CSA, 3x Mode)
    drv_safety_configure();

    // 5. Start 5 kHz High-Resolution Periodic Timer (200 microseconds)
    target_Vq = 0.0f;
    target_Vd = 0.0f;

    const esp_timer_create_args_t foc_timer_args = {
        .callback              = &foc_timer_callback,
        .name                  = "foc_5khz_loop",
        .skip_unhandled_events = true
    };
    esp_timer_handle_t foc_timer;
    ESP_ERROR_CHECK(esp_timer_create(&foc_timer_args, &foc_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(foc_timer, 200));

    // 6. Initialize Wi-Fi Network & Web Dashboard Server
    wifi_init_network();
    start_webserver();

    // 7. Launch Interactive UART CLI Task on Core 0
    xTaskCreatePinnedToCore(cli_task, "cli_task", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "5 kHz FOC Loop running. CLI active.");

    // 8. Background Idle Loop (Zero overhead when stream is disabled)
    while (1) {
        if (uart_stream_enabled) {
            int fault = gpio_get_level(PIN_DRV_FAULT);
            ESP_LOGI(TAG, "Status | Angle: %6.2f deg | Vq: %4.2f V | Current: %4.2f A | Limit: %3.1f A | State: %s",
                     electrical_angle * (180.0f / (float)M_PI), target_Vq, measured_I_mag,
                     current_limit_amps,
                     overcurrent_tripped ? "TRIPPED" : (fault ? "OK" : "HW_FAULT"));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
