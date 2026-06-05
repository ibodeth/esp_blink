# esp_blink

An IoT firmware client and backend server to interface physical microphone input and LED displays with the Blink voice assistant.

## How it Works
The ESP32 microcontroller connects to local Wi-Fi, captures microphone input over I2S, streams the raw audio binary to the companion Flask server, and drives a WS2812B LED status indicator. The Flask backend server processes the incoming binary stream, communicates with the Gemini API to get responses, and returns Text-to-Speech audio and state packets to the ESP32.

## Tech Stack
- **Languages/Frameworks:** C++, Python, Flask
- **Services/Libraries:** Arduino IDE (FastLED, WiFi, I2S libraries), Google Gemini API, Microsoft Edge TTS
- **Infrastructure:** ESP32, WS2812B Addressable LED, Flask Backend Server

## Local Setup
1. Upload the `ai.ino` firmware file to your ESP32 board using the Arduino IDE.
2. Clone the repository and navigate to the directory:
   ```bash
   git clone https://github.com/ibodeth/esp_blink.git
   cd esp_blink
   ```
3. Set up a virtual environment and start the Flask backend server:
   ```bash
   python -m venv venv
   source venv/bin/activate  # On Windows: venv\Scripts\activate
   pip install -r requirements.txt
   python server.py
   ```

## License
MIT
