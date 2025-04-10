import aubio from 'aubiojs';

export interface BeatDetectionResult {
  beatTimes: number[];
  phrases: { startTime: number; endTime: number }[];
  bpm: number;
  downbeatOffset: number;
}

interface DetectedBeat {
  time: number;
  confidence: number;
}

interface OffsetScore {
  offset: number;
  score: number;
}

async function createFilteredBuffer(buffer: AudioBuffer): Promise<Float32Array> {
  const sampleRate = buffer.sampleRate;
  const numSamples = buffer.length;

  // Create offline context for analysis
  const offlineCtx = new OfflineAudioContext(1, numSamples, sampleRate);
  const source = offlineCtx.createBufferSource();
  source.buffer = buffer;

  // Create a more sophisticated filter chain for breakbeat detection
  const highpass = offlineCtx.createBiquadFilter();
  highpass.type = 'highpass';
  highpass.frequency.value = 30;  // Lower cutoff to catch more bass frequencies
  highpass.Q.value = 0.7;

  const lowpass = offlineCtx.createBiquadFilter();
  lowpass.type = 'lowpass';
  lowpass.frequency.value = 200;  // Higher cutoff to include more mid frequencies
  lowpass.Q.value = 0.7;

  // Add a peak filter for snare detection
  const peak = offlineCtx.createBiquadFilter();
  peak.type = 'peaking';
  peak.frequency.value = 150;  // Center frequency for snare
  peak.gain.value = 10;
  peak.Q.value = 2;

  // Connect the filter chain
  source.connect(highpass);
  highpass.connect(lowpass);
  lowpass.connect(peak);
  peak.connect(offlineCtx.destination);
  source.start(0);

  // Render the filtered audio
  const renderedBuffer = await offlineCtx.startRendering();
  return renderedBuffer.getChannelData(0);
}

async function detectRawBeats(
  data: Float32Array,
  sampleRate: number
): Promise<{ detectedBeats: DetectedBeat[]; bpm: number }> {
  const { Tempo } = await aubio();
  const tempo = new Tempo(2048, 512, sampleRate);
  const detectedBeats: DetectedBeat[] = [];

  const hopSize = 512;
  const bufferSize = 2048;
  const CHUNK_SIZE = 10000;

  // Process audio in chunks to avoid blocking the main thread
  for (let chunkStart = 0; chunkStart < data.length - bufferSize; chunkStart += CHUNK_SIZE) {
    await new Promise(resolve => setTimeout(resolve, 0)); // Yield to main thread

    const chunkEnd = Math.min(chunkStart + CHUNK_SIZE, data.length - bufferSize);
    const processBuffer = new Float32Array(bufferSize);

    // Process each hop within the current chunk
    for (let i = chunkStart; i < chunkEnd; i += hopSize) {
      // Copy audio data into processing buffer
      for (let j = 0; j < bufferSize; j++) {
        processBuffer[j] = data[i + j];
      }

      // Detect beat and calculate confidence
      const confidence = tempo.do(processBuffer);
      
      // Only keep beats with sufficient confidence
      if (confidence > 0.0) {
        const beatTimeMs = (i / sampleRate) * 1000;
        detectedBeats.push({
          time: beatTimeMs,
          confidence
        });
      }
    }
  }

  // Merge very close beats (less than 200ms apart)
  const filteredBeats: DetectedBeat[] = [];
  for (let i = 0; i < detectedBeats.length; i++) {
    const current = detectedBeats[i];
    if (i === 0 || current.time - detectedBeats[i - 1].time > 200) {
      filteredBeats.push(current);
    }
  }
  return {
    detectedBeats: filteredBeats,
    bpm: tempo.getBpm()
  };
}

async function findOptimalGridOffset(
  detectedBeats: DetectedBeat[],
  adjustedBpm: number,
  durationMs: number
): Promise<{ gridOffset: number; beatTimes: number[] }> {
  const beatInterval = (60000 / adjustedBpm);
  const numBeats = Math.floor(durationMs / beatInterval);
  const numTestPoints = 20;
  let gridOffset = 0;
  let bestScore = -Infinity;

  const GRID_CHUNK_SIZE = 5;
  for (let i = 0; i < numTestPoints; i += GRID_CHUNK_SIZE) {
    await new Promise(resolve => setTimeout(resolve, 0));
    
    for (let j = i; j < Math.min(i + GRID_CHUNK_SIZE, numTestPoints); j++) {
      const testOffset = (beatInterval * j) / numTestPoints;
      let score = 0;

      for (let beatIndex = 0; beatIndex < numBeats; beatIndex++) {
        const gridTime = testOffset + (beatIndex * beatInterval);
        const nearbyBeats = detectedBeats.filter(beat =>
          Math.abs(beat.time - gridTime) < 100
        );

        for (const beat of nearbyBeats) {
          const distance = Math.abs(beat.time - gridTime);
          const distanceWeight = 1 - (distance / 100);
          score += beat.confidence * distanceWeight;
        }
      }

      if (score > bestScore) {
        bestScore = score;
        gridOffset = testOffset;
      }
    }
  }

  const beatTimes: number[] = [];
  for (let i = 0; i < numBeats; i++) {
    const beatTime = Math.round(gridOffset + (i * beatInterval));
    if (beatTime < durationMs) {
      beatTimes.push(beatTime);
    }
  }

  return { gridOffset, beatTimes };
}

async function findBestDownbeatOffset(
    beatTimes: number[],
    detectedBeats: DetectedBeat[]
  ): Promise<number> {
    const offsets = [0, 1, 2, 3];
    const scores: { offset: number; score: number }[] = [];
    const beatsPerBar = 4;

    // Helper to compute beat strengths
    const getStrength = (time: number): number => {
      const nearby = detectedBeats.filter(b => Math.abs(b.time - time) < 100);
      return nearby.reduce((sum, b) => sum + b.confidence * (1 - Math.abs(b.time - time) / 100), 0);
    };

    // For each offset (0–3), evaluate based on bar accent patterns
    for (const offset of offsets) {
      let totalScore = 0;
      let count = 0;

      for (let i = offset; i + 3 < beatTimes.length; i += beatsPerBar) {
        const strengths = [
          getStrength(beatTimes[i]),     // beat 1
          getStrength(beatTimes[i + 1]), // beat 2
          getStrength(beatTimes[i + 2]), // beat 3
          getStrength(beatTimes[i + 3])  // beat 4
        ];

        const total = strengths.reduce((a, b) => a + b, 0);
        if (total === 0) continue;

        const norm = strengths.map(s => s / total);

        // Expect pattern: strong (1), weak (2), medium (3), weak (4)
        const expected = [1.0, 0.4, 0.7, 0.4];

        let patternMatch = 0;
        for (let j = 0; j < 4; j++) {
          patternMatch += 1 - Math.abs(norm[j] - expected[j]);
        }

        totalScore += patternMatch;
        count++;
      }

      const averageScore = count > 0 ? totalScore / count : 0;

      // Add small bias toward offset = 0 (common case)
      const bias = offset === 0 ? 0.1 : 0;

      scores.push({ offset, score: averageScore + bias });
    }

    scores.sort((a, b) => b.score - a.score);
    return scores[0].offset;
}

async function detectPhrases(
  beatTimes: number[],
  detectedBeats: DetectedBeat[],
  bestOffset: number
): Promise<{ startTime: number; endTime: number }[]> {
  const phrases: { startTime: number; endTime: number }[] = [];
  const beatsPerBar = 4;
  const barsPerPhrase = 8;
  const minBarsForPhrase = 4;
  const barEnergies: number[] = [];
  const BAR_CHUNK_SIZE = 10;

  for (let i = bestOffset; i < beatTimes.length - beatsPerBar; i += beatsPerBar * BAR_CHUNK_SIZE) {
    await new Promise(resolve => setTimeout(resolve, 0));
    
    for (let j = i; j < Math.min(i + beatsPerBar * BAR_CHUNK_SIZE, beatTimes.length - beatsPerBar); j += beatsPerBar) {
      let barEnergy = 0;
      for (let k = 0; k < beatsPerBar; k++) {
        const beatTime = beatTimes[j + k];
        const nearbyBeats = detectedBeats.filter(beat =>
          Math.abs(beat.time - beatTime) < 100
        );
        barEnergy += nearbyBeats.reduce((sum, beat) => sum + beat.confidence, 0);
      }
      barEnergies.push(barEnergy);
    }
  }

  const energyThreshold = Math.max(...barEnergies) * 0.6;
  let phraseStartBar = 0;

  for (let bar = 1; bar < barEnergies.length; bar++) {
    const isSignificantChange =
      Math.abs(barEnergies[bar] - barEnergies[bar - 1]) > energyThreshold ||
      bar - phraseStartBar >= barsPerPhrase;

    if (isSignificantChange && bar - phraseStartBar >= minBarsForPhrase) {
      const startBeat = phraseStartBar * beatsPerBar + bestOffset;
      const endBeat = bar * beatsPerBar + bestOffset - 1;

      if (startBeat < beatTimes.length && endBeat < beatTimes.length) {
        phrases.push({
          startTime: beatTimes[startBeat],
          endTime: beatTimes[endBeat]
        });
      }
      phraseStartBar = bar;
    }
  }

  const remainingBars = barEnergies.length - phraseStartBar;
  if (remainingBars >= minBarsForPhrase) {
    const startBeat = phraseStartBar * beatsPerBar + bestOffset;
    const endBeat = Math.min(
      beatTimes.length - 1,
      (phraseStartBar + remainingBars) * beatsPerBar + bestOffset - 1
    );

    phrases.push({
      startTime: beatTimes[startBeat],
      endTime: beatTimes[endBeat]
    });
  }

  return phrases;
}

export async function detectBeats(buffer: AudioBuffer): Promise<BeatDetectionResult> {
  // Step 1: Create filtered buffer for analysis
  const filteredData = await createFilteredBuffer(buffer);

  // Step 2: Detect raw beats and get initial BPM
  const { detectedBeats, bpm } = await detectRawBeats(filteredData, buffer.sampleRate);
  console.log('Detected BPM:', bpm);

  // Step 3: Adjust BPM to standard range
  let adjustedBpm = bpm;
  if (adjustedBpm < 90) adjustedBpm *= 2;
  if (adjustedBpm > 180) adjustedBpm /= 2;

  // Step 4: Find optimal grid offset and generate beat times
  const { beatTimes } = await findOptimalGridOffset(
    detectedBeats,
    adjustedBpm,
    buffer.duration * 1000
  );

  // Step 5: Find best downbeat offset
  const bestOffset = await findBestDownbeatOffset(beatTimes, detectedBeats);
  console.log('Best downbeat offset:', bestOffset);

  // Step 6: Detect musical phrases
  const phrases = await detectPhrases(beatTimes, detectedBeats, bestOffset);

  return { beatTimes, phrases, bpm: adjustedBpm, downbeatOffset: bestOffset };
}