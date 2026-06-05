# esp_blink 🤖🔌

> **Note:** This is a lightweight IoT/ESP32 project to interface with the Blink voice assistant framework.

This repository contains the ESP32 client codebase and Flask server configurations for deploying a headless/IoT physical instance of the Blink AI Voice Assistant.

---

## Architecture Overview
* **`ai.ino` (ESP32 Client):** Connects to Wi-Fi, captures I2S microphone input stream, sends audio binaries to Flask backend, and drives custom WS2812B ambient light strips based on server response packets.
* **`server.py` (Flask Backend):** Receives binary PCM audio packages from the client, runs Speech-To-Text (STT) inference, orchestrates task pipelines via Gemini LLM models, and returns compiled protocol buffers with TTS audio.

---

## License
This project is licensed under the MIT License.
