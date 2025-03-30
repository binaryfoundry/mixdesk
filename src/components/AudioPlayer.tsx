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

  const detectBeats = async (audioBuffer: AudioBuffer, metadataBpm: number) => {
    try {
      // Set min/max tempo based on metadata BPM
      const minTempo = Math.max(60, metadataBpm * 0.95);
      const maxTempo = Math.min(200, metadataBpm * 1.05);

      const bpm = await analyze(audioBuffer, { maxTempo, minTempo });
      console.log('Detected BPM:' + bpm + '  Metadata BPM:' + metadataBpm);

      // Create an AudioContext for analysis
      const audioContext = new (window.AudioContext || (window as any).webkitAudioContext)();
      
      // Create an analyzer node
      const analyzer = audioContext.createAnalyser();
      analyzer.fftSize = 2048;
      
      // Create a source node from the audio buffer
      const source = audioContext.createBufferSource();
      source.buffer = audioBuffer;
      
      // Connect nodes
      source.connect(analyzer);
      analyzer.connect(audioContext.destination);
      
      // Calculate the expected beat interval in samples
      const beatIntervalSamples = audioBuffer.sampleRate * (60 / bpm);
      
      // Create arrays for analysis
      const bufferLength = analyzer.frequencyBinCount;
      const timeData = new Float32Array(bufferLength);
      const freqData = new Float32Array(bufferLength);
      
      // Analyze the first few seconds to find the first beat
      const analysisDuration = 5; // seconds
      const analysisSamples = Math.min(analysisDuration * audioBuffer.sampleRate, audioBuffer.length);
      const sampleStep = Math.floor(analysisSamples / 200); // Analyze 200 points
      
      let maxEnergy = 0;
      let firstBeatSample = 0;
      
      // Get the audio data from the first channel
      const channelData = audioBuffer.getChannelData(0);
      
      // Analyze samples in chunks
      for (let sample = 0; sample < analysisSamples; sample += sampleStep) {
        // Get time domain data
        analyzer.getFloatTimeDomainData(timeData);
        
        // Get frequency domain data
        analyzer.getFloatFrequencyData(freqData);
        
        // Calculate energy in the current chunk
        let energy = 0;
        const chunkSize = Math.min(sampleStep, analysisSamples - sample);
        
        // Calculate time domain energy
        for (let i = 0; i < chunkSize; i++) {
          const value = channelData[sample + i];
          energy += value * value;
        }
        
        // Add frequency domain energy (focusing on bass frequencies)
        let bassEnergy = 0;
        for (let i = 0; i < 20; i++) { // First 20 frequency bins (roughly 0-500Hz)
          bassEnergy += Math.pow(10, freqData[i] / 10);
        }
        
        // Combine time and frequency domain energy
        const totalEnergy = energy + (bassEnergy * 0.5);
        
        // If this is a potential beat (based on expected interval)
        if (sample % Math.floor(beatIntervalSamples) < sampleStep) {
          if (totalEnergy > maxEnergy) {
            maxEnergy = totalEnergy;
            firstBeatSample = sample;
          }
        }
      }
      
      // Convert sample position to time
      const startTime = firstBeatSample / audioBuffer.sampleRate;
      console.log('First beat detected at:', startTime, 'seconds with energy:', maxEnergy);

      // Generate beat times based on metadata BPM
      const beatIntervalSeconds = 60 / metadataBpm; // Time between beats in seconds
      const duration = audioBuffer.duration;
      const beatTimes: number[] = [];
      
      // Generate beat times starting from the first beat
      for (let time = startTime; time < duration; time += beatIntervalSeconds) {
        beatTimes.push(time);
      }

      setBeats(beatTimes);
    } catch (error) {
      console.error('Error detecting beats:', error);
      // Set default values on error
      setBeats([]);
    }
  };

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
            detectBeats(audioBuffer, trackMetadata.bpm || 120); // Use metadata BPM or default to 120
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

      // Add new regions for every 32nd beat
      beats.forEach((time, index) => {
        if (index % 32 === 0) {  // Only show every 32nd beat
          const nextBeat = beats[index + 32] || wavesurferRef.current?.getDuration() || time + 1;
          regionsPlugin.addRegion({
            start: time,
            end: nextBeat, // Region extends to the next 32nd beat
            color: regionIndex % 2 === 0 ? 'rgba(0, 0, 255, 0.1)' : 'rgba(0, 0, 255, 0.2)', // Transparent colors
            drag: false,
            resize: false,
            channelIdx: -1, // -1 means cover all channels
            minWidth: 0,
            maxWidth: 0,
            minHeight: 0,
            maxHeight: 0
          });
          regionIndex++;
        }
      });
    }
  }, [beats, gridColor]);

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