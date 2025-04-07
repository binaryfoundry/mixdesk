import { useState, useRef, useEffect } from 'react';
import * as mm from 'music-metadata';
import aubio from 'aubiojs';
import SignalsmithStretch from 'signalsmith-stretch';
import { detectBeats } from '../utils/beatDetection';

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
  const animationFrameRef = useRef<number | null>(null);
  const startTimeRef = useRef<number>(0);
  const startOffsetRef = useRef<number>(0);

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
        const updateTime = () => {
          const elapsed = (track.audioContext.currentTime || 0) - startTimeRef.current;
          const newTime = startOffsetRef.current + elapsed;

          if (isFinite(newTime) && newTime >= 0 && newTime <= track.duration) {
            updateTrack(activeTrackId, { currentTime: newTime });
            animationFrameRef.current = requestAnimationFrame(updateTime);
          }
        };

        animationFrameRef.current = requestAnimationFrame(updateTime);

        return () => {
          if (animationFrameRef.current) {
            cancelAnimationFrame(animationFrameRef.current);
          }
        };
      }
    }
  }, [activeTrackId, tracks]);

  // Clean up animation frame on unmount
  useEffect(() => {
    return () => {
      if (animationFrameRef.current) {
        cancelAnimationFrame(animationFrameRef.current);
      }
    };
  }, []);

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
        if (animationFrameRef.current) {
          cancelAnimationFrame(animationFrameRef.current);
        }
        updateTrack(trackId, { isPlaying: false });
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
          
          // Set the start time and offset right before we start playing
          startTimeRef.current = track.audioContext.currentTime;
          startOffsetRef.current = validCurrentTime;
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

  return {
    tracks,
    globalTempo,
    handleFileUpload,
    handlePlayPause,
    handleVolumeChange,
    handleTempoChange
  };
}