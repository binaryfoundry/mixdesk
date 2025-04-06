import { useState, useRef, useEffect } from 'react';
import * as mm from 'music-metadata';
import aubio from 'aubiojs';
import SignalsmithStretch from 'signalsmith-stretch';

interface TrackMetadata {
  title: string;
  key: string;
  bpm: number;
}

export interface Track {
  id: string;
  file: File;
  metadata: TrackMetadata;
  audioContext: AudioContext;
  audioBuffer: AudioBuffer | null;
  sourceNode: AudioBufferSourceNode | null;
  gainNode: GainNode | null;
  stretchNode: any | null;
  beats: number[];
  phrases: { startTime: number, endTime: number }[];
  isPlaying: boolean;
  currentTime: number;
  duration: number;
  volume: number;
  tempo: number;
  originalTempo: number;
  downbeatOffset: number;
}

export function useAudioPlayer() {
  const [tracks, setTracks] = useState<Track[]>([]);
  const [activeTrackId, setActiveTrackId] = useState<string | null>(null);
  const [globalTempo, setGlobalTempo] = useState(120);
  const timeUpdateIntervalRef = useRef<number | null>(null);

  // Helper function to update a specific track
  const updateTrack = (trackId: string, updates: Partial<Track>) => {
    setTracks(prevTracks => 
      prevTracks.map(track => 
        track.id === trackId 
          ? { ...track, ...updates }
          : track
      )
    );
  };

  // Update current time while playing
  useEffect(() => {
    if (activeTrackId) {
      const track = tracks.find(t => t.id === activeTrackId);
      if (track?.isPlaying && track.sourceNode) {
        const startTime = track.audioContext.currentTime || 0;
        const startOffset = track.currentTime;
        
        timeUpdateIntervalRef.current = window.setInterval(() => {
          const elapsed = (track.audioContext.currentTime || 0) - startTime;
          const newTime = startOffset + elapsed;
          
          if (isFinite(newTime) && newTime >= 0 && newTime <= track.duration) {
            updateTrack(activeTrackId, { currentTime: newTime });
          }
        }, 100);
      } else if (timeUpdateIntervalRef.current) {
        window.clearInterval(timeUpdateIntervalRef.current);
      }
    }
  }, [activeTrackId, tracks]);

  const initAudio = async () => {
    try {
      const audioContext = new (window.AudioContext || (window as any).webkitAudioContext)();
      const gainNode = audioContext.createGain();
      gainNode.connect(audioContext.destination);
      return { audioContext, gainNode };
    } catch (error) {
      console.error('Error initializing audio:', error);
      return null;
    }
  };

  const readMetadata = async (file: File): Promise<TrackMetadata> => {
    try {
      const arrayBuffer = await file.arrayBuffer();
      const buffer = new Uint8Array(arrayBuffer);
      const metadata = await mm.parseBuffer(buffer, file.type);
      
      const title = metadata.common.title || file.name;
      const key = metadata.common.key || 'Unknown';
      const bpm = metadata.common.bpm || 0;
      
      return {
        title,
        key,
        bpm
      };
    } catch (error) {
      console.error('Error reading metadata:', error);
      return {
        title: file.name,
        key: 'Unknown',
        bpm: 0
      };
    }
  };

  const handleFileUpload = async (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (file) {
      console.log('Loading audio file:', file.name);
      
      const audioSetup = await initAudio();
      if (!audioSetup) {
        console.error('Failed to initialize audio context');
        return;
      }
      
      const trackMetadata = await readMetadata(file);
      
      const arrayBuffer = await file.arrayBuffer();
      const audioBuffer = await audioSetup.audioContext.decodeAudioData(arrayBuffer);
      if (audioBuffer) {
        const sourceNode = audioSetup.audioContext.createBufferSource();
        if (sourceNode) {
          sourceNode.buffer = audioBuffer;
          
          const newTrack: Track = {
            id: crypto.randomUUID(),
            file,
            metadata: trackMetadata,
            audioContext: audioSetup.audioContext,
            audioBuffer: audioBuffer,
            sourceNode,
            gainNode: audioSetup.gainNode,
            stretchNode: null,
            beats: [],
            phrases: [],
            isPlaying: false,
            currentTime: 0,
            duration: audioBuffer.duration,
            volume: 1,
            tempo: 120,
            originalTempo: trackMetadata.bpm || 120,
            downbeatOffset: 0
          };

          if (newTrack.gainNode) {
            newTrack.gainNode.gain.value = newTrack.volume;
          }

          detectBeats(audioBuffer).then(({ beatTimes, phrases, bpm, downbeatOffset }) => {
            updateTrack(newTrack.id, {
              originalTempo: bpm,
              beats: beatTimes,
              phrases,
              downbeatOffset
            });
          });

          setTracks(prevTracks => [...prevTracks, newTrack]);
        }
      }
    }
  };

  const handlePlayPause = async (trackId: string) => {
    const track = tracks.find(t => t.id === trackId);
    if (!track) return;

    try {
      if (track.audioContext.state === 'suspended') {
        await track.audioContext.resume();
      }

      if (track.isPlaying) {
        track.sourceNode?.stop();
        if (timeUpdateIntervalRef.current) {
          window.clearInterval(timeUpdateIntervalRef.current);
        }
      } else {
        const validCurrentTime = isFinite(track.currentTime) ? track.currentTime : 0;
        
        const arrayBuffer = await track.file.arrayBuffer();
        const audioBuffer = await track.audioContext.decodeAudioData(arrayBuffer);
        const sourceNode = track.audioContext.createBufferSource();
        
        if (sourceNode) {
          sourceNode.buffer = audioBuffer;
          const stretchNode = await SignalsmithStretch(track.audioContext);
          const semitones = -12 * Math.log2(globalTempo / track.originalTempo);
          stretchNode.schedule({ rate: 1.0, semitones: semitones });
          stretchNode.start();

          sourceNode.connect(stretchNode);
          if (track.gainNode) {
            stretchNode.connect(track.gainNode);
          }
          
          if (track.gainNode) {
            track.gainNode.gain.value = track.volume;
          }

          const rate = globalTempo / track.originalTempo;
          sourceNode.playbackRate.value = rate;
          sourceNode.start(0, validCurrentTime);
          
          updateTrack(trackId, {
            sourceNode,
            stretchNode,
            isPlaying: true
          });
          
          setActiveTrackId(trackId);
        }
      }
    } catch (error) {
      console.error('Error in handlePlayPause:', error);
    }
  };

  const handleVolumeChange = (trackId: string, newValue: number | number[]) => {
    const newVolume = newValue as number;
    const track = tracks.find(t => t.id === trackId);
    if (track?.gainNode) {
      track.gainNode.gain.value = newVolume;
    }
    updateTrack(trackId, { volume: newVolume });
  };

  const handleTempoChange = (newValue: number | number[]) => {
    const newTempo = newValue as number;
    setGlobalTempo(newTempo);

    tracks.forEach(track => {
      if (track.sourceNode) {
        const rate = newTempo / track.originalTempo;
        track.sourceNode.playbackRate.value = rate;
      }
      if (track.stretchNode) {
        const semitones = -12 * Math.log2(newTempo / track.originalTempo);
        track.stretchNode.schedule({ rate: 1.0, semitones });
      }
      updateTrack(track.id, { tempo: newTempo });
    });
  };

  async function detectBeats(buffer: AudioBuffer): Promise<{
    beatTimes: number[], 
    phrases: { startTime: number, endTime: number }[],
    bpm: number,
    downbeatOffset: number
  }> {
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
    const processBuffer = new Float32Array(bufferSize);
    
    // Process in hops
    for (let i = 0; i < data.length - bufferSize; i += hopSize) {
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

    const bpm = tempo.getBpm();
    console.log('Detected BPM:', bpm);

    // Adjust BPM to standard range
    let adjustedBpm = bpm;
    if (adjustedBpm < 90) adjustedBpm *= 2;
    if (adjustedBpm > 180) adjustedBpm /= 2;

    // Fit regular grid based on confidence-weighted beats
    const beatInterval = (60000 / adjustedBpm); // ms between beats at the adjusted BPM

    // Find optimal grid offset by trying different starting points
    const durationMs = (buffer.duration * 1000);
    const numBeats = Math.floor(durationMs / beatInterval);

    // Try different offsets within one beat interval to find best alignment
    const numTestPoints = 20;
    let gridOffset = 0;
    let bestScore = -Infinity;

    for (let i = 0; i < numTestPoints; i++) {
      const testOffset = (beatInterval * i) / numTestPoints;
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
    const offsetScores = offsets.map(offset => ({
      offset,
      score: getOffsetScore(offset)
    }));

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

    // Group beats into 32-beat phrases
    const phrases: {startTime: number, endTime: number}[] = [];
    for (let i = 0; i < beatTimes.length; i += 32) {
      const startTime = beatTimes[i];
      const endIndex = (i + 31 < beatTimes.length) ? i + 31 : beatTimes.length - 1;
      const endTime = beatTimes[endIndex];
      phrases.push({ startTime, endTime });
    }

    return { beatTimes, phrases, bpm: adjustedBpm, downbeatOffset: bestOffset };
  }

  return {
    tracks,
    globalTempo,
    handleFileUpload,
    handlePlayPause,
    handleVolumeChange,
    handleTempoChange
  };
} 