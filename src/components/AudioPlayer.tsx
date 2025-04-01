import { useEffect, useRef, useState } from 'react';
import { Box, Button, Slider, Typography, Paper } from '@mui/material';
import PlayArrowIcon from '@mui/icons-material/PlayArrow';
import PauseIcon from '@mui/icons-material/Pause';
import UploadFileIcon from '@mui/icons-material/UploadFile';
import VolumeUpIcon from '@mui/icons-material/VolumeUp';
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

export default function AudioPlayer() {
  const [tracks, setTracks] = useState<Track[]>([]);
  const [activeTrackId, setActiveTrackId] = useState<string | null>(null);
  const [globalTempo, setGlobalTempo] = useState(120); // 100% = normal speed
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

  // Helper function to update all tracks
  const updateAllTracks = (updates: Partial<Track>) => {
    setTracks(prevTracks => 
      prevTracks.map(track => ({ ...track, ...updates }))
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
          
          // Ensure the new time is a valid number and within bounds
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
      
      // Initialize audio context first
      const audioSetup = await initAudio();
      if (!audioSetup) {
        console.error('Failed to initialize audio context');
        return;
      }
      
      // Read metadata first
      const trackMetadata = await readMetadata(file);
      
      // Load audio into Web Audio API
      const arrayBuffer = await file.arrayBuffer();
      const audioBuffer = await audioSetup.audioContext.decodeAudioData(arrayBuffer);
      if (audioBuffer) {
        // Create initial source node
        const sourceNode = audioSetup.audioContext.createBufferSource();
        if (sourceNode) {
          sourceNode.buffer = audioBuffer;
          
          // Create new track
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

          // Set initial volume
          if (newTrack.gainNode) {
            newTrack.gainNode.gain.value = newTrack.volume;
          }

          // Detect beats
          detectBeats(audioBuffer, trackMetadata.bpm || 120).then(({ beatTimes, phrases, bpm }) => {
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
      // Resume audio context if it's suspended
      if (track.audioContext.state === 'suspended') {
        await track.audioContext.resume();
      }

      if (track.isPlaying) {
        track.sourceNode?.stop();
        if (timeUpdateIntervalRef.current) {
          window.clearInterval(timeUpdateIntervalRef.current);
        }
      } else {
        // Ensure currentTime is valid before starting playback
        const validCurrentTime = isFinite(track.currentTime) ? track.currentTime : 0;
        
        // Create and start a new source node
        const arrayBuffer = await track.file.arrayBuffer();
        const audioBuffer = await track.audioContext.decodeAudioData(arrayBuffer);
        const sourceNode = track.audioContext.createBufferSource();
        
        if (sourceNode) {
          sourceNode.buffer = audioBuffer;
          // Create and configure stretch node
          const stretchNode = await SignalsmithStretch(track.audioContext);
          const semitones = -12 * Math.log2(globalTempo / track.bpm);
          stretchNode.schedule({ rate: 1.0, semitones });
          stretchNode.start();

          // Connect nodes: source -> stretch -> gain -> destination
          sourceNode.connect(stretchNode);
          if (track.gainNode) {
            stretchNode.connect(track.gainNode);
          }
          
          // Set initial volume
          if (track.gainNode) {
            track.gainNode.gain.value = track.volume;
          }

          // Start playback
          const rate = globalTempo / track.bpm;
          sourceNode.playbackRate.value = rate;
          sourceNode.start(0, validCurrentTime);
          
          // Update track state
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

    // Update each track's tempo and playback rate
    tracks.forEach(track => {
      if (track.sourceNode) {
        // Set playback rate on source node
        const rate = newTempo / track.bpm; // Convert percentage to rate (e.g., 120% = 1.2)

        track.sourceNode.playbackRate.value = rate;
      }
      if (track.stretchNode) {
        // Calculate semitone adjustment from BPM difference
        // Formula: semitones = 12 * log2(newBPM / originalBPM)
        const semitones = -12 * Math.log2(newTempo / track.bpm);
        track.stretchNode.schedule({ rate: 1.0, semitones });
      }
      updateTrack(track.id, { tempo: newTempo });
    });
  };

  async function detectBeats(buffer: AudioBuffer, metadataBpm: number): Promise<{
    beatTimes: number[], 
    phrases: { startTime: number, endTime: number }[],
    bpm: number
  }> {
    const sampleRate = buffer.sampleRate;
    const numSamples = buffer.length;
    
    // 1. Offline context for filtering (mono, full length)
    const offlineCtx = new OfflineAudioContext(1, numSamples, sampleRate);
    const source = offlineCtx.createBufferSource();
    source.buffer = buffer;
    // Create filters to isolate kick drum frequencies
    const highpass = offlineCtx.createBiquadFilter();
    highpass.type = 'highpass';
    highpass.frequency.value = 40;            // cut rumble below 40 Hz
    const lowpass = offlineCtx.createBiquadFilter();
    lowpass.type = 'lowpass';
    lowpass.frequency.value = 150;           // cut above 150 Hz to focus on bass
    // Connect nodes: source -> highpass -> lowpass -> destination
    source.connect(highpass);
    highpass.connect(lowpass);
    lowpass.connect(offlineCtx.destination);
    source.start(0);
    // Render offline audio
    const renderedBuffer = await offlineCtx.startRendering();
    const data = renderedBuffer.getChannelData(0);  // get mono PCM data
    
    // 2. Use aubio.js for beat (tempo) detection
    const { Tempo } = await aubio();  
    const tempo = new Tempo(1024, 512, sampleRate);
    let beatTimes: number[] = [];
    let totalFrames = 0;
    
    // Create a buffer for processing
    const hopSize = 512;
    const bufferSize = 1024;
    const processBuffer = new Float32Array(bufferSize);
    
    // Process in hops of 512 samples
    for (let i = 0; i < data.length - bufferSize; i += hopSize) {
      // Copy data into process buffer
      for (let j = 0; j < bufferSize; j++) {
        processBuffer[j] = data[i + j];
      }
      
      // Process this frame
      const result = tempo.do(processBuffer);
      if (result !== 0) {
        // Beat detected at end of this frame
        const beatTimeMs = (totalFrames / sampleRate) * 1000;
        beatTimes.push(Math.round(beatTimeMs));     // record beat timestamp in ms
      }
      totalFrames += hopSize;
    }
    
    const bpm = tempo.getBpm();
    console.log('Detected BPM:', bpm);
    
    // (Optional) adjust BPM to standard range and refine beat times
    let adjustedBpm = bpm;
    if (adjustedBpm < 90) adjustedBpm *= 2;
    if (adjustedBpm > 180) adjustedBpm /= 2;
    
    // 3. Group beats into 32-beat phrases
    const phrases: {startTime: number, endTime: number}[] = [];
    for (let i = 0; i < beatTimes.length; i += 32) {
      const startTime = beatTimes[i];
      // If fewer than 32 beats remain, use last beat as endTime
      const endIndex = (i + 31 < beatTimes.length) ? i + 31 : beatTimes.length - 1;
      const endTime = beatTimes[endIndex];
      phrases.push({ startTime, endTime });
    }

    return { beatTimes, phrases, bpm };
  }

  return (
    <Paper elevation={3} sx={{ 
      p: 3, 
      width: '100%', 
      height: '100%', 
      borderRadius: 0,
      display: 'flex',
      flexDirection: 'column'
    }}>
      <Box sx={{ 
        display: 'flex', 
        flexDirection: 'column', 
        gap: 2, 
        maxWidth: '1200px', 
        mx: 'auto',
        flex: 1,
        height: '100%'
      }}>
        <Typography variant="h5" gutterBottom>
          MixDesk
        </Typography>
        
        <Box sx={{ display: 'flex', alignItems: 'center', gap: 2 }}>
          <Button
            variant="contained"
            component="label"
            startIcon={<UploadFileIcon />}
          >
            Upload Audio
            <input
              type="file"
              hidden
              accept="audio/*"
              onChange={handleFileUpload}
            />
          </Button>
        </Box>

        <Box sx={{ 
          display: 'flex', 
          alignItems: 'center', 
          gap: 2,
          p: 2,
          border: '1px solid',
          borderColor: 'divider',
          borderRadius: 1,
          mb: 2
        }}>
          <Typography variant="body1" sx={{ minWidth: '100px' }}>
            Global Tempo: {globalTempo}
          </Typography>
          <Slider
            value={globalTempo}
            onChange={(e, v) => handleTempoChange(v)}
            min={90}
            max={150}
            step={1}
            sx={{ flex: 1 }}
          />
        </Box>

        {tracks.map(track => (
          <Box key={track.id} sx={{ 
            display: 'flex', 
            alignItems: 'center', 
            gap: 2,
            flexWrap: 'wrap',
            p: 2,
            border: '1px solid',
            borderColor: 'divider',
            borderRadius: 1
          }}>
            <Box sx={{ display: 'flex', flexDirection: 'column', flex: 1 }}>
              <Typography variant="body2" color="text.secondary">
                {track.metadata.title || track.file.name}
              </Typography>
              <Typography variant="caption" color="text.secondary">
                Key: {track.metadata.key} | BPM: {track.metadata.bpm}
              </Typography>
            </Box>

            <Button
              variant="contained"
              onClick={() => handlePlayPause(track.id)}
              startIcon={track.isPlaying ? <PauseIcon /> : <PlayArrowIcon />}
              sx={{ minWidth: '120px' }}
            >
              {track.isPlaying ? 'Pause' : 'Play'}
            </Button>

            <Box sx={{ 
              display: 'flex', 
              alignItems: 'center', 
              gap: 1, 
              width: { xs: '100%', sm: '200px' },
              minWidth: '200px'
            }}>
              <VolumeUpIcon color={track.volume === 0 ? 'disabled' : 'primary'} />
              <Slider
                value={track.volume}
                onChange={(e, v) => handleVolumeChange(track.id, v)}
                min={0}
                max={1}
                step={0.01}
                size="small"
              />
            </Box>
          </Box>
        ))}
      </Box>
    </Paper>
  );
} 