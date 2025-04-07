import aubio from 'aubiojs';

export interface BeatDetectionResult {
  beatTimes: number[];
  phrases: { startTime: number; endTime: number }[];
  bpm: number;
  downbeatOffset: number;
}

export async function detectBeats(buffer: AudioBuffer): Promise<BeatDetectionResult> {
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
  const data = renderedBuffer.getChannelData(0);

  // Use aubio.js for beat detection with optimized parameters
  const { Tempo } = await aubio();
  const tempo = new Tempo(2048, 512, sampleRate);  // Larger buffer size for better accuracy
  let detectedBeats: { time: number; confidence: number; }[] = [];
  let totalFrames = 0;

  // Create a buffer for processing
  const hopSize = 512;
  const bufferSize = 2048;

  // Process in chunks with yield to UI
  const CHUNK_SIZE = 10000; // Process 10000 samples at a time
  const processChunk = async (startIndex: number): Promise<void> => {
    return new Promise(resolve => {
      setTimeout(() => {
        const endIndex = Math.min(startIndex + CHUNK_SIZE, data.length - bufferSize);
        const processBuffer = new Float32Array(bufferSize);

        for (let i = startIndex; i < endIndex; i += hopSize) {
          // Copy data into process buffer
          for (let j = 0; j < bufferSize; j++) {
            processBuffer[j] = data[i + j];
          }

          // Process this frame
          const confidence = tempo.do(processBuffer);
          if (confidence !== 0) {
            const beatTimeMs = (totalFrames / sampleRate) * 1000;
            detectedBeats.push({
              time: Math.round(beatTimeMs),
              confidence: confidence
            });
          }
          totalFrames += hopSize;
        }

        resolve();
      }, 0);
    });
  };

  // Process all chunks
  for (let i = 0; i < data.length - bufferSize; i += CHUNK_SIZE) {
    await processChunk(i);
  }

  const bpm = tempo.getBpm();
  console.log('Detected BPM:', bpm);

  // Adjust BPM to standard range
  let adjustedBpm = bpm;
  if (adjustedBpm < 90) adjustedBpm *= 2;
  if (adjustedBpm > 180) adjustedBpm /= 2;

  // Fit regular grid based on confidence-weighted beats
  const beatInterval = (60000 / adjustedBpm); // ms between beats at the adjusted BPM
  const durationMs = (buffer.duration * 1000);
  const numBeats = Math.floor(durationMs / beatInterval);

  // Try different offsets within one beat interval to find best alignment
  const numTestPoints = 20;
  let gridOffset = 0;
  let bestScore = -Infinity;

  // Process grid alignment in chunks
  const GRID_CHUNK_SIZE = 5; // Process 5 test points at a time
  for (let i = 0; i < numTestPoints; i += GRID_CHUNK_SIZE) {
    await new Promise(resolve => setTimeout(resolve, 0));

    for (let j = i; j < Math.min(i + GRID_CHUNK_SIZE, numTestPoints); j++) {
      const testOffset = (beatInterval * j) / numTestPoints;
      let score = 0;

      // For each potential grid point, find nearby detected beats and score based on confidence
      for (let beatIndex = 0; beatIndex < numBeats; beatIndex++) {
        const gridTime = testOffset + (beatIndex * beatInterval);

        // Find detected beats within 100ms of this grid point
        const nearbyBeats = detectedBeats.filter(beat =>
          Math.abs(beat.time - gridTime) < 100
        );

        // Score based on confidence and distance
        for (const beat of nearbyBeats) {
          const distance = Math.abs(beat.time - gridTime);
          const distanceWeight = 1 - (distance / 100); // Linear falloff with distance
          score += beat.confidence * distanceWeight;
        }
      }

      if (score > bestScore) {
        bestScore = score;
        gridOffset = testOffset;
      }
    }
  }

  // Generate final regular grid with optimal offset
  const beatTimes: number[] = [];
  for (let i = 0; i < numBeats; i++) {
    const beatTime = Math.round(gridOffset + (i * beatInterval));
    if (beatTime < durationMs) { // Only add beats within audio duration
      beatTimes.push(beatTime);
    }
  }

  // === Estimate Downbeat Offset using confidence scores ===
  const getOffsetScore = (offset: number) => {
    let score = 0;
    let totalConfidence = 0;
    let patternScore = 0;
    let consistencyScore = 0;

    // Look at groups of 4 beats starting at the offset
    for (let i = offset; i < beatTimes.length - 4; i += 4) {
      const barBeats: number[] = [];
      const barConfidences: number[] = [];

      // Analyze all 4 beats in this bar
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

      // Score based on common rhythm patterns (1-2-3-4 emphasis)
      const commonPatterns = [
        [1.0, 0.5, 0.7, 0.5],  // Standard 4/4
        [1.0, 0.4, 0.8, 0.4],  // Common rock/pop
        [1.0, 0.3, 0.6, 0.3],  // Heavy downbeat
      ];

      // Normalize bar beats for pattern matching
      const maxBeat = Math.max(...barBeats);
      if (maxBeat > 0) {
        const normalizedBeats = barBeats.map(b => b / maxBeat);

        // Find best matching pattern
        for (const pattern of commonPatterns) {
          let patternMatch = 0;
          for (let j = 0; j < 4; j++) {
            patternMatch += 1 - Math.abs(normalizedBeats[j] - pattern[j]);
          }
          patternScore += patternMatch;
        }
      }

      // Score based on downbeat strength
      const downbeatStrength = barBeats[0];
      const otherBeatsAvg = (barBeats[1] + barBeats[2] + barBeats[3]) / 3;
      if (downbeatStrength > otherBeatsAvg) {
        score += (downbeatStrength - otherBeatsAvg) * 2;
      }

      // Score based on confidence consistency
      const avgConfidence = barConfidences.reduce((a, b) => a + b, 0) / 4;
      if (barConfidences[0] > avgConfidence) {
        consistencyScore += barConfidences[0] - avgConfidence;
      }

      totalConfidence += barConfidences[0];
    }

    // Combine all scoring factors with weights
    const finalScore = score * 0.4 +
                      patternScore * 0.3 +
                      consistencyScore * 0.2 +
                      totalConfidence * 0.1;

    return finalScore;
  };

  // Cache offset scores
  const offsets = [0, 1, 2, 3];
  const offsetScores = [];
  const OFFSET_CHUNK_SIZE = 2;

  for (let i = 0; i < offsets.length; i += OFFSET_CHUNK_SIZE) {
    await new Promise(resolve => setTimeout(resolve, 0));

    for (let j = i; j < Math.min(i + OFFSET_CHUNK_SIZE, offsets.length); j++) {
      offsetScores.push({
        offset: offsets[j],
        score: getOffsetScore(offsets[j])
      });
    }
  }

  // Sort by score and get top candidates
  const sortedOffsets = offsetScores
    .sort((a, b) => b.score - a.score)
    .slice(0, 2);

  // If top two scores are close, use additional factors to break tie
  const bestOffset = sortedOffsets[0].score > sortedOffsets[1].score * 1.2
    ? sortedOffsets[0].offset
    : offsets.reduce((best, current) => {
        // Additional tiebreaker: check consistency across larger phrases
        const checkLargerPhrase = (offset: number) => {
          let score = 0;
          // Check 8-beat and 16-beat patterns
          for (let i = offset; i < beatTimes.length - 16; i += 8) {
            const beatTime = beatTimes[i];
            const nearbyBeats = detectedBeats.filter(beat =>
              Math.abs(beat.time - beatTime) < 100
            );
            score += nearbyBeats.reduce((sum, beat) => sum + beat.confidence, 0);
          }
          return score;
        };

        return checkLargerPhrase(current) > checkLargerPhrase(best)
          ? current
          : best;
      }, sortedOffsets[0].offset);

  console.log('Best downbeat offset:', bestOffset, 'Scores:', offsetScores);

  // Group beats into musical phrases based on bars and energy patterns
  const phrases: {startTime: number, endTime: number}[] = [];
  const beatsPerBar = 4;
  const barsPerPhrase = 8; // Standard 8-bar phrases
  const minBarsForPhrase = 4; // Minimum bars to consider a phrase

  // Calculate energy/confidence for each bar
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
        // Sum confidence values for this beat
        barEnergy += nearbyBeats.reduce((sum, beat) => sum + beat.confidence, 0);
      }
      barEnergies.push(barEnergy);
    }
  }

  // Detect significant changes in energy to identify phrase boundaries
  const energyThreshold = Math.max(...barEnergies) * 0.6; // 60% of max energy
  let phraseStartBar = 0;

  for (let bar = 1; bar < barEnergies.length; bar++) {
    const isSignificantChange =
      Math.abs(barEnergies[bar] - barEnergies[bar - 1]) > energyThreshold ||
      bar - phraseStartBar >= barsPerPhrase;

    // Check if we've found a phrase boundary
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

  // Add final phrase if there are enough remaining bars
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

  return { beatTimes, phrases, bpm: adjustedBpm, downbeatOffset: bestOffset };
}