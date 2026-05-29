# PunkyBin

Proyecto desarrollado con ESP32 que implementa un bote de basura inteligente utilizando sensores ultrasónicos, un servomotor y comunicación MQTT para monitoreo remoto.

## Descripción

El sistema detecta cuando una persona se acerca al bote mediante un sensor ultrasónico exterior. Cuando se detecta movimiento a cierta distancia, la tapa se abre automáticamente usando un servomotor.

Además, el bote cuenta con un segundo sensor ultrasónico interno que mide el nivel de llenado. Esta información se envía a Adafruit IO mediante MQTT para poder monitorear el porcentaje de llenado desde la nube.

Cuando el bote alcanza un nivel crítico de capacidad, el sistema envía automáticamente una notificación por WhatsApp utilizando CallMeBot.

## Funciones principales

- Apertura automática de la tapa.
- Medición del nivel de llenado del bote.
- Envío de datos a Adafruit IO usando MQTT.
- Notificaciones automáticas por WhatsApp usando CallMeBot.
- Sistema de detección de capacidad máxima.
- Actualización del porcentaje de llenado en tiempo real.

# Instrucciones de compilación y ejecución

## 1. Requisitos previos

Antes de compilar el proyecto es necesario tener instalado:

- ESP-IDF
- Python
- Git
- Drivers del ESP32

Documentación oficial de ESP-IDF:

https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/

---

## 2. Clonar el repositorio

```bash
git clone git@github.com:Lilly-8/PunkyBin.git
```

Entrar a la carpeta del proyecto:

```bash
cd PunkyBin
```

---

## 3. Configurar credenciales

Dentro del código es necesario configurar:

- SSID del Wi-Fi
- Contraseña del Wi-Fi
- Usuario de Adafruit IO
- API Key de Adafruit IO
- Número telefónico y API Key de CallMeBot

Por seguridad, estas credenciales no se incluyen en el repositorio.

---

## 4. Compilar el proyecto

```bash
idf.py build
```

---

## 5. Cargar el programa al ESP32

```bash
idf.py flash
```

---

## 6. Abrir monitor serial

```bash
idf.py monitor
```
