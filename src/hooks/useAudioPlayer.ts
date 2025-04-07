import { useState, useRef, useEffect } from 'react';
import * as mm from 'music-metadata';
import SignalsmithStretch from 'signalsmith-stretch';
import { detectBeats } from '../utils/beatDetection';

// Create an event emitter for metronome beats
const metronomeEmitter = new EventTarget();
export const METRONOME_BEAT_EVENT = 'metronomeBeat';

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
  const [globalTempo, setGlobalTempo] = useState(120);
  const timeUpdateIntervalRef = useRef<number | null>(null);
  const animationFrameRef = useRef<number | null>(null);
  const startTimeRefs = useRef<Map<string, number>>(new Map());
  const startOffsetRefs = useRef<Map<string, number>>(new Map());
  const metronomeContextRef = useRef<AudioContext | null>(null);
  const nextBeatTimeRef = useRef<number>(0);
  const beatCountRef = useRef<number>(0);
  const metronomeSchedulerRef = useRef<number | null>(null);
  const currentTempoRef = useRef<number>(120);

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
    const updateTime = () => {
      const now = Date.now();
      tracks.forEach(track => {
        if (track.isPlaying && track.sourceNode) {
          const startTime = startTimeRefs.current.get(track.id);
          const startOffset = startOffsetRefs.current.get(track.id);
          
          if (startTime !== undefined && startOffset !== undefined) {
            const elapsed = (track.audioContext.currentTime || 0) - startTime;
            const newTime = startOffset + elapsed;

            if (isFinite(newTime) && newTime >= 0 && newTime <= track.duration) {
              updateTrack(track.id, { currentTime: newTime });
            }
          }
        }
      });
      animationFrameRef.current = requestAnimationFrame(updateTime);
    };

    animationFrameRef.current = requestAnimationFrame(updateTime);

    return () => {
      if (animationFrameRef.current) {
        cancelAnimationFrame(animationFrameRef.current);
      }
    };
  }, [tracks]);

  // Initialize metronome audio context
  useEffect(() => {
    metronomeContextRef.current = new (window.AudioContext || (window as any).webkitAudioContext)();
    nextBeatTimeRef.current = metronomeContextRef.current.currentTime;
    currentTempoRef.current = globalTempo;
    scheduleBeats();

    return () => {
      if (metronomeSchedulerRef.current) {
        clearTimeout(metronomeSchedulerRef.current);
      }
      metronomeContextRef.current?.close();
    };
  }, []);

  // Update currentTempoRef when globalTempo changes
  useEffect(() => {
    currentTempoRef.current = globalTempo;
  }, [globalTempo]);

  // Schedule upcoming metronome beats
  const scheduleBeats = () => {
    const lookaheadMs = 25.0;
    const scheduleAheadTime = 0.1;
    
    const context = metronomeContextRef.current;
    if (!context) return;

    const currentTime = context.currentTime;
    
    // Only schedule the next beat if we're close enough to it
    if (nextBeatTimeRef.current < currentTime + scheduleAheadTime) {
      const beatNumber = beatCountRef.current + 1;

      // Dispatch beat event
      const beatEvent = new CustomEvent(METRONOME_BEAT_EVENT, {
        detail: {
          beatNumber,
          time: nextBeatTimeRef.current,
          isDownbeat: beatNumber === 1
        }
      });
      metronomeEmitter.dispatchEvent(beatEvent);
      
      beatCountRef.current = (beatCountRef.current + 1) % 4;
      
      // Calculate next beat time using current tempo from ref
      const secondsPerBeat = 60.0 / currentTempoRef.current;
      nextBeatTimeRef.current = currentTime + secondsPerBeat;
    }

    // Always schedule the next check
    metronomeSchedulerRef.current = window.setTimeout(scheduleBeats, lookaheadMs);
  };

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

  const initAudioProcessing = async (track: Track) => {
    try {
      // Create source node
      const sourceNode = track.audioContext.createBufferSource();
      sourceNode.buffer = track.audioBuffer;

      // Set the playback rate
      const rate = globalTempo / track.originalTempo;
      sourceNode.playbackRate.value = rate;

      // Initialize stretch node
      const stretchNode = await SignalsmithStretch(track.audioContext);
      const semitones = -12 * Math.log2(rate);
      stretchNode.schedule({ rate: rate, semitones: semitones });
      stretchNode.start();

      // Connect the audio processing chain
      sourceNode.connect(stretchNode);
      if (track.gainNode) {
        stretchNode.connect(track.gainNode);
      }


      return { sourceNode, stretchNode };
    } catch (error) {
      console.error('Error initializing audio processing:', error);
      return null;
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
        const newTrack: Track = {
          id: crypto.randomUUID(),
          file,
          metadata: trackMetadata,
          audioContext: audioSetup.audioContext,
          audioBuffer: audioBuffer,
          sourceNode: null,
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

        // Initialize audio processing
        const processing = await initAudioProcessing(newTrack);
        if (processing) {
          newTrack.sourceNode = processing.sourceNode;
          newTrack.stretchNode = processing.stretchNode;
        }

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
  };

  const handlePlayPause = async (trackId: string) => {
    const track = tracks.find(t => t.id === trackId);
    if (!track) return;

    try {
      if (track.audioContext.state === 'suspended') {
        await track.audioContext.resume();
      }

      if (track.isPlaying) {
        // Stop the current playback
        track.sourceNode?.stop();
        
        // If we're seeking to a new position, start playing from there
        if (track.currentTime > 0) {
          // Create a new source node
          const sourceNode = track.audioContext.createBufferSource();
          sourceNode.buffer = track.audioBuffer;
          sourceNode.connect(track.stretchNode!);
          sourceNode.playbackRate.value = globalTempo / track.originalTempo;

          // Start playback from the current time
          startTimeRefs.current.set(track.id, track.audioContext.currentTime);
          startOffsetRefs.current.set(track.id, track.currentTime);
          sourceNode.start(0, track.currentTime);

          updateTrack(trackId, { 
            sourceNode,
            isPlaying: true 
          });
        } else {
          // If not seeking, just pause
          updateTrack(trackId, { isPlaying: false });
        }
      } else {
        const validCurrentTime = isFinite(track.currentTime) ? track.currentTime : 0;

        // Create a new source node
        const sourceNode = track.audioContext.createBufferSource();
        sourceNode.buffer = track.audioBuffer;
        sourceNode.connect(track.stretchNode!);
        sourceNode.playbackRate.value = globalTempo / track.originalTempo;

        // Start playback from the current time
        startTimeRefs.current.set(track.id, track.audioContext.currentTime);
        startOffsetRefs.current.set(track.id, validCurrentTime);
        sourceNode.start(0, validCurrentTime);

        updateTrack(trackId, { 
          sourceNode,
          isPlaying: true 
        });
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
    console.log('Tempo changing to:', newValue);
    const newTempo = newValue as number;
    setGlobalTempo(newTempo);

    // Update track playback rates
    tracks.forEach(track => {
      if (track.sourceNode) {
        const rate = newTempo / track.originalTempo;
        track.sourceNode.playbackRate.value = rate;

        if (track.stretchNode) {
          const semitones = -12 * Math.log2(newTempo / track.originalTempo);
          track.stretchNode.schedule({ rate: rate, semitones });
        }
      }
      updateTrack(track.id, { tempo: newTempo });
    });
  };

  return {
    tracks,
    globalTempo,
    handleFileUpload,
    handlePlayPause,
    handleVolumeChange,
    handleTempoChange,
    metronomeEmitter
  };
}