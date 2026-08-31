#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/mcpwm_prelude.h"
#include "esp_adc/adc_continuous.h"

#define TAG "FOC_DRIVER"

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
#define POLE_PAIRS          7          // Number of magnetic pole pairs
#define SUPPLY_VOLTAGE      24.0f      // DC Bus Voltage (Volts)

// Center-Aligned MCPWM Configuration (25 kHz switching frequency)
#define MCPWM_TIMER_RES_HZ  40000000   // 40 MHz MCPWM clock resolution
#define PWM_FREQ_HZ         25000      // 25 kHz PWM frequency
// Up-Down count mode: f_pwm = f_timer / (2 * period_ticks) => 40MHz / (2 * 25kHz) = 800 ticks
#define PWM_MAX_DUTY        (MCPWM_TIMER_RES_HZ / (2 * PWM_FREQ_HZ)) // 800 ticks

// =========================================================================
// 3. PERIPHERAL HANDLES & FOC STATE
// =========================================================================
static spi_device_handle_t      drv_spi;
static i2c_master_dev_handle_t  as5600_handle;
static mcpwm_cmpr_handle_t      comparators[3];
static adc_continuous_handle_t  adc_handle;

// FOC Targets and State
volatile float    target_Vq            = 0.0f;  // Torque voltage target (V)
volatile float    target_Vd            = 0.0f;  // Flux voltage target (V)
volatile float    electrical_angle     = 0.0f;  // Electrical angle [0, 2*PI)
volatile uint16_t as5600_zero_offset   = 0;     // Calibrated zero angle offset

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
        .format         = ADC_DIGI_OUTPUT_FORMAT_TYPE1
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
    float s = sinf(electrical_angle);
    float c = cosf(electrical_angle);

    // 1. Inverse Park Transform (d-q -> alpha-beta)
    float v_alpha = target_Vd * c - target_Vq * s;
    float v_beta  = target_Vd * s + target_Vq * c;

    // 2. Inverse Clarke Transform (alpha-beta -> a-b-c)
    float v_a = v_alpha;
    float v_b = -0.5f * v_alpha + (0.8660254f * v_beta); // sqrt(3)/2 = 0.8660254
    float v_c = -0.5f * v_alpha - (0.8660254f * v_beta);

    // 3. Normalize and center duty cycle at 50%
    float duty_a = (v_a / SUPPLY_VOLTAGE) + 0.5f;
    float duty_b = (v_b / SUPPLY_VOLTAGE) + 0.5f;
    float duty_c = (v_c / SUPPLY_VOLTAGE) + 0.5f;

    // 4. Clamp outputs within [0.0, 1.0]
    if (duty_a < 0.0f) duty_a = 0.0f; else if (duty_a > 1.0f) duty_a = 1.0f;
    if (duty_b < 0.0f) duty_b = 0.0f; else if (duty_b > 1.0f) duty_b = 1.0f;
    if (duty_c < 0.0f) duty_c = 0.0f; else if (duty_c > 1.0f) duty_c = 1.0f;

    // 5. Update MCPWM compare registers
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
// 10. MAIN ENTRY POINT
// =========================================================================
void app_main(void) {
    ESP_LOGI(TAG, "Initializing FOC Motor Controller on ESP32-S3-WROOM-1...");

    // 1. Configure DRV8323 Enable & 3x PWM Low-Side pins (held HIGH in 3x mode)
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

    // 2. Initialize Hardware Peripherals
    drv_spi_init();
    as5600_i2c_init();
    pwm_init();
    adc_init();

    // 3. Configure DRV8323 Control Register 2 (0x02) for 3x PWM Mode
    // Bit 6:5 = 01b (3x PWM Mode), Bit 4 = 0 (1x PWM Mode disabled)
    uint16_t ctrl2 = drv_read_reg(0x02);
    ctrl2 = (ctrl2 & ~(0x03 << 5)) | (0x01 << 5);
    drv_write_reg(0x02, ctrl2);
    ESP_LOGI(TAG, "DRV8323 Control Register 2 set to 3x PWM Mode (0x%04X).", ctrl2);

    // 4. Start 5 kHz High-Resolution Periodic Timer (200 microseconds)
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

    ESP_LOGI(TAG, "5 kHz FOC Control Loop running.");

    // 5. Idle / Status Logging Loop
    while (1) {
        int fault = gpio_get_level(PIN_DRV_FAULT);
        ESP_LOGI(TAG, "Status | Angle: %6.2f deg | Vq: %4.2f V | Vd: %4.2f V | Fault: %s",
                 electrical_angle * (180.0f / (float)M_PI), target_Vq, target_Vd,
                 fault ? "OK" : "FAULT");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
