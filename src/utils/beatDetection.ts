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

async function detectRawBeats(data: Float32Array, sampleRate: number): Promise<{ detectedBeats: DetectedBeat[]; bpm: number }> {
  const { Tempo } = await aubio();
  const tempo = new Tempo(2048, 512, sampleRate);
  let detectedBeats: DetectedBeat[] = [];
  let totalFrames = 0;

  const hopSize = 512;
  const bufferSize = 2048;
  const CHUNK_SIZE = 10000;

  const processChunk = async (startIndex: number): Promise<void> => {
    return new Promise(resolve => {
      setTimeout(() => {
        const endIndex = Math.min(startIndex + CHUNK_SIZE, data.length - bufferSize);
        const processBuffer = new Float32Array(bufferSize);

        for (let i = startIndex; i < endIndex; i += hopSize) {
          for (let j = 0; j < bufferSize; j++) {
            processBuffer[j] = data[i + j];
          }

          const confidence = tempo.do(processBuffer);
          if (confidence !== 0) {
            const beatTimeMs = (totalFrames / sampleRate) * 1000;
            detectedBeats.push({
              time: Math.round(beatTimeMs),
              confidence
            });
          }
          totalFrames += hopSize;
        }
        resolve();
      }, 0);
    });
  };

  for (let i = 0; i < data.length - bufferSize; i += CHUNK_SIZE) {
    await processChunk(i);
  }

  return { detectedBeats, bpm: tempo.getBpm() };
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

function getOffsetScore(
  offset: number,
  beatTimes: number[],
  detectedBeats: DetectedBeat[]
): number {
  let score = 0;
  let totalConfidence = 0;
  let patternScore = 0;
  let consistencyScore = 0;

  for (let i = offset; i < beatTimes.length - 4; i += 4) {
    const barBeats: number[] = [];
    const barConfidences: number[] = [];

    for (let j = 0; j < 4; j++) {
      const gridTime = beatTimes[i + j];
      const nearbyBeats = detectedBeats.filter(beat =>
        Math.abs(beat.time - gridTime) < 100
      );

      let beatScore = 0;
      let maxConfidence = 0;

      for (const beat of nearbyBeats) {
        const distance = Math.abs(beat.time - gridTime);
        const distanceWeight = 1 - (distance / 100);
        const weightedConfidence = beat.confidence * distanceWeight;
        beatScore += weightedConfidence;
        maxConfidence = Math.max(maxConfidence, beat.confidence);
      }

      barBeats.push(beatScore);
      barConfidences.push(maxConfidence);
    }

    const commonPatterns = [
      [1.0, 0.5, 0.7, 0.5],
      [1.0, 0.4, 0.8, 0.4],
      [1.0, 0.3, 0.6, 0.3],
    ];

    const maxBeat = Math.max(...barBeats);
    if (maxBeat > 0) {
      const normalizedBeats = barBeats.map(b => b / maxBeat);

      for (const pattern of commonPatterns) {
        let patternMatch = 0;
        for (let j = 0; j < 4; j++) {
          patternMatch += 1 - Math.abs(normalizedBeats[j] - pattern[j]);
        }
        patternScore += patternMatch;
      }
    }

    const downbeatStrength = barBeats[0];
    const otherBeatsAvg = (barBeats[1] + barBeats[2] + barBeats[3]) / 3;
    if (downbeatStrength > otherBeatsAvg) {
      score += (downbeatStrength - otherBeatsAvg) * 2;
    }

    const avgConfidence = barConfidences.reduce((a, b) => a + b, 0) / 4;
    if (barConfidences[0] > avgConfidence) {
      consistencyScore += barConfidences[0] - avgConfidence;
    }

    totalConfidence += barConfidences[0];
  }

  return score * 0.4 + patternScore * 0.3 + consistencyScore * 0.2 + totalConfidence * 0.1;
}

async function findBestDownbeatOffset(
  beatTimes: number[],
  detectedBeats: DetectedBeat[]
): Promise<number> {
  const offsets = [0, 1, 2, 3];
  const offsetScores: OffsetScore[] = [];
  const OFFSET_CHUNK_SIZE = 2;

  for (let i = 0; i < offsets.length; i += OFFSET_CHUNK_SIZE) {
    await new Promise(resolve => setTimeout(resolve, 0));
    
    for (let j = i; j < Math.min(i + OFFSET_CHUNK_SIZE, offsets.length); j++) {
      offsetScores.push({
        offset: offsets[j],
        score: getOffsetScore(offsets[j], beatTimes, detectedBeats)
      });
    }
  }

  const sortedOffsets = offsetScores
    .sort((a, b) => b.score - a.score)
    .slice(0, 2);

  if (sortedOffsets[0].score > sortedOffsets[1].score * 1.2) {
    return sortedOffsets[0].offset;
  }

  const checkLargerPhrase = (offset: number): number => {
    let score = 0;
    for (let i = offset; i < beatTimes.length - 16; i += 8) {
      const beatTime = beatTimes[i];
      const nearbyBeats = detectedBeats.filter(beat =>
        Math.abs(beat.time - beatTime) < 100
      );
      score += nearbyBeats.reduce((sum, beat) => sum + beat.confidence, 0);
    }
    return score;
  };

  return offsets.reduce((best, current) =>
    checkLargerPhrase(current) > checkLargerPhrase(best) ? current : best,
    sortedOffsets[0].offset
  );
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