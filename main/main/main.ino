/*
  Módulo principal para el manejo del rastreador basado en TTGO T-Beam.

  Modificado para corregir problemas con datos GPS y asegurar una integración estable con TTNMapper.

  Copyright (C) 2018 por Xose Pérez <xose dot perez at gmail dot com>

  Este programa es software libre: puedes redistribuirlo y/o modificarlo
  bajo los términos de la Licencia Pública General de GNU según lo
  publicado por la Free Software Foundation, ya sea la versión 3 de la
  Licencia, o (a tu elección) cualquier versión posterior.

  Este programa se distribuye con la esperanza de que sea útil,
  pero SIN NINGUNA GARANTÍA; sin siquiera la garantía implícita de
  COMERCIABILIDAD o IDONEIDAD PARA UN PROPÓSITO PARTICULAR. Consulta la
  Licencia Pública General de GNU para más detalles.

  Deberías haber recibido una copia de la Licencia Pública General de GNU
  junto con este programa. Si no, consulta <http://www.gnu.org/licenses/>.
*/

#include "configuration.h" // Configuración principal del proyecto
#include "rom/rtc.h"       // Soporte para RTC y variables persistentes
#include <TinyGPS++.h>      // Biblioteca para manejo de datos GPS
#include <Wire.h>           // Comunicación I2C

#include "axp20x.h"        // Controlador del PMU AXP192

AXP20X_Class axp;           // Objeto para manejar el PMU AXP192
bool pmu_irq = false;       // Bandera para interrupciones del PMU
String baChStatus = "No charging"; // Estado inicial de la batería

// Indicadores para dispositivos detectados
bool ssd1306_found = false; // Indica si la pantalla OLED está conectada
bool axp192_found = false;  // Indica si el PMU AXP192 está presente

// Indicadores para el estado de los paquetes LoRaWAN
bool packetSent = false;    // Indica si se envió un paquete
bool packetQueued = false;  // Indica si hay un paquete en cola

// Buffers para datos de transmisión LoRaWAN
#if defined(PAYLOAD_USE_FULL)
    // Payload que incluye el número de satélites y precisión
    static uint8_t txBuffer[10];
#elif defined(PAYLOAD_USE_CAYENNE)
    // Formato de payload CayenneLPP
    static uint8_t txBuffer[11] = {0x03, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
#endif

// Variables para soporte de deep sleep
RTC_DATA_ATTR int bootCount = 0;          // Contador de reinicios durante deep sleep
esp_sleep_source_t wakeCause;            // Causa del último reinicio

// -----------------------------------------------------------------------------
// Lógica principal de la aplicación
// -----------------------------------------------------------------------------

/**
 * Construye el paquete de datos para enviar a TTN.
 * El contenido exacto depende de la configuración del payload.
 * @param txBuffer Array donde se almacenará el paquete construido.
 */
void buildPacket(uint8_t txBuffer[]); // Declaración de la función, implementada en otro lugar.

/**
 * Intenta enviar un paquete si los datos GPS son válidos.
 * @return true si se envió el paquete, false en caso contrario.
 */
bool trySend() {
    packetSent = false; // Reinicia el estado del envío.

    // Verifica si los datos GPS son válidos:
    // - HDOP menor a 50 (precisión aceptable).
    // - Coordenadas (latitud, longitud) y altitud válidas.
    if (0 < gps_hdop() && gps_hdop() < 50 && gps_latitude() != 0 && gps_longitude() != 0 && gps_altitude() != 0) {
        char buffer[40];

        // Muestra la latitud en la pantalla OLED y puerto Serie
        snprintf(buffer, sizeof(buffer), "Latitud: %10.6f", gps_latitude());
        Serial.println(buffer);
        setDisplayMessage(buffer);

        // Muestra la longitud en la pantalla OLED y puerto Serie
        snprintf(buffer, sizeof(buffer), "Longitud: %10.6f", gps_longitude());
        Serial.println(buffer);
        setDisplayMessage(buffer);

        // Muestra el error de precisión (HDOP) en la pantalla OLED y puerto Serie
        snprintf(buffer, sizeof(buffer), "HDOP: %4.2f", gps_hdop());
        Serial.println(buffer);
        setDisplayMessage(buffer);

        // Construye el paquete con los datos GPS
        buildPacket(txBuffer);

        // Control de confirmaciones (opcional según configuración)
        #if LORAWAN_CONFIRMED_EVERY > 0
        bool confirmed = (ttn_get_count() % LORAWAN_CONFIRMED_EVERY == 0);
        if (confirmed) {
            Serial.println("Confirmación activada para este envío.");
        }
        #endif

        // Envía el paquete a TTN
        packetQueued = true;
        ttn_send(txBuffer, sizeof(txBuffer), LORAWAN_PORT, false);

        // Muestra mensaje de éxito en la pantalla
        setDisplayMessage("Paquete enviado con éxito");
        Serial.println("Paquete enviado con éxito");

        return true;
    } else {
        // Muestra mensaje indicando que los datos GPS no son válidos
        setDisplayMessage("Esperando datos GPS...");
        Serial.println("Esperando datos GPS válidos...");
        return false;
    }
}


/**
 * Configura el modo deep sleep durante un tiempo específico.
 * @param msecToWake Tiempo en milisegundos para despertar.
 */
void doDeepSleep(uint64_t msecToWake) {
    Serial.printf("Entrando en deep sleep por %llu segundos\n", msecToWake / 1000);

    // Apaga la pantalla OLED para ahorrar energía
    screen_off();

    // Apaga el módulo LoRa de manera limpia
    LMIC_shutdown();

    // Apaga las salidas de potencia del PMU si está presente
    if (axp192_found) {
        axp.setPowerOutPut(AXP192_LDO2, AXP202_OFF); // Apaga el LoRa
        axp.setPowerOutPut(AXP192_LDO3, AXP202_OFF); // Apaga el GPS
    }

    // Configuración para mantener los periféricos RTC encendidos
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Configura el botón como fuente de interrupción para despertar
    uint64_t gpioMask = (1ULL << BUTTON_PIN);
    gpio_pullup_en((gpio_num_t) BUTTON_PIN);
    esp_sleep_enable_ext1_wakeup(gpioMask, ESP_EXT1_WAKEUP_ALL_LOW);

    // Configura un temporizador para despertar después de `msecToWake`
    esp_sleep_enable_timer_wakeup(msecToWake * 1000ULL);

    // Inicia el modo deep sleep
    esp_deep_sleep_start();
}

/**
 * Configura el modo de bajo consumo entre mensajes.
 * - Muestra un mensaje en la pantalla antes de entrar en deep sleep.
 * - Calcula el tiempo restante hasta el próximo intervalo de envío.
 */
void sleep() {
#if SLEEP_BETWEEN_MESSAGES
    // Si la pantalla OLED está conectada, muestra un mensaje antes de dormir
    if (ssd1306_found) {
        char buffer[20];
        snprintf(buffer, sizeof(buffer), "Sleeping in %3.1fs\n", (MESSAGE_TO_SLEEP_DELAY / 1000.0));
        screen_print(buffer);

        // Espera un tiempo antes de apagar la pantalla
        delay(MESSAGE_TO_SLEEP_DELAY);

        // Apaga la pantalla OLED
        screen_off();
    }

    // Configura el botón de usuario para despertar el dispositivo
    sleep_interrupt(BUTTON_PIN, LOW);

    // Calcula el tiempo restante hasta el próximo envío
    uint32_t sleep_for = (millis() < SEND_INTERVAL) ? SEND_INTERVAL - millis() : SEND_INTERVAL;

    // Entra en modo deep sleep
    doDeepSleep(sleep_for);
#endif
}


/**
 * Callback para manejar eventos provenientes de TTN.
 * Dependiendo del evento, se actualiza la pantalla OLED y se procesan los mensajes.
 * @param message Código del evento recibido.
 */
void callback(uint8_t message) {
    bool ttn_joined = false; // Indica si el dispositivo se unió a TTN.

    // Manejo de eventos específicos
    if (EV_JOINED == message) {
        ttn_joined = true;
    }

    if (EV_JOINING == message) {
        if (ttn_joined) {
            screen_print("TTN joining...\n");
        } else {
            screen_print("Joined TTN!\n");
        }
        // delay(1500); // Tiempo para leer el mensaje en la pantalla.
    }

    if (EV_JOIN_FAILED == message) screen_print("TTN join failed\n");
    if (EV_REJOIN_FAILED == message) screen_print("TTN rejoin failed\n");
    if (EV_RESET == message) screen_print("Reset TTN connection\n");
    if (EV_LINK_DEAD == message) screen_print("TTN link dead\n");
    if (EV_ACK == message) screen_print("ACK received\n");
    if (EV_PENDING == message) screen_print("Message discarded\n");
    if (EV_QUEUED == message) screen_print("Message queued\n");

    // Mostramos mensajes específicos en la pantalla OLED
    switch (message) {
        case EV_JOINING:
            Serial.println("Conectando a TTN...");
            setDisplayMessage("Conectando a TTN...");
            break;

        case EV_JOINED:
            ttn_joined = true;
            Serial.println("Conexión establecida con TTN");
            setDisplayMessage("Conexión con TTN");
            break;

        case EV_JOIN_FAILED:
            Serial.println("Conexión a TTN fallida");
            setDisplayMessage("Conexión fallida");
            break;

        case EV_TXCOMPLETE:
            Serial.println("Paquete transmitido con éxito");
            setDisplayMessage("Paquete enviado");
            break;

        case EV_TXCANCELED:
            Serial.println("Error al enviar el paquete");
            setDisplayMessage("Fallo al enviar paquete");
            break;

        default:
            Serial.printf("Evento TTN no manejado: %d\n", message);
            break;
    }

    // Procesamiento adicional para paquetes transmitidos
    if (EV_TXCOMPLETE == message && packetQueued) {
        screen_print("Message sent\n");
        packetQueued = false;
        packetSent = true;
        // delay(2000); // Tiempo para leer el mensaje.
    }

    // Manejo de respuestas desde TTN
    if (EV_RESPONSE == message) {
        screen_print("[TTN] Response: ");

        size_t len = ttn_response_len();
        uint8_t data[len];
        ttn_response(data, len);

        char buffer[6];
        for (uint8_t i = 0; i < len; i++) {
            snprintf(buffer, sizeof(buffer), "%02X", data[i]);
            screen_print(buffer);
        }
        screen_print("\n");
    }
}


/**
 * Escanea dispositivos I2C conectados al bus.
 * - Detecta la pantalla OLED (SSD1306).
 * - Detecta el PMU AXP192.
 * - Imprime información de depuración en el puerto serie.
 */
void scanI2Cdevice(void) {
    byte err, addr;        // Variables para errores y direcciones I2C.
    int nDevices = 0;      // Contador de dispositivos encontrados.

    Serial.println("Escaneando dispositivos I2C...");

    // Recorre todas las direcciones I2C posibles (0x01 a 0x7F).
    for (addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr); // Inicia la transmisión I2C.
        err = Wire.endTransmission(); // Finaliza y captura el estado.

        if (err == 0) { // Si no hay error, se encontró un dispositivo.
            Serial.print("Dispositivo I2C encontrado en la dirección 0x");
            if (addr < 16) Serial.print("0"); // Agrega un cero si la dirección es menor a 0x10.
            Serial.print(addr, HEX);
            Serial.println(" !");

            nDevices++; // Incrementa el contador de dispositivos.

            // Verifica si el dispositivo es la pantalla OLED.
            if (addr == SSD1306_ADDRESS) {
                ssd1306_found = true;
                Serial.println("Pantalla SSD1306 detectada.");
            }

            // Verifica si el dispositivo es el PMU AXP192.
            if (addr == AXP192_SLAVE_ADDRESS) {
                axp192_found = true;
                Serial.println("PMU AXP192 detectado.");
            }
        } else if (err == 4) { // Error desconocido en la transmisión I2C.
            Serial.print("Error desconocido en la dirección 0x");
            if (addr < 16) Serial.print("0");
            Serial.println(addr, HEX);
        }
    }

    // Imprime el resultado final del escaneo.
    if (nDevices == 0) {
        Serial.println("No se encontraron dispositivos I2C.");
    } else {
        Serial.println("Escaneo I2C completado.");
    }
}


/**
 * Inicializa el PMU AXP192.
  * axp192 power 
    DCDC1 0.7-3.5V @ 1200mA max -> OLED  // If you turn this off you'll lose comms to the axp192 because the OLED and the axp192 share the same i2c bus, instead use ssd1306 sleep mode
    DCDC2 -> unused
    DCDC3 0.7-3.5V @ 700mA max -> ESP32 (keep this on!)
    LDO1 30mA -> charges GPS backup battery  // charges the tiny J13 battery by the GPS to power the GPS ram (for a couple of days), can not be turned off
    LDO2 200mA -> LORA
    LDO3 200mA -> GPS
 */
/**
 * Inicializa el chip de gestión de energía AXP192.
 * Configura las salidas necesarias para LoRa, GPS y OLED.
 * Realiza verificaciones iniciales y habilita las interrupciones necesarias.
 */
void axp192Init() {
    if (axp192_found) {
        // Inicializa el PMU AXP192
        if (!axp.begin(Wire, AXP192_SLAVE_ADDRESS)) {
            Serial.println("AXP192 inicializado correctamente.");
        } else {
            Serial.println("Error al inicializar el AXP192.");
            return;
        }

        // Imprime el estado inicial de las salidas del PMU
        Serial.printf("DCDC1: %s\n", axp.isDCDC1Enable() ? "ENABLE" : "DISABLE");
        Serial.printf("DCDC2: %s\n", axp.isDCDC2Enable() ? "ENABLE" : "DISABLE");
        Serial.printf("LDO2: %s\n", axp.isLDO2Enable() ? "ENABLE" : "DISABLE");
        Serial.printf("LDO3: %s\n", axp.isLDO3Enable() ? "ENABLE" : "DISABLE");
        Serial.printf("DCDC3: %s\n", axp.isDCDC3Enable() ? "ENABLE" : "DISABLE");
        Serial.printf("Exten: %s\n", axp.isExtenEnable() ? "ENABLE" : "DISABLE");
        Serial.println("----------------------------------------");

        // Configura las salidas necesarias
        axp.setPowerOutPut(AXP192_LDO2, AXP202_ON); // Habilita LoRa
        axp.setPowerOutPut(AXP192_LDO3, AXP202_ON); // Habilita GPS
        axp.setPowerOutPut(AXP192_DCDC2, AXP202_ON); // Habilita salida DC2 (sin usar en este caso)
        axp.setPowerOutPut(AXP192_EXTEN, AXP202_ON); // Habilita salida EXTEN (si aplica)
        axp.setPowerOutPut(AXP192_DCDC1, AXP202_ON); // Habilita la alimentación de la OLED

        // Configura el voltaje de DCDC1 (3.3V para OLED)
        axp.setDCDC1Voltage(3300);

        // Imprime el estado actualizado de las salidas
        Serial.printf("DCDC1: %s\n", axp.isDCDC1Enable() ? "ENABLE" : "DISABLE");
        Serial.printf("DCDC2: %s\n", axp.isDCDC2Enable() ? "ENABLE" : "DISABLE");
        Serial.printf("LDO2: %s\n", axp.isLDO2Enable() ? "ENABLE" : "DISABLE");
        Serial.printf("LDO3: %s\n", axp.isLDO3Enable() ? "ENABLE" : "DISABLE");
        Serial.printf("DCDC3: %s\n", axp.isDCDC3Enable() ? "ENABLE" : "DISABLE");
        Serial.printf("Exten: %s\n", axp.isExtenEnable() ? "ENABLE" : "DISABLE");

        // Configura el pin de interrupción del PMU
        pinMode(PMU_IRQ, INPUT_PULLUP);
        attachInterrupt(PMU_IRQ, [] {
            pmu_irq = true;
        }, FALLING);

        // Habilita ADC para monitorear la corriente de la batería
        axp.adc1Enable(AXP202_BATT_CUR_ADC1, 1);

        // Habilita interrupciones para eventos de alimentación
        axp.enableIRQ(AXP202_VBUS_REMOVED_IRQ | AXP202_VBUS_CONNECT_IRQ |
                      AXP202_BATT_REMOVED_IRQ | AXP202_BATT_CONNECT_IRQ, 1);
        axp.clearIRQ();

        // Verifica el estado de carga de la batería
        if (axp.isCharging()) {
            baChStatus = "Charging";
            Serial.println("La batería está cargando.");
        } else {
            Serial.println("La batería no está cargando.");
        }
    } else {
        Serial.println("AXP192 no detectado.");
    }
}
/**
 * Inicialización después de despertar del modo deep sleep.
 * - Incrementa el contador de reinicios.
 * - Determina la causa del reinicio.
 * - (Opcional) Maneja eventos relacionados con botones de despertar.
 */


void initDeepSleep() {
    // Incrementa el contador de reinicios
    bootCount++;

    // Determina la causa del reinicio (despertar)
    wakeCause = esp_sleep_get_wakeup_cause();

    // Depuración: imprime la causa del reinicio y el número de reinicios
    Serial.printf("Dispositivo iniciado, causa del reinicio: %d (reinicios: %d)\n", wakeCause, bootCount);

    /* Opcional: Manejo avanzado de botones para despertar
       Este código se puede habilitar si se desea manejar múltiples botones o reglas específicas
       para determinar qué botón despertó el dispositivo.
    */

    /*
    uint64_t wakeButtons = esp_sleep_get_ext1_wakeup_status(); // Botones que activaron el despertar
    if (wakeCause == ESP_SLEEP_WAKEUP_EXT1 && !wakeButtons) {
        // Si no se identifican botones, se asume el primero como predeterminado
        wakeButtons = ((uint64_t)1) << buttons.gpios[0];
    }
    */
}

/**
 * Configuración inicial del sistema.
 * Inicializa los periféricos, la pantalla OLED, el GPS, el PMU y el LoRa.
 */
void setup() {  //////////////////////////////////////////////////////////////////////////////////////////////////////
    // Configuración del puerto serie para depuración
    #ifdef DEBUG_PORT
        DEBUG_PORT.begin(SERIAL_BAUD);
    #endif

    Serial.begin(SERIAL_BAUD); // Configura la velocidad del puerto serie
    Serial.println("Iniciando dispositivo...");

    // Inicializa las variables y verifica la causa del reinicio
    initDeepSleep();

    // Inicializa el bus I2C
    Wire.begin(I2C_SDA, I2C_SCL);

    // Escanea dispositivos I2C y configura banderas
    scanI2Cdevice();

    // Inicializa el PMU AXP192
    axp192Init();

    // Configura el botón de usuario
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Configura el LED si está definido
    #ifdef LED_PIN
        pinMode(LED_PIN, OUTPUT);
    #endif

    // Mensaje de bienvenida en depuración
    DEBUG_MSG(APP_NAME " " APP_VERSION "\n");

    // No inicializa la pantalla si el reinicio fue causado por un temporizador
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
        ssd1306_found = false; // Asume que el hardware no está presente
    }

    // Configura la pantalla OLED si está presente
    if (ssd1306_found) screen_setup();

    // Inicializa el GPS
    gps_setup();

    // Muestra el logo en el primer arranque
    #ifndef ALWAYS_SHOW_LOGO
    if (bootCount == 0) {
    #endif
        screen_print(APP_NAME " - - - " APP_VERSION, 0, 0); // Mensaje en la pantalla
        screen_show_logo(); // Muestra el logo en el OLED
        screen_update(); // Actualiza la pantalla
        delay(LOGO_DELAY); // Espera para mostrar el logo
    #ifndef ALWAYS_SHOW_LOGO
    }
    #endif

    // Configuración de TTN (The Things Network)
    if (!ttn_setup()) {
        // Error al detectar el módulo LoRa
        screen_print("[ERR] Radio module not found!\n");

        if (REQUIRE_RADIO) {
            delay(MESSAGE_TO_SLEEP_DELAY);
            screen_off();
            sleep_forever(); // Entra en modo de bajo consumo indefinido
        }
    } else {
        // Configura el callback para manejar eventos de TTN
        ttn_register(callback);
        ttn_join(); // Intenta unirse a TTN
        ttn_adr(LORAWAN_ADR); // Habilita ADR en TTN
    }
    
    Serial.println("Todos los módulos inicializados.");
}


/**
 * Ciclo principal del programa.
 * - Gestiona el GPS, la comunicación con TTN y la pantalla OLED.
 * - Maneja eventos de usuario (botón) y el envío de datos.
 */
void loop() {  ////////////////////////////////////////////////////////////////////////////////////////////////////// 
    // Actualiza los módulos GPS, TTN y la pantalla OLED
    gps_loop();      // Procesa datos GPS
    ttn_loop();      // Procesa eventos TTN
    screen_loop();   // Actualiza la pantalla OLED
    delay(2000);     // Espera un breve período (optimizable si es necesario)

    // Si se envió un paquete, entra en modo de bajo consumo
    if (packetSent) {
        packetSent = false;
        sleep(); // Llama a la función de sleep entre envíos
    }

    // Manejo del botón de usuario para descartar preferencias
    static bool wasPressed = false;      // Indica si el botón está presionado
    static uint32_t minPressMs;          // Tiempo mínimo para considerar una pulsación larga
    if (!digitalRead(BUTTON_PIN)) {      // Si el botón está presionado
        if (!wasPressed) {
            // Inicia una nueva pulsación
            Serial.println("Botón presionado");
            wasPressed = true;
            minPressMs = millis() + 3000; // Configura un tiempo de espera de 3 segundos
        }
    } else if (wasPressed) {
        // Maneja la liberación del botón
        wasPressed = false;
        if (millis() > minPressMs) { // Si el botón estuvo presionado por más de 3 segundos
            #ifndef PREFS_DISCARD
                screen_print("Descartar preferencias deshabilitado\n");
            #endif

            #ifdef PREFS_DISCARD
                screen_print("Descartando preferencias!\n");
                ttn_erase_prefs();       // Borra las preferencias de TTN
                delay(5000);             // Tiempo para leer el mensaje en pantalla
                ESP.restart();           // Reinicia el dispositivo
            #endif
        }
    }

    // Lógica de envío de datos periódicos
    static uint32_t last = 0;  // Marca de tiempo del último envío
    static bool first = true;  // Indica si es el primer intento de envío
    if (0 == last || millis() - last > SEND_INTERVAL) {
        if (trySend()) {       // Intenta enviar un paquete si los datos GPS son válidos
            last = millis();   // Actualiza el tiempo del último envío
            first = false;
            Serial.println("TRANSMITIDO");
        } else {
            if (first) {
                // Muestra un mensaje indicando que espera datos GPS
                screen_print("Esperando datos GPS\n");
                first = false;
            }

            #ifdef GPS_WAIT_FOR_LOCK
            // Si el GPS no obtiene un lock dentro de un tiempo, entra en modo de bajo consumo
            if (millis() > GPS_WAIT_FOR_LOCK) {
                sleep();
            }
            #endif

            // Reduce el consumo mientras espera el lock del GPS
            delay(100);
        }
    }
}

