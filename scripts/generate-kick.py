import numpy as np
import wave

# ------------------------------
# Parameters
# ------------------------------
duration = 60.0            # total duration in seconds
bpm = 120                  # beats per minute
beat_interval = 60 / bpm   # time interval between beats (seconds)
sample_rate = 44100        # samples per second (CD quality)
total_samples = int(duration * sample_rate)

# Create a zeroed audio array for the total duration
audio = np.zeros(total_samples)

# ------------------------------
# Kick Drum Synthesis Parameters
# ------------------------------
kick_duration = 0.2        # duration of each kick sound (in seconds)
kick_samples = int(kick_duration * sample_rate)
t = np.linspace(0, kick_duration, kick_samples, endpoint=False)

# Define a linear chirp for a simple pitch drop:
#   - Start frequency (f0) and end frequency (f1) define the tone over the duration.
#   - The instantaneous phase for a linear chirp is:
#         phase(t) = 2π * [f0 * t + ((f1-f0) / (2 * duration)) * t²]
f0 = 80                   # starting frequency in Hz
f1 = 40                   # ending frequency in Hz
phase = 2 * np.pi * (f0 * t + ((f1 - f0) / (2 * kick_duration)) * t**2)
kick_wave = np.sin(phase)

# Create an exponential decay envelope to shape the kick sound.
# Adjust the decay constant (here 0.05) to taste.
envelope = np.exp(-t / 0.05)
kick_sound = kick_wave * envelope

# ------------------------------
# Place the Kick Drum at 120 BPM
# ------------------------------
num_beats = int(duration / beat_interval)

for i in range(num_beats):
    start_sample = int(i * beat_interval * sample_rate)
    end_sample = start_sample + kick_samples
    if end_sample <= total_samples:
        audio[start_sample:end_sample] += kick_sound

# ------------------------------
# Normalization and WAV File Output
# ------------------------------
# Normalize to ensure maximum amplitude is within [-1, 1] to avoid clipping.
max_val = np.max(np.abs(audio))
if max_val > 0:
    audio = audio / max_val

# Convert the floating point data to 16-bit PCM format.
audio_int16 = np.int16(audio * 32767)

# Write the audio data to a WAV file.
output_filename = 'kick_drum.wav'
with wave.open(output_filename, 'wb') as wav_file:
    wav_file.setnchannels(1)         # mono audio
    wav_file.setsampwidth(2)           # 16-bit (2 bytes per sample)
    wav_file.setframerate(sample_rate) # sample rate
    wav_file.writeframes(audio_int16.tobytes())

print(f"Generated {output_filename}")
