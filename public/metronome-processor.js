// metronome-processor.js

class MetronomeProcessor extends AudioWorkletProcessor {
  // Define the "tempo" parameter with a-rate automation for precise timing.
  static get parameterDescriptors() {
    return [{
      name: 'tempo',
      defaultValue: 120,
      minValue: 20,
      maxValue: 300,
      automationRate: 'a-rate'
    }];
  }

  constructor(options) {
    super();
    // Initialize the counter for samples until the next tick.
    // This uses the default tempo of 120 BPM.
    this.nextTickSamples = sampleRate * (60 / 120);
    this.beatCount = 0;
  }

  process(inputs, outputs, parameters) {
    // Output silent audio to keep the worklet alive.
    const output = outputs[0];
    if (output && output.length > 0) {
      for (let channel = 0; channel < output.length; channel++) {
        output[channel].fill(0);
      }
    }

    // Retrieve the tempo parameter array.
    // If no automation is applied, it will be an array with one value.
    const tempoValues = parameters['tempo'];
    // Determine the block size (typically 128 frames per call).
    const blockSize = output[0] ? output[0].length : 128;

    // Process the block sample-by-sample.
    for (let i = 0; i < blockSize; i++) {
      // Count down one sample.
      this.nextTickSamples -= 1;
      // When the counter reaches zero, fire a tick event.
      if (this.nextTickSamples <= 0) {
        this.port.postMessage({ type: 'tick', beatCount: this.beatCount || 0 });
        this.beatCount = (this.beatCount || 0) + 1;
        // If the tempo array length is 1, use the single value; otherwise, use the current sample value.
        const currentTempo = tempoValues.length > 1 ? tempoValues[i] : tempoValues[0];
        // Calculate the interval in samples until the next tick.
        const samplesPerTick = sampleRate * (60 / currentTempo);
        // Add the interval to the counter, compensating for any overrun.
        this.nextTickSamples += samplesPerTick;
      }
    }
    // Returning true keeps the processor active.
    return true;
  }
}

registerProcessor('metronome-processor', MetronomeProcessor);
