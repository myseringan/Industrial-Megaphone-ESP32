#!/usr/bin/env python3
"""
Megaphone Player Client (swagger API compatible)

Usage: 
  python send_audio.py output.wav                      - upload and play
  python send_audio.py check "message text" "hash"    - check if exists
"""

import sys
import subprocess
import tempfile
import os
import hashlib

try:
    import requests
except ImportError:
    print("Installing requests...")
    subprocess.run([sys.executable, "-m", "pip", "install", "requests"])
    import requests

# Change this to your ESP32 IP
ESP_IP = "172.33.11.147"
ESP_PORT = 1820
BASE_URL = f"http://{ESP_IP}:{ESP_PORT}"

def convert_to_pcm(wav_file):
    """Convert WAV/MP3 to raw PCM (44100Hz, stereo, 16-bit)"""
    pcm_file = tempfile.mktemp(suffix=".raw")
    cmd = [
        "ffmpeg", "-i", wav_file,
        "-f", "s16le", "-acodec", "pcm_s16le",
        "-ar", "44100", "-ac", "2",
        pcm_file, "-y"
    ]
    result = subprocess.run(cmd, capture_output=True)
    if result.returncode != 0:
        print(f"FFmpeg error: {result.stderr.decode()}")
        return None
    return pcm_file

def get_file_hash(filepath):
    """Calculate MD5 hash of file"""
    with open(filepath, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()

def check_exists(message_text, audio_hash):
    """Check if audio exists on device"""
    try:
        r = requests.post(
            f"{BASE_URL}/check-audio",
            json={"message_text": message_text, "audio_hash": audio_hash},
            timeout=5
        )
        return r.json().get("exists", False)
    except:
        return False

def main():
    if len(sys.argv) < 2:
        print("Megaphone Player Client")
        print("Usage:")
        print("  python send_audio.py <wav_file>                    - upload and play")
        print("  python send_audio.py check <message_text> <hash>   - check if exists")
        sys.exit(1)
    
    # Check health
    print(f"Checking {BASE_URL}/health ...")
    try:
        r = requests.get(f"{BASE_URL}/health", timeout=5)
        health = r.json()
        print(f"Health: {health}")
    except Exception as e:
        print(f"Error connecting to ESP32: {e}")
        sys.exit(1)
    
    # Check command
    if sys.argv[1] == "check" and len(sys.argv) >= 4:
        message_text = sys.argv[2]
        audio_hash = sys.argv[3]
        exists = check_exists(message_text, audio_hash)
        print(f"'{message_text}' (hash={audio_hash}): {'EXISTS' if exists else 'NOT FOUND'}")
        return
    
    wav_file = sys.argv[1]
    
    if not os.path.exists(wav_file):
        print(f"File not found: {wav_file}")
        sys.exit(1)
    
    # Get message_text from filename and calculate hash
    message_text = os.path.splitext(os.path.basename(wav_file))[0]
    audio_hash = get_file_hash(wav_file)
    
    print(f"File: {wav_file}")
    print(f"Message text: {message_text}")
    print(f"Audio hash: {audio_hash}")
    
    # Check if already exists
    print("Checking if audio exists on device...")
    if check_exists(message_text, audio_hash):
        print("Audio already exists on device - skipping upload!")
        print("Playing stored audio...")
        r = requests.post(
            f"{BASE_URL}/play-message",
            json={"message_text": message_text, "audio_hash": audio_hash},
            timeout=30
        )
        print(f"Play response: {r.status_code}")
        return
    
    # Convert to PCM
    print(f"Converting to PCM...")
    pcm_file = convert_to_pcm(wav_file)
    if not pcm_file:
        print("Conversion failed!")
        sys.exit(1)
    
    # Read PCM bytes
    with open(pcm_file, "rb") as f:
        pcm_data = f.read()
    
    print(f"PCM size: {len(pcm_data)} bytes ({len(pcm_data)//1024} KB)")
    
    # Upload to device via /update-audio
    print("Uploading to device...")
    try:
        r = requests.post(
            f"{BASE_URL}/update-audio",
            data=pcm_data,
            headers={
                "Content-Type": "application/octet-stream",
                "X-Message-Text": message_text,
                "X-Audio-Hash": audio_hash
            },
            timeout=120
        )
        print(f"Upload response: {r.status_code}")
        
        if r.status_code != 200:
            print(f"Upload failed: {r.text}")
            os.remove(pcm_file)
            sys.exit(1)
    except Exception as e:
        print(f"Upload error: {e}")
        os.remove(pcm_file)
        sys.exit(1)
    
    # Play it via /play-message
    print("Playing...")
    try:
        r = requests.post(
            f"{BASE_URL}/play-message",
            json={"message_text": message_text, "audio_hash": audio_hash},
            timeout=30
        )
        print(f"Play response: {r.status_code}")
    except Exception as e:
        print(f"Play error: {e}")
    
    # Cleanup
    os.remove(pcm_file)
    print("Done!")

if __name__ == "__main__":
    main()
