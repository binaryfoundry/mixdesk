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

export default function AudioPlayer() {
  const audioContextRef = useRef<AudioContext | null>(null);
  const sourceNodeRef = useRef<AudioBufferSourceNode | null>(null);
  const gainNodeRef = useRef<GainNode | null>(null);
  const stretchNodeRef = useRef<any>(null);
  const timeUpdateIntervalRef = useRef<number | null>(null);
  const [isPlaying, setIsPlaying] = useState(false);
  const [currentTime, setCurrentTime] = useState(0);
  const [duration, setDuration] = useState(0);
  const [volume, setVolume] = useState(1);
  const [tempo, setTempo] = useState(120); // 100% = normal speed
  const [audioFile, setAudioFile] = useState<File | null>(null);
  const [metadata, setMetadata] = useState<TrackMetadata | null>(null);
  const [beats, setBeats] = useState<number[]>([]);
  const [phrases, setPhrases] = useState<{ startTime: number, endTime: number }[]>([]);

  // Update current time while playing
  useEffect(() => {
    if (isPlaying && sourceNodeRef.current) {
      const startTime = audioContextRef.current?.currentTime || 0;
      const startOffset = currentTime;
      
      timeUpdateIntervalRef.current = window.setInterval(() => {
        const elapsed = (audioContextRef.current?.currentTime || 0) - startTime;
        const newTime = startOffset + elapsed;
        
        // Ensure the new time is a valid number and within bounds
        if (isFinite(newTime) && newTime >= 0 && newTime <= duration) {
          setCurrentTime(newTime);
        }
      }, 100);
    } else if (timeUpdateIntervalRef.current) {
      window.clearInterval(timeUpdateIntervalRef.current);
    }
  }, [isPlaying, currentTime, duration]);

  const initAudio = async () => {
    try {
      audioContextRef.current = new (window.AudioContext || (window as any).webkitAudioContext)();
      
      // Create gain node
      gainNodeRef.current = audioContextRef.current.createGain();
      gainNodeRef.current.connect(audioContextRef.current.destination);

      return true;
    } catch (error) {
      console.error('Error initializing audio:', error);
      return false;
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
      setAudioFile(file);
      
      // Initialize audio context first
      const audioInitialized = await initAudio();
      if (!audioInitialized) {
        console.error('Failed to initialize audio context');
        return;
      }
      
      // Read metadata first
      const trackMetadata = await readMetadata(file);
      setMetadata(trackMetadata);
      
      // Load audio into Web Audio API
      const arrayBuffer = await file.arrayBuffer();
      const audioBuffer = await audioContextRef.current?.decodeAudioData(arrayBuffer);
      if (audioBuffer && audioContextRef.current && gainNodeRef.current) {
        setDuration(audioBuffer.duration);
        
        // Create initial source node
        const sourceNode = audioContextRef.current.createBufferSource();
        if (sourceNode) {
          sourceNodeRef.current = sourceNode;
          sourceNode.buffer = audioBuffer;
          
          // Connect nodes: source -> gain -> destination
          sourceNode.connect(gainNodeRef.current);
          
          // Set initial volume
          gainNodeRef.current.gain.value = volume;
        }

        // Detect beats
        detectBeats(audioBuffer, trackMetadata.bpm || 120).then(({ beatTimes, phrases }) => {
          setBeats(beatTimes);
          setPhrases(phrases);
        });
      }
    }
  };

  const handlePlayPause = async () => {
    if (audioFile && audioContextRef.current && gainNodeRef.current) {
      try {
        // Resume audio context if it's suspended
        if (audioContextRef.current.state === 'suspended') {
          await audioContextRef.current.resume();
        }

        if (isPlaying) {
          sourceNodeRef.current?.stop();
          if (timeUpdateIntervalRef.current) {
            window.clearInterval(timeUpdateIntervalRef.current);
          }
        } else {
          // Ensure currentTime is valid before starting playback
          const validCurrentTime = isFinite(currentTime) ? currentTime : 0;
          
          // Create and start a new source node
          const arrayBuffer = await audioFile.arrayBuffer();
          const audioBuffer = await audioContextRef.current.decodeAudioData(arrayBuffer);
          const sourceNode = audioContextRef.current.createBufferSource();
          
          if (sourceNode) {
            sourceNodeRef.current = sourceNode;
            sourceNode.buffer = audioBuffer;
            
            // Create and configure stretch node
            const stretchNode = await SignalsmithStretch(audioContextRef.current);
            stretchNodeRef.current = stretchNode;
            
            // Connect nodes: source -> stretch -> gain -> destination
            sourceNode.connect(stretchNode);
            stretchNode.connect(gainNodeRef.current);
            gainNodeRef.current.connect(audioContextRef.current.destination);
            
            // Set initial volume
            gainNodeRef.current.gain.value = volume;
            
            // Start playback
            stretchNode.start();
            sourceNode.start(0, validCurrentTime);
            
            // Debug logging
            console.log('Audio playback started:', {
              contextState: audioContextRef.current.state,
              volume: gainNodeRef.current.gain.value,
              currentTime: validCurrentTime
            });
          }
        }
        setIsPlaying(!isPlaying);
      } catch (error) {
        console.error('Error in handlePlayPause:', error);
      }
    }
  };

  const handleVolumeChange = (event: Event, newValue: number | number[]) => {
    const newVolume = newValue as number;
    setVolume(newVolume);
    if (gainNodeRef.current) {
      gainNodeRef.current.gain.value = newVolume;
    }
  };

  const handleTempoChange = (event: Event, newValue: number | number[]) => {
    const newTempo = newValue as number;
    setTempo(newTempo);
    
    // Convert percentage to rate (e.g., 100% = 1.0, 150% = 1.5, 50% = 0.5)
    const rate = newTempo / 100;
    
    // Update the stretch node's rate
    if (stretchNodeRef.current) {
      stretchNodeRef.current.schedule({ rate, semitones: -2 });
    }
  };

  async function detectBeats(buffer: AudioBuffer, metadataBpm: number): Promise<{
    beatTimes: number[], 
    phrases: { startTime: number, endTime: number }[]
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

    return { beatTimes, phrases };
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
          {audioFile && (
            <Box sx={{ display: 'flex', flexDirection: 'column' }}>
              <Typography variant="body2" color="text.secondary">
                {metadata?.title || audioFile.name}
              </Typography>
              {metadata && (
                <Typography variant="caption" color="text.secondary">
                  Key: {metadata.key} | BPM: {metadata.bpm}
                </Typography>
              )}
            </Box>
          )}
        </Box>

        <Box sx={{ 
          display: 'flex', 
          alignItems: 'center', 
          gap: 2,
          flexWrap: 'wrap',
          '@media (max-width: 600px)': {
            flexDirection: 'column',
            alignItems: 'stretch'
          }
        }}>
          <Button
            variant="contained"
            onClick={handlePlayPause}
            disabled={!audioFile}
            startIcon={isPlaying ? <PauseIcon /> : <PlayArrowIcon />}
            sx={{ minWidth: '120px' }}
          >
            {isPlaying ? 'Pause' : 'Play'}
          </Button>

          <Box sx={{ flex: 1, minWidth: '200px' }}>
            <Typography gutterBottom>Tempo: {tempo}</Typography>
            <Slider
              value={tempo}
              onChange={handleTempoChange}
              min={90}
              max={150}
              step={1}
              disabled={!audioFile}
            />
          </Box>

          <Box sx={{ 
            display: 'flex', 
            alignItems: 'center', 
            gap: 1, 
            width: { xs: '100%', sm: '200px' },
            minWidth: '200px'
          }}>
            <VolumeUpIcon color={volume === 0 ? 'disabled' : 'primary'} />
            <Slider
              value={volume}
              onChange={handleVolumeChange}
              min={0}
              max={1}
              step={0.01}
              disabled={!audioFile}
              size="small"
            />
          </Box>
        </Box>
      </Box>
    </Paper>
  );
} 