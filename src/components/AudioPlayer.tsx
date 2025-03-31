import { useEffect, useRef, useState, useCallback } from 'react';
import WaveSurfer from 'wavesurfer.js';
import RegionsPlugin from 'wavesurfer.js/dist/plugins/regions.js';
import { Box, Button, Slider, Typography, Paper } from '@mui/material';
import PlayArrowIcon from '@mui/icons-material/PlayArrow';
import PauseIcon from '@mui/icons-material/Pause';
import UploadFileIcon from '@mui/icons-material/UploadFile';
import VolumeUpIcon from '@mui/icons-material/VolumeUp';
import { analyze } from 'web-audio-beat-detector';
import * as mm from 'music-metadata';
import aubio from 'aubiojs';

interface TrackMetadata {
  title: string;
  key: string;
  bpm: number;
}

const AudioPlayer = () => {
  const waveformRef = useRef<HTMLDivElement>(null);
  const wavesurferRef = useRef<WaveSurfer | null>(null);
  const tempoTimeoutRef = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const [isPlaying, setIsPlaying] = useState(false);
  const [tempo, setTempo] = useState(0);
  const [volume, setVolume] = useState(1);
  const [audioFile, setAudioFile] = useState<File | null>(null);
  const [beats, setBeats] = useState<number[]>([]);
  const [gridColor, setGridColor] = useState('#ff0000');
  const [metadata, setMetadata] = useState<TrackMetadata | null>(null);
  const [phrases, setPhrases] = useState<{ startTime: number, endTime: number }[]>([]);

  useEffect(() => {
    if (waveformRef.current) {
      const wavesurfer = WaveSurfer.create({
        container: waveformRef.current,
        waveColor: '#4a9eff',
        progressColor: '#2c5282',
        cursorColor: '#2c5282',
        barWidth: 2,
        barRadius: 3,
        cursorWidth: 1,
        height: 100,
        barGap: 1,
        normalize: true,
        fillParent: true,
        minPxPerSec: 1,
        interact: true,
        hideScrollbar: true,
        autoCenter: true,
        autoScroll: false,
        backend: 'MediaElement',
        mediaControls: false,
        plugins: [RegionsPlugin.create()]
      });

      wavesurferRef.current = wavesurfer;

      wavesurfer.on('finish', () => {
        setIsPlaying(false);
      });

      return () => {
        if (tempoTimeoutRef.current) {
          clearTimeout(tempoTimeoutRef.current);
        }
        wavesurfer.destroy();
      };
    }
  }, []);

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
  

  const readMetadata = async (file: File): Promise<TrackMetadata> => {
    try {
      const arrayBuffer = await file.arrayBuffer();
      const buffer = new Uint8Array(arrayBuffer);
      const metadata = await mm.parseBuffer(buffer, file.type);
      
      const title = metadata.common.title || file.name;
      const key = metadata.common.key || 'Unknown';
      
      // Log all available metadata for debugging
      console.log('Full metadata:', metadata);
      
      // Try to get BPM from common tags
      let bpm = 0;
      if (metadata.common.bpm) {
        bpm = metadata.common.bpm;
      }
      
      console.log('Extracted BPM:', bpm);
      
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
    if (file && wavesurferRef.current) {
      console.log('Loading audio file:', file.name);
      setAudioFile(file);
      
      // Read metadata first
      const trackMetadata = await readMetadata(file);
      setMetadata(trackMetadata);
      
      const audioUrl = URL.createObjectURL(file);
      
      // Listen for ready event before loading
      wavesurferRef.current.on('ready', () => {
        console.log('WaveSurfer is ready after loading audio');
        // Create an AudioContext to analyze the file
        const audioContext = new (window.AudioContext || (window as any).webkitAudioContext)();
        file.arrayBuffer().then(arrayBuffer => {
          audioContext.decodeAudioData(arrayBuffer).then(audioBuffer => {
            detectBeats(audioBuffer, trackMetadata.bpm || 120).then(({ beatTimes, phrases }) => {
              setBeats(beatTimes);
              setPhrases(phrases);
            });
          });
        });
      });

      wavesurferRef.current.load(audioUrl);
    }
  };

  const handlePlayPause = () => {
    if (wavesurferRef.current) {
      wavesurferRef.current.playPause();
      setIsPlaying(!isPlaying);
    }
  };

  const updateTempo = useCallback((newTempo: number) => {
    if (wavesurferRef.current) {
      const playbackRate = 1 + (newTempo / 100);
      wavesurferRef.current.setPlaybackRate(playbackRate, true);
    }
  }, []);

  const handleTempoChange = (event: Event, newValue: number | number[]) => {
    const newTempo = newValue as number;
    setTempo(newTempo);

    if (tempoTimeoutRef.current) {
      clearTimeout(tempoTimeoutRef.current);
    }

    tempoTimeoutRef.current = setTimeout(() => {
      updateTempo(newTempo);
    }, 50);
  };

  const handleVolumeChange = (event: Event, newValue: number | number[]) => {
    const newVolume = newValue as number;
    setVolume(newVolume);
    if (wavesurferRef.current) {
      wavesurferRef.current.setVolume(newVolume);
    }
  };

  // Update beat regions
  useEffect(() => {
    if (wavesurferRef.current && beats.length > 0) {
      console.log('Updating beat regions with', beats.length, 'beats');
      const regionsPlugin = wavesurferRef.current.getActivePlugins()[0];

      // Clear existing regions
      regionsPlugin.clearRegions();
      let regionIndex = 0;

      // Add regions for each phrase
      phrases.forEach((phrase, index) => {
        regionsPlugin.addRegion({
          start: phrase.startTime / 1000, // Convert ms to seconds
          end: phrase.endTime / 1000,     // Convert ms to seconds
          color: index % 2 === 0 ? 'rgba(0, 0, 255, 0.1)' : 'rgba(0, 0, 255, 0.2)', // Transparent colors
          drag: false,
          resize: false,
          channelIdx: -1, // -1 means cover all channels
          minWidth: 0,
          maxWidth: 0,
          minHeight: 0,
          maxHeight: 0
        });
        regionIndex++;
      });
    }
  }, [beats, phrases, gridColor]);

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

        <Box ref={waveformRef} sx={{ 
          width: '100%', 
          flex: 1,
          minHeight: '200px',
          position: 'relative'
        }} />

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
            <Typography gutterBottom>Tempo: {tempo > 0 ? '+' : ''}{tempo}%</Typography>
            <Slider
              value={tempo}
              onChange={handleTempoChange}
              min={-10}
              max={10}
              step={0.1}
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
};

export default AudioPlayer; 