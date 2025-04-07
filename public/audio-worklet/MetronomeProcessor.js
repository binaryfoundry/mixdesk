class MetronomeProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.nextBeatTime = 0;
    this.beatCount = 0;
    this.tempo = 120;
    this.isPlaying = false;
    this.lastBeatTime = 0;
    this.port.onmessage = (event) => {
      if (event.data.type === 'start') {
        this.isPlaying = true;
        this.nextBeatTime = currentTime;
        this.beatCount = 0;
      } else if (event.data.type === 'stop') {
        this.isPlaying = false;
      } else if (event.data.type === 'tempo') {
        this.tempo = event.data.tempo;
      }
    };
  }

  process(inputs, outputs, parameters) {
    if (!this.isPlaying) return true;

    const output = outputs[0];
    const secondsPerBeat = 60.0 / this.tempo;

    // Check if it's time for the next beat
    if (currentTime >= this.nextBeatTime) {
      // Create a click sound
      const clickDuration = 0.01; // 10ms
      const clickGain = 0.5;

      // Generate a click sound for each channel
      for (let channel = 0; channel < output.length; channel++) {
        const outputChannel = output[channel];
        const startSample = Math.floor((this.nextBeatTime - currentTime) * sampleRate);
        const endSample = Math.floor((this.nextBeatTime + clickDuration - currentTime) * sampleRate);

        for (let i = startSample; i < endSample && i < outputChannel.length; i++) {
          // Generate a click sound (simple sine wave)
          const t = (i - startSample) / sampleRate;
          const frequency = 1000; // 1kHz
          outputChannel[i] = Math.sin(2 * Math.PI * frequency * t) * clickGain * (1 - t / clickDuration);
        }
      }

      // Dispatch beat event
      this.port.postMessage({
        type: 'beat',
        beatNumber: this.beatCount + 1,
        time: this.nextBeatTime
      });

      // Update for next beat
      this.beatCount = (this.beatCount + 1) % 4;
      this.nextBeatTime += secondsPerBeat;
    }

    return true;
  }
}

registerProcessor('metronome-processor', MetronomeProcessor);