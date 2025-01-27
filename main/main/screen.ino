/*
  Módulo para manejar la pantalla OLED SSD1306.

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

#include <Wire.h>             // Comunicación I2C
#include "SSD1306Wire.h"      // Controlador para la pantalla OLED SSD1306
#include "OLEDDisplay.h"      // Controlador genérico de pantalla OLED
#include "images.h"           // Imágenes a mostrar (logo, íconos, etc.)
#include "fonts.h"            // Fuentes personalizadas para la pantalla

// Altura del encabezado de la pantalla OLED
#define SCREEN_HEADER_HEIGHT    14

// Objeto global para manejar la pantalla OLED
SSD1306Wire * display;

// Línea inicial para texto debajo del encabezado
uint8_t _screen_line = SCREEN_HEADER_HEIGHT - 1;

// Variable global para almacenar el mensaje actual mostrado en la pantalla
String currentMessage = ""; // Inicialmente vacío

/**
 * Dibuja el encabezado en la pantalla OLED.
 * - Muestra el logo de TTN, número de mensajes, estado de batería/tiempo y satélites GPS.
 */
void _screen_header() {
    if (!display) return; // Verifica que la pantalla esté inicializada

    char buffer[20];

    // Número de mensajes enviados
    snprintf(buffer, sizeof(buffer), "#%03d", ttn_get_count() % 1000);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(0, 2, buffer);

    // Estado de batería o tiempo GPS
    if (axp192_found && millis() % 8000 < 3000) {
        snprintf(buffer, sizeof(buffer), "%.1fV %.0fmA",
                 axp.getBattVoltage() / 1000,
                 axp.getBattChargeCurrent() - axp.getBattDischargeCurrent());
    } else {
        gps_time(buffer, sizeof(buffer)); // Obtiene el tiempo desde el GPS
    }
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(display->getWidth() / 2, 2, buffer);

    // Cantidad de satélites conectados
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->drawString(display->getWidth() - SATELLITE_IMAGE_WIDTH - 4, 2,
                        itoa(gps_sats(), buffer, 10));
    display->drawXbm(display->getWidth() - SATELLITE_IMAGE_WIDTH, 0,
                     SATELLITE_IMAGE_WIDTH, SATELLITE_IMAGE_HEIGHT,
                     SATELLITE_IMAGE);
}

/**
 * Muestra el logo de TTN centrado en la pantalla OLED.
 * - Calcula dinámicamente la posición para centrar la imagen en la pantalla.
 */
void screen_show_logo() {
    if (!display) return; // Verifica que la pantalla esté inicializada

    // Calcula las coordenadas para centrar el logo
    uint8_t x = (display->getWidth() - TTN_IMAGE_WIDTH) / 2;
    uint8_t y = SCREEN_HEADER_HEIGHT + 
                (display->getHeight() - SCREEN_HEADER_HEIGHT - TTN_IMAGE_HEIGHT) / 2 + 1;

    // Dibuja el logo en la pantalla OLED
    display->drawXbm(x, y, TTN_IMAGE_WIDTH, TTN_IMAGE_HEIGHT, TTN_IMAGE);

    // Opcional: Actualiza la pantalla si esta función no es seguida por un `display->display()`.
    display->display();
}

/**
 * Apaga la pantalla OLED.
 * Reduce el consumo energético al desactivar el módulo de visualización.
 */
void screen_off() {
    if (!display) return; // Verifica que la pantalla esté inicializada
    display->displayOff(); // Apaga la pantalla OLED
}

/**
 * Enciende la pantalla OLED.
 * Reactiva el módulo de visualización después de estar apagado.
 */
void screen_on() {
    if (!display) return; // Verifica que la pantalla esté inicializada
    display->displayOn(); // Enciende la pantalla OLED
}

/**
 * Limpia el contenido de la pantalla OLED.
 * Borra todos los elementos previamente dibujados en el buffer.
 */
void screen_clear() {
    if (!display) return; // Verifica que la pantalla esté inicializada
    display->clear();     // Limpia el contenido del buffer

    // Opcional: Actualiza la pantalla si esta función no es seguida por un `display->display()`.
    display->display();
}

/**
 * Imprime texto en la pantalla OLED en una posición específica con una alineación definida.
 * @param text Texto a mostrar.
 * @param x Posición horizontal en píxeles.
 * @param y Posición vertical en píxeles.
 * @param alignment Alineación del texto (izquierda, centro, derecha).
 */
void screen_print(const char * text, uint8_t x, uint8_t y, uint8_t alignment) {
    DEBUG_MSG(text); // Depuración: imprime el texto en el puerto serie

    if (!display) return; // Verifica que la pantalla esté inicializada

    // Configura la alineación y dibuja el texto en la posición especificada
    display->setTextAlignment((OLEDDISPLAY_TEXT_ALIGNMENT) alignment);
    display->drawString(x, y, text);
}

/**
 * Imprime texto en la pantalla OLED en una posición específica con alineación por defecto (izquierda).
 * @param text Texto a mostrar.
 * @param x Posición horizontal en píxeles.
 * @param y Posición vertical en píxeles.
 */
void screen_print(const char * text, uint8_t x, uint8_t y) {
    screen_print(text, x, y, TEXT_ALIGN_LEFT); // Llama a la sobrecarga principal con alineación por defecto
}

/**
 * Imprime texto en la pantalla OLED en la siguiente línea disponible.
 * Desplaza el contenido si es necesario.
 * @param text Texto a mostrar.
 */
void screen_print(const char * text) {
    Serial.printf("Screen: %s\n", text); // Depuración: imprime el texto en el puerto serie

    if (!display) return; // Verifica que la pantalla esté inicializada

    // Imprime el texto en la pantalla
    display->print(text);

    // Manejo de desplazamiento si el texto excede la altura de la pantalla
    if (_screen_line + 8 > display->getHeight()) {
        // Implementar desplazamiento si es necesario
        // Ejemplo: mover el buffer o reiniciar _screen_line
    }

    // Incrementa la línea actual
    _screen_line += 8;

    // Actualiza la pantalla OLED
    screen_loop();
}

/**
 * Actualiza la pantalla OLED.
 * - Envía el contenido del buffer a la pantalla para que los cambios sean visibles.
 */
void screen_update() {
    if (display) {
        display->display(); // Actualiza la pantalla con el contenido actual del buffer
    }
}

/**
 * Configura un mensaje global para mostrar debajo del encabezado.
 * Llama automáticamente a `screen_loop` para actualizar el contenido de la pantalla OLED.
 * @param mensaje Mensaje que se mostrará en la pantalla.
 */
void setDisplayMessage(const String& mensaje) {
    currentMessage = mensaje; // Actualiza el mensaje global con el nuevo contenido
    screen_loop();            // Redibuja el encabezado y el mensaje en la pantalla
}

/**
 * Configura e inicializa la pantalla OLED.
 * - Crea una instancia de la pantalla.
 * - Configura orientación, fuente y buffer de desplazamiento.
 */
void screen_setup() {
    // Crea la instancia de la pantalla OLED con las conexiones I2C
    display = new SSD1306Wire(SSD1306_ADDRESS, I2C_SDA, I2C_SCL);

    // Inicializa la pantalla OLED
    display->init();

    // Invierte la orientación de la pantalla para una vista correcta
    display->flipScreenVertically();

    // Configura la fuente predeterminada para el texto
    display->setFont(Custom_ArialMT_Plain_10);

    // Configura un buffer de desplazamiento (log buffer) para mostrar texto adicional
    // - 5 líneas de altura, 30 caracteres por línea
    // display->setLogBuffer(5, 30);
}


/**
 * Actualiza la pantalla OLED.
 * - Limpia la pantalla y redibuja el encabezado.
 * - Muestra un mensaje global dinámico debajo del encabezado.
 * - Maneja eventos del PMU (AXP192) si está presente.
 */
void screen_loop() {
    if (!display) return; // Verifica que la pantalla esté inicializada

    #ifdef T_BEAM_V10
    // Manejo de interrupciones del PMU AXP192
    if (axp192_found && pmu_irq) {
        pmu_irq = false; // Reinicia la bandera de interrupción
        axp.readIRQ();   // Lee el estado de las interrupciones del PMU

        if (axp.isChargingIRQ()) {
            baChStatus = "Charging";
        } else if (axp.isVbusRemoveIRQ()) {
            baChStatus = "No Charging";
        } else {
            baChStatus = "No Charging";
        }

        Serial.println(baChStatus); // Imprime el estado de carga en el puerto serie
        digitalWrite(2, !digitalRead(2)); // Indica el estado de carga (opcional)
        axp.clearIRQ(); // Limpia las interrupciones del PMU
    }
    #endif

    // Limpia la pantalla
    display->clear();

    // Dibuja el encabezado
    _screen_header();

    // Muestra el mensaje global debajo del encabezado, si existe
    if (currentMessage != "") {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(ArialMT_Plain_10);
        display->drawString(0, SCREEN_HEADER_HEIGHT + 5, currentMessage);
    }

    // Dibuja el buffer de logs (si está habilitado)
    display->drawLogBuffer(0, SCREEN_HEADER_HEIGHT);

    // Actualiza la pantalla OLED con los cambios
    display->display();
}





