#include <application.h>

// Service mode interval defines how much time
#define SERVICE_MODE_INTERVAL (15 * 60 * 1000)
#define BATTERY_UPDATE_INTERVAL (60 * 60 * 1000)
#define TEMPERATURE_PUB_INTERVAL (15 * 60 * 1000)
#define TEMPERATURE_PUB_DIFFERENCE 0.2f
#define TEMPERATURE_UPDATE_SERVICE_INTERVAL (1 * 1000)
#define TEMPERATURE_UPDATE_NORMAL_INTERVAL (10 * 1000)
#define ACCELEROMETER_UPDATE_SERVICE_INTERVAL (1 * 1000)
#define ACCELEROMETER_UPDATE_NORMAL_INTERVAL (10 * 1000)

// LED instance
twr_led_t led;

// Button instance
twr_button_t button;

// Thermometer instance
twr_tmp112_t tmp112;

// Accelerometer instance
twr_lis2dh12_t lis2dh12;

// Dice instance
twr_dice_t dice;

// Counters for button events
uint16_t button_click_count = 0;
uint16_t button_hold_count = 0;

// Time of button has press
twr_tick_t tick_start_button_press;
// Flag for button hold event
bool button_hold_event;

// Time of next temperature report
twr_tick_t tick_temperature_report = 0;

// This function dispatches button events
void button_event_handler(twr_button_t *self, twr_button_event_t event, void *event_param)
{
    if (event == TWR_BUTTON_EVENT_CLICK)
    {
        // Pulse LED for 100 milliseconds
        twr_led_pulse(&led, 100);

        // Increment press count
        button_click_count++;

        // Publish button message on radio
        char buffer[32];
        sprintf(buffer, "Button: %d", button_click_count);
        twr_uart_write(TWR_UART_UART2, buffer, strlen(buffer));
    }
    else if (event == TWR_BUTTON_EVENT_HOLD)
    {
        // Pulse LED for 250 milliseconds
        twr_led_pulse(&led, 250);

        // Increment hold count
        button_hold_count++;

        // Publish message on radio
        char buffer[32];
        sprintf(buffer, "Button_hold: %d", button_hold_count);
        twr_uart_write(TWR_UART_UART2, buffer, strlen(buffer));

        // Set button hold event flag
        button_hold_event = true;
    }
    else if (event == TWR_BUTTON_EVENT_PRESS)
    {
        // Reset button hold event flag
        button_hold_event = false;

        tick_start_button_press = twr_tick_get();
    }
    else if (event == TWR_BUTTON_EVENT_RELEASE)
    {
        if (button_hold_event)
        {
            int hold_duration = twr_tick_get() - tick_start_button_press;

            char buffer[32];
            sprintf(buffer, "Button_hold_duration: %d\r\n", hold_duration);
            twr_uart_write(TWR_UART_UART2, buffer, strlen(buffer));
        }
    }
}

// This function dispatches battery events
void battery_event_handler(twr_module_battery_event_t event, void *event_param)
{
    // Update event?
    if (event == TWR_MODULE_BATTERY_EVENT_UPDATE)
    {
        float voltage;

        // Read battery voltage
        if (twr_module_battery_get_voltage(&voltage))
        {
            char buffer[32];
            sprintf(buffer, "Battery: %.2f\r\n", voltage);
            twr_uart_write(TWR_UART_UART2, buffer, strlen(buffer));
        }
    }
}

// This function dispatches thermometer events
void tmp112_event_handler(twr_tmp112_t *self, twr_tmp112_event_t event, void *event_param)
{
    // Update event?
    if (event == TWR_TMP112_EVENT_UPDATE)
    {
        float temperature;

        // Successfully read temperature?
        if (twr_tmp112_get_temperature_celsius(self, &temperature))
        {
            // Implicitly do not publish message on radio
            bool publish = false;

            // Is time up to report temperature?
            if (twr_tick_get() >= tick_temperature_report)
            {
                // Publish message on radio
                publish = true;
            }

            // Last temperature value used for change comparison
            static float last_published_temperature = NAN;

            // Temperature ever published?
            if (last_published_temperature != NAN)
            {
                // Is temperature difference from last published value significant?
                if (fabsf(temperature - last_published_temperature) >= TEMPERATURE_PUB_DIFFERENCE)
                {
                    // Publish message on radio
                    publish = true;
                }
            }

            // Publish message on radio?
            if (publish)
            {
                char buffer[32];
                sprintf(buffer, "Temperature: %.2f\r\n", temperature);
                twr_uart_write(TWR_UART_UART2, buffer, strlen(buffer));

                // Schedule next temperature report
                tick_temperature_report = twr_tick_get() + TEMPERATURE_PUB_INTERVAL;

                // Remember last published value
                last_published_temperature = temperature;
            }
        }
    }
    // Error event?
    else if (event == TWR_TMP112_EVENT_ERROR)
    {
    }
}

// This function dispatches accelerometer events
void lis2dh12_event_handler(twr_lis2dh12_t *self, twr_lis2dh12_event_t event, void *event_param)
{
    // Update event?
    if (event == TWR_LIS2DH12_EVENT_UPDATE)
    {
        twr_lis2dh12_result_g_t result;

        // Successfully read accelerometer vectors?
        if (twr_lis2dh12_get_result_g(self, &result))
        {
            // Update dice with new vectors
            twr_dice_feed_vectors(&dice, result.x_axis, result.y_axis, result.z_axis);

            // This variable holds last dice face
            static twr_dice_face_t last_face = TWR_DICE_FACE_UNKNOWN;

            // Get current dice face
            twr_dice_face_t face = twr_dice_get_face(&dice);

            // Did dice face change from last time?
            if (last_face != face)
            {
                // Remember last dice face
                last_face = face;

                // Convert dice face to integer
                int orientation = face;

                char buffer[32];
                sprintf(buffer, "Orientation: %d\r\n", orientation);
                twr_uart_write(TWR_UART_UART2, buffer, strlen(buffer));
            }
        }
    }
    // Error event?
    else if (event == TWR_LIS2DH12_EVENT_ERROR)
    {
    }
}

// This function is run as task and exits service mode
void exit_service_mode_task(void *param)
{
    // Set thermometer update interval to normal
    twr_tmp112_set_update_interval(&tmp112, TEMPERATURE_UPDATE_NORMAL_INTERVAL);

    // Set accelerometer update interval to normal
    twr_lis2dh12_set_update_interval(&lis2dh12, ACCELEROMETER_UPDATE_NORMAL_INTERVAL);

    // Unregister current task (it has only one-shot purpose)
    twr_scheduler_unregister(twr_scheduler_get_current_task_id());
}

void application_init(void)
{
    // Initialize LED
    twr_led_init(&led, TWR_GPIO_LED, false, false);
    twr_led_set_mode(&led, TWR_LED_MODE_OFF);

    // Initialize button
    twr_button_init(&button, TWR_GPIO_BUTTON, TWR_GPIO_PULL_DOWN, false);
    twr_button_set_event_handler(&button, button_event_handler, NULL);

    // Initialize battery
    twr_module_battery_init();
    twr_module_battery_set_event_handler(battery_event_handler, NULL);
    twr_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // Initialize thermometer
    twr_tmp112_init(&tmp112, TWR_I2C_I2C0, 0x49);
    twr_tmp112_set_event_handler(&tmp112, tmp112_event_handler, NULL);
    twr_tmp112_set_update_interval(&tmp112, TEMPERATURE_UPDATE_SERVICE_INTERVAL);

    // Initialize accelerometer
    twr_lis2dh12_init(&lis2dh12, TWR_I2C_I2C0, 0x19);
    twr_lis2dh12_set_event_handler(&lis2dh12, lis2dh12_event_handler, NULL);
    twr_lis2dh12_set_update_interval(&lis2dh12, ACCELEROMETER_UPDATE_SERVICE_INTERVAL);

    // Initialize dice
    twr_dice_init(&dice, TWR_DICE_FACE_UNKNOWN);

    twr_scheduler_register(exit_service_mode_task, NULL, SERVICE_MODE_INTERVAL);

    twr_uart_init(TWR_UART_UART2, TWR_UART_BAUDRATE_115200, TWR_UART_SETTING_8N1);

    // Pulse LED
    twr_led_pulse(&led, 2000);
}
