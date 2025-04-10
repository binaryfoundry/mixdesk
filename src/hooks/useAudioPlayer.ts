import { useState, useRef, useEffect } from 'react';
import * as mm from 'music-metadata';
import SignalsmithStretch from 'signalsmith-stretch';
import { detectBeats } from '../utils/beatDetection';
import { Metronome } from '../Metronome';

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
  clickedBeatIndex: number | null;
  startAudioContextTime: number;
  selectedStartTime: number;
  adjustedStartTime: number;
}

export const METRONOME_BEAT_EVENT = 'metronomeBeat';

export function useAudioPlayer() {
  const [tracks, setTracks] = useState<Track[]>([]);
  const animationFrameRef = useRef<number | null>(null);
  const metronomeInitializedRef = useRef<boolean>(false);
  const metronomeRef = useRef<Metronome | null>(null);

  // Initialize metronome only once
  if (!metronomeInitializedRef.current) {
    metronomeRef.current = new Metronome(120);
    metronomeRef.current.start();
    metronomeInitializedRef.current = true;
  }

  // Helper function to adjust playback rate and pitch
  const adjustPlaybackRate = (
    track: Track,
    correctionFactor: number = 1
  ) => {
    if (!track.sourceNode || !track.stretchNode || !metronomeRef.current) return;
    
    const rate = (metronomeRef.current.getTempo() / track.originalTempo) * correctionFactor;
    
    // Use setValueAtTime for precise timing
    track.sourceNode.playbackRate.setValueAtTime(rate, track.audioContext.currentTime);
    
    const semitones = -12 * Math.log2(rate);
    track.stretchNode.schedule({ rate, semitones });
  };

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
      tracks.forEach(track => {
        if (track.isPlaying && track.sourceNode && track.startAudioContextTime !== null) {
          const elapsed = (track.audioContext.currentTime || 0) - track.startAudioContextTime;
          const currentTime = track.adjustedStartTime + elapsed;
          updateTrack(track.id, { currentTime: currentTime });

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

      // Initialize stretch node
      const stretchNode = await SignalsmithStretch(track.audioContext);
      stretchNode.start();
      adjustPlaybackRate(track, 1);

      // Connect the audio processing chain
      sourceNode.connect(stretchNode);
      if (track.gainNode) {
        stretchNode.connect(track.gainNode);
      }
      track.sourceNode = sourceNode;
      track.stretchNode = stretchNode;
      return;
    } catch (error) {
      console.error('Error initializing audio processing:', error);
      return;
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
          downbeatOffset: 0,
          clickedBeatIndex: null,
          startAudioContextTime: 0,
          selectedStartTime: 0,
          adjustedStartTime: 0
        };

        // Initialize audio processing
        await initAudioProcessing(newTrack);

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
        track.sourceNode?.stop();
      }

      // Start playback
      const sourceNode = track.audioContext.createBufferSource();
      sourceNode.buffer = track.audioBuffer;
      sourceNode.connect(track.stretchNode!);
      track.sourceNode = sourceNode;

      adjustPlaybackRate(track, 1);

      const timeUntilNextBeat = metronomeRef.current?.getTimeUntilNextBeat() || 0;
      const adjustedStartTime = track.selectedStartTime - timeUntilNextBeat;

      sourceNode.start(0, adjustedStartTime);
      adjustPlaybackRate(track, 1);

      track.startAudioContextTime = track.audioContext.currentTime;
      updateTrack(trackId, { isPlaying: true, adjustedStartTime: adjustedStartTime });

    } catch (error) {
      console.error('Error handling play/pause:', error);
    }
  };

  const handleVolumeChange = (trackId: string, newValue: number | number[]) => {
    const newVolume = newValue as number;
    const track = tracks.find(t => t.id === trackId);
    if (track?.gainNode) {
      track.gainNode.gain.value = newVolume;
      track.volume = newVolume;
    }
  };

  const handleTempoChange = (newValue: number | number[]) => {
    const newTempo = newValue as number;
    if (metronomeRef.current) {
      metronomeRef.current.setTempo(newTempo);
    }

    // Update track playback rates
    tracks.forEach(track => {
      if (track.sourceNode) {
        adjustPlaybackRate(track, 1);
      }
      track.tempo = newTempo;
    });
  };

  return {
    tracks,
    handleFileUpload,
    handlePlayPause,
    handleVolumeChange,
    handleTempoChange,
    metronome: metronomeRef.current
  };
}