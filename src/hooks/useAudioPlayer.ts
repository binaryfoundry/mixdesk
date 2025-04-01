import { useState, useRef, useEffect } from 'react';
import * as mm from 'music-metadata';
import aubio from 'aubiojs';
import SignalsmithStretch from 'signalsmith-stretch';

interface TrackMetadata {
  title: string;
  key: string;
  bpm: number;
}

interface Track {
  id: string;
  file: File;
  metadata: TrackMetadata;
  audioContext: AudioContext;
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
  bpm: number;
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
            bpm: trackMetadata.bpm || 120
          };

          if (newTrack.gainNode) {
            newTrack.gainNode.gain.value = newTrack.volume;
          }

          detectBeats(audioBuffer).then(({ beatTimes, phrases, bpm }) => {
            updateTrack(newTrack.id, {
              bpm,
              beats: beatTimes,
              phrases
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
          const semitones = -12 * Math.log2(globalTempo / track.bpm);
          stretchNode.schedule({ rate: 1.0, semitones });
          stretchNode.start();

          sourceNode.connect(stretchNode);
          if (track.gainNode) {
            stretchNode.connect(track.gainNode);
          }
          
          if (track.gainNode) {
            track.gainNode.gain.value = track.volume;
          }

          const rate = globalTempo / track.bpm;
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
        const rate = newTempo / track.bpm;
        track.sourceNode.playbackRate.value = rate;
      }
      if (track.stretchNode) {
        const semitones = -12 * Math.log2(newTempo / track.bpm);
        track.stretchNode.schedule({ rate: 1.0, semitones });
      }
      updateTrack(track.id, { tempo: newTempo });
    });
  };

  async function detectBeats(buffer: AudioBuffer): Promise<{
    beatTimes: number[], 
    phrases: { startTime: number, endTime: number }[],
    bpm: number
  }> {
    const sampleRate = buffer.sampleRate;
    const numSamples = buffer.length;
    
    const offlineCtx = new OfflineAudioContext(1, numSamples, sampleRate);
    const source = offlineCtx.createBufferSource();
    source.buffer = buffer;
    
    const highpass = offlineCtx.createBiquadFilter();
    highpass.type = 'highpass';
    highpass.frequency.value = 40;
    
    const lowpass = offlineCtx.createBiquadFilter();
    lowpass.type = 'lowpass';
    lowpass.frequency.value = 150;
    
    source.connect(highpass);
    highpass.connect(lowpass);
    lowpass.connect(offlineCtx.destination);
    source.start(0);
    
    const renderedBuffer = await offlineCtx.startRendering();
    const data = renderedBuffer.getChannelData(0);
    
    const { Tempo } = await aubio();  
    const tempo = new Tempo(1024, 512, sampleRate);
    let beatTimes: number[] = [];
    let totalFrames = 0;
    
    const hopSize = 512;
    const bufferSize = 1024;
    const processBuffer = new Float32Array(bufferSize);
    
    for (let i = 0; i < data.length - bufferSize; i += hopSize) {
      for (let j = 0; j < bufferSize; j++) {
        processBuffer[j] = data[i + j];
      }
      
      const result = tempo.do(processBuffer);
      if (result !== 0) {
        const beatTimeMs = (totalFrames / sampleRate) * 1000;
        beatTimes.push(Math.round(beatTimeMs));
      }
      totalFrames += hopSize;
    }
    
    const bpm = tempo.getBpm();
    console.log('Detected BPM:', bpm);
    
    let adjustedBpm = bpm;
    if (adjustedBpm < 90) adjustedBpm *= 2;
    if (adjustedBpm > 180) adjustedBpm /= 2;
    
    const phrases: {startTime: number, endTime: number}[] = [];
    for (let i = 0; i < beatTimes.length; i += 32) {
      const startTime = beatTimes[i];
      const endIndex = (i + 31 < beatTimes.length) ? i + 31 : beatTimes.length - 1;
      const endTime = beatTimes[endIndex];
      phrases.push({ startTime, endTime });
    }

    return { beatTimes, phrases, bpm };
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