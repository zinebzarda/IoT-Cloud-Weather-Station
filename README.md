# 🌦️ Station Météorologique Locale – IoT & Cloud Computing

<p align="center">
  <strong>Station météorologique intelligente basée sur ESP32, MQTT et une architecture Cloud/IoT</strong>
</p>

---

## 👩‍💻 Réalisé par

**Zineb Zarda**  
**Rajae Boutahri**

Dans le cadre du module **IoT et Cloud Computing**

**Groupe : 10**

---

## 📌 Présentation du projet

Ce projet consiste à concevoir et simuler une **station météorologique locale** permettant de collecter différentes données environnementales à l'aide d'un **ESP32 simulé avec Wokwi**.

Les données collectées sont transmises à travers le protocole **MQTT** vers un broker public **EMQX**, puis traitées avec **Node-RED**, stockées dans **InfluxDB** et finalement visualisées sous forme de graphiques dans **Grafana**.

L'ensemble de l'infrastructure Cloud est déployé à l'aide de **Docker et Docker Compose**.

---

## 🎯 Objectifs

Les principaux objectifs du projet sont :

- Simuler une station météorologique avec un ESP32.
- Collecter différentes mesures environnementales.
- Transmettre les données via Wi-Fi et MQTT.
- Utiliser un broker MQTT pour assurer la communication.
- Traiter les données avec Node-RED.
- Stocker les données temporelles dans InfluxDB.
- Visualiser les données avec Grafana.
- Conteneuriser les services avec Docker.
- Mettre en place une architecture IoT complète et fonctionnelle.

---

## 🏗️ Architecture du système

```text
                    ┌──────────────────────┐
                    │      Wokwi           │
                    │       ESP32          │
                    │                      │
                    │  DHT22               │
                    │  BMP180              │
                    │  LDR                 │
                    │  Wind Sensor         │
                    │  Rain Sensor         │
                    │  OLED                │
                    └──────────┬───────────┘
                               │
                              Wi-Fi
                               │
                               ▼
                    ┌──────────────────────┐
                    │      EMQX Public     │
                    │     MQTT Broker       │
                    └──────────┬───────────┘
                               │
                              MQTT
                               │
                               ▼
                    ┌──────────────────────┐
                    │      Node-RED        │
                    │                      │
                    │ MQTT Reception       │
                    │ Data Processing      │
                    │ Data Formatting      │
                    └──────────┬───────────┘
                               │
                               ▼
                    ┌──────────────────────┐
                    │       InfluxDB       │
                    │   station_meteo      │
                    │                      │
                    │ Time-Series Data     │
                    └──────────┬───────────┘
                               │
                               ▼
                    ┌──────────────────────┐
                    │       Grafana        │
                    │                      │
                    │     Dashboard        │
                    │    Visualisation     │
                    └──────────────────────┘