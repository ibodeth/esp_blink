"""
BLINK AI ASSISTANT — Backend v16.0 (Matrix Agent Edition)
"""
import io, wave, struct, json, os, time, subprocess, tempfile, sqlite3, logging
from pathlib import Path
from flask import Flask, request, Response
import speech_recognition as sr
import google.generativeai as genai
from gtts import gTTS
from pydub import AudioSegment
import requests

# LOGGING
logging.basicConfig(level=logging.INFO)
log = logging.getLogger("blink")

GEMINI_API_KEY = os.environ.get("GEMINI_API_KEY", "")
model = genai.GenerativeModel("gemini-3.1-flash-lite-preview")
AudioSegment.converter = "/usr/bin/ffmpeg"

app = Flask(__name__)

# ── HAVA DURUMU (Matrix Analiz Modu) ──────────────────────────────────────────
def get_weather_analysis(city):
    url = "https://api.open-meteo.com/v1/forecast?latitude=37.87&longitude=32.48&daily=temperature_2m_max,temperature_2m_min,weathercode&timezone=auto"
    try:
        r = requests.get(url, timeout=10).json()
        daily = r.get("daily", {})
        return "\n".join([f"{daily['time'][i]}: {daily['temperature_2m_max'][i]}°C/{daily['temperature_2m_min'][i]}°C, Kod:{daily['weathercode'][i]}" for i in range(len(daily.get("time", [])))])
    except: return "Veri akışı kesildi."

# ── YARDIMCILAR ─────────────────────────────────────────────────────────────
def text_to_pcm(text, vol_db=22):
    try:
        tts = gTTS(text=text, lang="tr", slow=False)
        mp3 = io.BytesIO(); tts.write_to_fp(mp3); mp3.seek(0)
        seg = AudioSegment.from_file(mp3, format="mp3") 
        seg = seg.set_frame_rate(16000).set_channels(1).set_sample_width(2)
        seg = (seg - vol_db) + AudioSegment.silent(duration=800, frame_rate=16000)
        return bytes(seg.raw_data)
    except: return b""

def pkt_audio(text, pcm):
    tb = text.encode("utf-8")
    return struct.pack("<BHI", 0x02, len(tb), len(pcm)) + tb + pcm

# ── MATRIX PROMPT ───────────────────────────────────────────────────────────
def intent_prompt(user_text, now, city):
    return f"""[SYSTEM] Adın Blink. Matrix sisteminin bir parçasısın. 
Karakterin: Ciddi, teknolojik, Jarvis gibi ama Matrix estetiğinde.
Sen bir Agentsın: Tüm niyetleri sırayla kuyruğa diz.
[ZAMAN] {now} | [KONUM] {city} | [GİRDİ] "{user_text}"

[KURALLAR]
1. IŞIK: MUTLAKA hex kodu döndür (#00FF41 en uygun yeşildir).
2. MÜZİK: Her zaman kuyruğun en sonuna koy.
3. HAVA: 'target' kısmına istenen tarihi yaz.
4. CEVAPLAR: Teknik, net ve akıllıca olsun.

[OUTPUT FORMAT]
{{"queue": [
  {{"type": "ambient_light", "target": "#HEX", "response": "Sistem rengi güncellendi."}},
  {{"type": "chat", "response": "Analiz tamamlandı..."}},
  {{"type": "music", "target": "Megadeth", "response": "Ses dalgaları başlatılıyor."}}
]}}
"""

@app.route("/ses-test", methods=["POST"])
def process_audio():
    # 1. STT
    wav = io.BytesIO()
    with wave.open(wav, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000); w.writeframes(request.data)
    wav.seek(0)
    rec = sr.Recognizer()
    try: user_text = rec.recognize_google(rec.record(src=sr.AudioFile(wav)), language="tr-TR")
    except: user_text = ""

    out = bytearray()
    if not user_text:
        err = "Sinyal alınamadı."; out += pkt_audio(err, text_to_pcm(err)) + struct.pack("<B", 0xFF)
        data = bytes(out)
        return Response(data, mimetype="application/octet-stream",
                        headers={"Content-Length": str(len(data))})

    # STT Packet
    sb = user_text.encode("utf-8"); out += struct.pack("<BH", 0x01, len(sb)) + sb

    # 2. Gemini Multi-Intent (Sequential Sorting)
    try:
        resp = model.generate_content(intent_prompt(user_text, time.strftime("%H:%M"), "Konya"))
        raw_queue = json.loads(resp.text.strip().lstrip("```json").lstrip("```").rstrip("```").strip()).get("queue", [])
    except: raw_queue = [{"type": "chat", "response": "Veri hatası."}]

    # Priority Sort: Light(1) > Chat(2) > Weather(3) > Reminder(4) > Music(5)
    order = {"ambient_light": 1, "chat": 2, "weather": 3, "reminder": 4, "music": 5, "playlist": 5}
    queue = sorted(raw_queue, key=lambda x: order.get(x.get("type"), 9))

    for item in queue:
        itype = item.get("type", "chat")
        resp_text = (item.get("response") or "").strip()
        tgt = item.get("target", "")

        if itype == "ambient_light":
            try:
                hc = tgt.lstrip("#")
                rv, gv, bv = int(hc[0:2], 16), int(hc[2:4], 16), int(hc[4:6], 16)
                out += struct.pack("<BBBB", 0x04, rv, gv, bv)
            except: pass
            if resp_text: out += pkt_audio(resp_text, text_to_pcm(resp_text))

        elif itype == "chat":
            if resp_text: out += pkt_audio(resp_text, text_to_pcm(resp_text))

        elif itype == "weather":
            forecast = get_weather_analysis("Konya")
            analysis = model.generate_content(f"Teknik hava verileri: {forecast}. '{tgt}' için Matrix tarzında 1 kısa cümle özet ve giysi tavsiyesi ver.").text.strip()
            out += pkt_audio(analysis, text_to_pcm(analysis))

        elif itype == "music":
            if resp_text: out += pkt_audio(resp_text, text_to_pcm(resp_text))
            tb = tgt.encode("utf-8")
            out += struct.pack("<BB", 0x05, 1) + struct.pack("<H", len(tb)) + tb

    out += struct.pack("<B", 0xFF)
    data = bytes(out)
    return Response(data, mimetype="application/octet-stream",
                    headers={"Content-Length": str(len(data))})

@app.route("/tts", methods=["GET"])
def get_tts():
    text = request.args.get("q", "")
    return Response(text_to_pcm(f"Protokol uyarısı. {text}", vol_db=15), mimetype="application/octet-stream")

@app.route("/music", methods=["GET"])
def serve_music():
    query = request.args.get("q", "")
    with tempfile.TemporaryDirectory() as tmp:
        cmd = ["yt-dlp", "-x", "--audio-format", "best", "--force-ipv4", "-o", os.path.join(tmp, "a.%(ext)s"), f"ytsearch1:{query}"]
        subprocess.run(cmd, capture_output=True)
        found = [os.path.join(tmp, f) for f in os.listdir(tmp) if f.endswith((".mp3", ".webm", ".m4a"))]
        if not found: return "Yok", 404
        seg = AudioSegment.from_file(found[0]).set_frame_rate(16000).set_channels(1).set_sample_width(2)
        pcm = bytes((seg[:240000]-6).raw_data)
        return Response(struct.pack("<I", len(pcm)) + pcm, mimetype="application/octet-stream")

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)