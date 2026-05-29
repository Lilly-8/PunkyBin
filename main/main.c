#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include "mqtt_client.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#define SERVO_PIN        27

#define TRIG_OUT         26
#define ECHO_OUT         25

#define TRIG_IN          33
#define ECHO_IN          32

#define LEDC_TIMER       LEDC_TIMER_0
#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL     LEDC_CHANNEL_0
#define LEDC_DUTY_RES    LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY   50 

#define SERVO_MIN_DUTY   205
#define SERVO_MAX_DUTY   1024
#define SERVO_CLOSED_ANGLE  90
#define SERVO_OPEN_ANGLE     15

#define AIO_SERVER      "io.adafruit.com"
#define AIO_PORT        8883
#define AIO_USER        "USERNAME_AIO"
#define AIO_KEY         "KEY_AQUI"
#define FEED_LLENADO    "NOMBRE_USUARIO/feeds/llenado"
#define FEED_APERTURAS  "NOMBRE_USUARIO/feeds/aperturas"
#define FEED_ESTADO     "NOMBRE_USUARIO/feeds/estado"
#define WIFI_SSID      "WIFI_SSID"
#define WIFI_PASS      "WIFI_PASS"

esp_mqtt_client_handle_t client;
static bool mqtt_connected = false;
static bool wifi_got_ip = false;
static bool whatsapp_alert_sent = false;

#define CALLMEBOT_URL_BASE  "http://api.callmebot.com/whatsapp.php"
#define CALLMEBOT_PHONE     "5216462591034"
#define CALLMEBOT_APIKEY    "7879807"


// Maneja eventos MQTT (conexión/desconexión/errores)
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    if (event_id == MQTT_EVENT_CONNECTED) {
        mqtt_connected = true;
        printf("MQTT conectado a Adafruit IO.\n");
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        mqtt_connected = false;
        printf("MQTT desconectado. Reintentando...\n");
    } else if (event_id == MQTT_EVENT_ERROR) {
        mqtt_connected = false;
        printf("Error en MQTT.\n");
    }
}

// Manejador de eventos Wi‑Fi: conecta automáticamente y registra cuando obtiene IP
static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        wifi_got_ip = true;
        printf("¡Conectado! IP obtenida.\n");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_got_ip = false;
        printf("Fallo de conexión. Reintentando...\n");
        esp_wifi_connect();
    }
}

// Inicialización de Wi‑Fi y pila de red
// Se registra el manejador de eventos para conectar y obtener IP automáticamente
void wifi_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
// --- AGREGAR ESTO: Registrar los manejadores de eventos ---
    // Esto vincula la función wifi_event_handler con los eventos reales del Wi-Fi
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
    // ---------------------------------------------------------

    // Configuración de credenciales Wi‑Fi (STA)
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.rssi = -127,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Al ejecutar start, se disparará el evento WIFI_EVENT_STA_START 
    // y tu handler llamará automáticamente a esp_wifi_connect()
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    printf("Iniciando Wi-Fi para: %s...\n", WIFI_SSID);

    printf("Esperando dirección IP...\n");
    for (int i = 0; i < 100 && !wifi_got_ip; ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!wifi_got_ip) {
        printf("Advertencia: Wi-Fi no obtuvo IP a tiempo.\n");
    }
}

// Inicia el cliente MQTT hacia Adafruit IO usando TLS
void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.hostname = AIO_SERVER;
    mqtt_cfg.broker.address.port = AIO_PORT;
    mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
    mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;

    mqtt_cfg.credentials.username = AIO_USER;
    mqtt_cfg.credentials.authentication.password = AIO_KEY;

    mqtt_cfg.session.keepalive = 60;
    mqtt_cfg.network.reconnect_timeout_ms = 5000;

    client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    esp_mqtt_client_start(client);
    printf("Conectando a Adafruit vía TLS: %s\n", AIO_SERVER);
}

// Función para enviar datos
void enviar_dato(const char* topic, int valor) {
    char data[10];
    sprintf(data, "%d", valor);
    if (!mqtt_connected) {
        printf("MQTT aún no está conectado. No se envió %s = %s\n", topic, data);
        return;
    }

    int msg_id = esp_mqtt_client_publish(client, topic, data, 0, 1, 0);
    printf("Publicado %s = %s (msg_id=%d)\n", topic, data, msg_id);
}

void enviar_estado(const char* estado) {
    if (!mqtt_connected) {
        printf("MQTT aún no está conectado. No se envió estado = %s\n", estado);
        return;
    }

    int msg_id = esp_mqtt_client_publish(client, FEED_ESTADO, estado, 0, 1, 0);
    printf("Publicado %s = %s (msg_id=%d)\n", FEED_ESTADO, estado, msg_id);
}

// Codifica texto para incluirlo en URL (ASCII básico)
static void url_encode(const char *src, char *dst, size_t dst_len) {
    const char *hex = "0123456789ABCDEF";
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 4 < dst_len; ++si) {
        unsigned char c = (unsigned char)src[si];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[di++] = c;
        } else if (c == ' ') {
            dst[di++] = '+'; // forma simple para espacios
        } else {
            if (di + 3 >= dst_len) break;
            dst[di++] = '%';
            dst[di++] = hex[(c >> 4) & 0xF];
            dst[di++] = hex[c & 0xF];
        }
    }
    dst[di] = '\0';
}

// Envía un mensaje por WhatsApp usando CallMeBot (petición HTTP GET).
// Retorna true si el servidor responde con 2xx.
static bool send_whatsapp_message(const char *text) {
    if (CALLMEBOT_PHONE[0] == '<' || CALLMEBOT_APIKEY[0] == '<') {
        printf("CallMeBot: teléfono/apikey no configurados. Mensaje no enviado.\n");
        return false;
    }

    char enc[512];
    url_encode(text, enc, sizeof(enc));

    char url[768];
    // Formato: http://api.callmebot.com/whatsapp.php?phone=PHONE&text=TEXT&apikey=APIKEY
    snprintf(url, sizeof(url), "%s?phone=%s&text=%s&apikey=%s", CALLMEBOT_URL_BASE, CALLMEBOT_PHONE, enc, CALLMEBOT_APIKEY);

    for (int intento = 1; intento <= 3; ++intento) {
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .timeout_ms = 7000,
        };

        esp_http_client_handle_t http = esp_http_client_init(&config);
        if (!http) {
            printf("CallMeBot: fallo init HTTP (intento %d)\n", intento);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        esp_err_t err = esp_http_client_perform(http);
        int status = esp_http_client_get_status_code(http);
        esp_http_client_cleanup(http);

        if (err == ESP_OK && (status == 200 || status == 201 || status == 204)) {
            printf("CallMeBot: Mensaje enviado (HTTP %d, intento %d)\n", status, intento);
            return true;
        }

        printf("CallMeBot: Error enviando mensaje (intento %d, err=%d, http_status=%d)\n", intento, err, status);
        if (intento < 3) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    return false;
}



void init_hw() {
    // Configuración PWM para el Servo
    // Configuración del temporizador PWM para el servo
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = SERVO_PIN,
        .duty           = 0,
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);

    gpio_set_direction(TRIG_OUT, GPIO_MODE_OUTPUT);
    gpio_set_direction(ECHO_OUT, GPIO_MODE_INPUT);

    gpio_set_direction(TRIG_IN, GPIO_MODE_OUTPUT);
    gpio_set_direction(ECHO_IN, GPIO_MODE_INPUT);
}

void set_servo_angle(int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    uint32_t duty = SERVO_MIN_DUTY + (angle * (SERVO_MAX_DUTY - SERVO_MIN_DUTY) / 180);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static void move_servo_smoothly(int target_angle) {
    static int current_angle = SERVO_CLOSED_ANGLE;

    if (target_angle < 0) target_angle = 0;
    if (target_angle > 180) target_angle = 180;

    int step = (target_angle > current_angle) ? 1 : -1;
    // Hacemos el movimiento más lento y suave: 30 ms por grado
    while (current_angle != target_angle) {
        current_angle += step;
        set_servo_angle(current_angle);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Lee un pulso del sensor ultrasónico y devuelve distancia en cm.
// Retorna -1.0 si hay timeout (eco no recibido), para detectar lecturas inválidas.
float get_distance(int trig_pin, int echo_pin) {
    gpio_set_level(trig_pin, 0);
    ets_delay_us(2);
    gpio_set_level(trig_pin, 1);
    ets_delay_us(10);
    gpio_set_level(trig_pin, 0);

    // Timeout para evitar que el código se bloquee si no hay eco
    uint32_t timeout = 20000; // ~20 ms
    while (gpio_get_level(echo_pin) == 0 && timeout > 0) {
        timeout--;
        ets_delay_us(1);
    }
    if (timeout == 0) return -1.0f;
    
    int64_t start = esp_timer_get_time();
    
    timeout = 20000;
    while (gpio_get_level(echo_pin) == 1 && timeout > 0) {
        timeout--;
        ets_delay_us(1);
    }
    if (timeout == 0) return -1.0f;
    int64_t end = esp_timer_get_time();

    return (float)(end - start) / 58.0;
}

// Toma varias lecturas y devuelve la mediana para mitigar picos y lecturas erráticas.
static float get_distance_filtered(int trig_pin, int echo_pin) {
    float samples[5];
    int valid_count = 0;

    for (int i = 0; i < 5; ++i) {
        float d = get_distance(trig_pin, echo_pin);
        if (d > 0.0f) samples[valid_count++] = d;
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    if (valid_count == 0) return -1.0f;

    // Ordena y devuelve la mediana
    for (int i = 0; i < valid_count - 1; ++i) {
        for (int j = i + 1; j < valid_count; ++j) {
            if (samples[j] < samples[i]) {
                float tmp = samples[i]; samples[i] = samples[j]; samples[j] = tmp;
            }
        }
    }
    return samples[valid_count / 2];
}

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(100)); // Pequeño delay para estabilizar el sistema
    printf("app_main: inicio\n");
    
    init_hw();
    printf("app_main: hardware listo\n");
    set_servo_angle(SERVO_CLOSED_ANGLE);
    printf("app_main: servo inicializado\n");
    wifi_init();
    printf("app_main: Wi-Fi inicializado\n");

    mqtt_app_start();
    printf("app_main: esperando MQTT...\n");
    for (int i = 0; i < 100 && !mqtt_connected; ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if ((i % 10) == 9) {
            printf("app_main: MQTT aún no conectado (%d00 ms)\n", i + 1);
        }
    }
    if (!mqtt_connected) {
        printf("Advertencia: MQTT no confirmó conexión a tiempo.\n");
    }
    enviar_estado("boot_ok");
    
    bool tapa_abierta = false;
    uint32_t tiempo_abierto = 0;
    int ultimo_porcentaje_valido = 0;
    const int UMBRAL_ABRIR = 5; // Distancia en cm para abrir la tapa
    // Espera en ms antes de cerrar automáticamente la tapa (3 segundos)
    const int TIEMPO_ESPERA_MS = 3000; 
    const float DISTANCIA_VACIO_CM = 27.0; // Calibrado con lectura real en vacío (~27.17 cm)
    const float DISTANCIA_LLENO_CM = 3.0;  // Zona cercana al sensor considerada lleno

    printf("Sistema de Bote Inteligente Iniciado...\n");

    while (1) {
        float dist_ext = get_distance(TRIG_OUT, ECHO_OUT);

        // 1. LÓGICA DE APERTURA
        if (dist_ext > 2 && dist_ext < UMBRAL_ABRIR) {
            if (!tapa_abierta) {
                printf(">>> Detectado. Abriendo...\n");
                move_servo_smoothly(SERVO_OPEN_ANGLE);
                tapa_abierta = true;
                tiempo_abierto = esp_timer_get_time() / 1000;
                // ENVIAR ESTADÍSTICA DE APERTURA
                enviar_dato(FEED_APERTURAS, 1); 
                printf(">>> Apertura registrada en la nube.\n");
            }
        }

        // 2. LÓGICA DE CIERRE Y MEDICIÓN ÚNICA
        if (tapa_abierta) {
            uint32_t ahora = esp_timer_get_time() / 1000;
            if (ahora - tiempo_abierto > TIEMPO_ESPERA_MS) {
                // MEDICIÓN ANTES DE CERRAR: tomamos la lectura mientras la tapa sigue abierta
                float dist_int = get_distance_filtered(TRIG_IN, ECHO_IN);
                printf("Comprobando nivel de llenado (antes de cerrar): %.2f cm\n", dist_int);

                int porcentaje_actual = ultimo_porcentaje_valido;
                if (dist_int > 0.0f) {
                    if (dist_int <= DISTANCIA_LLENO_CM) {
                        porcentaje_actual = 100;
                    } else if (dist_int >= DISTANCIA_VACIO_CM) {
                        porcentaje_actual = 0;
                    } else {
                        float span = DISTANCIA_VACIO_CM - DISTANCIA_LLENO_CM;
                        porcentaje_actual = (int)(((DISTANCIA_VACIO_CM - dist_int) / span) * 100.0f);
                    }

                    if (porcentaje_actual < 0) porcentaje_actual = 0;
                    if (porcentaje_actual > 100) porcentaje_actual = 100;
                    ultimo_porcentaje_valido = porcentaje_actual;
                } else {
                    printf("Lectura interior inválida; se mantiene último porcentaje válido: %d%%\n", ultimo_porcentaje_valido);
                }

                // Enviamos el nivel actual inmediatamente
                enviar_dato(FEED_LLENADO, porcentaje_actual);
                printf("Nivel en Dashboard: %d%%\n", porcentaje_actual);

                // Lógica de alerta WhatsApp: enviar una sola vez cuando pase del 90%
                if (porcentaje_actual >= 90) {
                    if (!whatsapp_alert_sent) {
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Alerta: Bote al %d%% de llenado.", porcentaje_actual);
                        if (send_whatsapp_message(msg)) {
                            whatsapp_alert_sent = true;
                            printf("Alerta WhatsApp enviada y bloqueada hasta que el nivel baje.\n");
                        } else {
                            printf("Fallo al enviar alerta WhatsApp. Se reintentará en la próxima comprobación si persiste el estado.\n");
                        }
                    }
                } else {
                    if (whatsapp_alert_sent) {
                        whatsapp_alert_sent = false;
                        printf("Nivel por debajo del 90%%; alerta WhatsApp rearmada.\n");
                    }
                }

                // Ahora cerramos la tapa después de medir
                printf(">>> Cerrando tapa...\n");
                move_servo_smoothly(SERVO_CLOSED_ANGLE);
                tapa_abierta = false;
                // Esperamos a que la tapa termine de bajar físicamente
                vTaskDelay(pdMS_TO_TICKS(700)); 
                // Registrar cierre en la nube (0 = cerrada)
                enviar_dato(FEED_APERTURAS, 0);
                printf(">>> Cierre registrado en la nube.\n");

            }
        }

        vTaskDelay(pdMS_TO_TICKS(150)); 
    }
}