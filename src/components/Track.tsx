import { Box, Button, Slider, Typography } from '@mui/material';
import PlayArrowIcon from '@mui/icons-material/PlayArrow';
import PauseIcon from '@mui/icons-material/Pause';
import VolumeUpIcon from '@mui/icons-material/VolumeUp';
import { useEffect, useRef, useState } from 'react';

interface TrackProps {
  track: {
    id: string;
    file: File;
    metadata: {
      title: string;
      key: string;
      bpm: number;
    };
    isPlaying: boolean;
    volume: number;
    originalTempo: number;
    audioBuffer: AudioBuffer | null;
  };
  onPlayPause: (trackId: string) => void;
  onVolumeChange: (trackId: string, value: number | number[]) => void;
}

export function Track({ track, onPlayPause, onVolumeChange }: TrackProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [isLoading, setIsLoading] = useState(true);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Set canvas dimensions
    canvas.width = canvas.offsetWidth;
    canvas.height = 60;

    // Clear canvas
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    if (!track.audioBuffer) {
      // Draw loading state
      ctx.fillStyle = '#f5f5f5';
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = '#4a9eff';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.font = '14px Arial';
      ctx.fillText('Loading waveform...', canvas.width / 2, canvas.height / 2);
      setIsLoading(true);
      return;
    }

    setIsLoading(false);

    // Get audio data
    const data = track.audioBuffer.getChannelData(0);
    const step = Math.ceil(data.length / canvas.width);
    const amp = canvas.height / 2;

    // Draw waveform
    ctx.beginPath();
    ctx.strokeStyle = '#4a9eff';
    ctx.lineWidth = 2;

    for (let i = 0; i < canvas.width; i++) {
      let max = 0;
      const start = i * step;
      const end = Math.min(start + step, data.length);

      // Find max absolute value in this segment
      for (let j = start; j < end; j++) {
        const absValue = Math.abs(data[j]);
        if (absValue > max) max = absValue;
      }

      // Use square root scaling
      const sqrtValue = Math.sqrt(max);
      const height = sqrtValue * amp * 2; // Multiply by 2 to make it more visible
      const y = amp - height / 2;
      
      ctx.moveTo(i, y);
      ctx.lineTo(i, y + height);
    }

    ctx.stroke();
  }, [track.audioBuffer]);

  return (
    <Box sx={{ 
      display: 'flex', 
      flexDirection: 'column',
      gap: 2,
      p: 2,
      border: '1px solid',
      borderColor: 'divider',
      borderRadius: 1
    }}>
      <Box sx={{ 
        display: 'flex', 
        alignItems: 'center', 
        gap: 2,
        flexWrap: 'wrap',
      }}>
        <Box sx={{ display: 'flex', flexDirection: 'column', flex: 1 }}>
          <Typography variant="body2" color="text.secondary">
            {track.metadata.title || track.file.name}
          </Typography>
          <Typography variant="caption" color="text.secondary">
            Key: {track.metadata.key} | BPM: {Math.round(track.originalTempo)}
          </Typography>
        </Box>

        <Button
          variant="contained"
          onClick={() => onPlayPause(track.id)}
          startIcon={track.isPlaying ? <PauseIcon /> : <PlayArrowIcon />}
          sx={{ minWidth: '120px' }}
          disabled={isLoading}
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
            onChange={(e, v) => onVolumeChange(track.id, v)}
            min={0}
            max={1}
            step={0.01}
            size="small"
            disabled={isLoading}
          />
        </Box>
      </Box>

      <Box sx={{ width: '100%' }}>
        <canvas 
          ref={canvasRef} 
          style={{ 
            width: '100%', 
            height: '60px',
            backgroundColor: '#f5f5f5',
            borderRadius: '4px'
          }} 
        />
      </Box>
    </Box>
  );
} 