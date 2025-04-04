import { Box, Button, Slider, Typography } from '@mui/material';
import PlayArrowIcon from '@mui/icons-material/PlayArrow';
import PauseIcon from '@mui/icons-material/Pause';
import VolumeUpIcon from '@mui/icons-material/VolumeUp';
import { useEffect, useRef, useState } from 'react';
import { Track as TrackType } from '../hooks/useAudioPlayer';

interface TrackProps {
  track: TrackType;
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
    canvas.width = canvas.offsetWidth * 4;  // Quadruple the width for higher resolution
    canvas.height = 120;

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
    const hscale = 1;

    // Draw waveform
    ctx.beginPath();
    ctx.strokeStyle = '#4a9eff';
    ctx.lineWidth = 2;

    for (let i = 0; i < canvas.width; i++) {
      let max = 0;
      const start = Math.floor(i) * step;
      const end = Math.min(start + step, data.length);

      // Find max absolute value in this segment
      for (let j = start; j < end; j++) {
        const absValue = Math.abs(data[j / hscale]);
        const squaredValue = absValue * absValue;
        if (squaredValue > max) max = squaredValue;
      }

      // Use squared value
      const height = max * amp * 2; // Multiply by 2 to make it more visible
      const y = amp - height / 2;
      
      ctx.moveTo(i, y);
      ctx.lineTo(i, y + height);
    }

    ctx.stroke();

    // Draw beat markers
    if (track.beats && track.beats.length > 0) {
      console.log('Drawing beats:', track.beats);
      ctx.beginPath();
      ctx.strokeStyle = '#000000';
      ctx.lineWidth = 2;
      ctx.setLineDash([2, 2]);

      track.beats.forEach(beat => {
        // Convert milliseconds to seconds and account for 4x resolution
        const startX = (beat / 1000 / track.duration) * hscale * canvas.width;
        //const endX = (phrase.endTime / 1000 / track.duration) * canvas.width;

        console.log('Drawing beats:', {
          startX,
          duration: track.duration,
          canvasWidth: canvas.width,
          offsetWidth: canvas.offsetWidth
        });

        // Draw start line
        ctx.moveTo(startX, 0);
        ctx.lineTo(startX, canvas.height);
      });

      ctx.stroke();
      ctx.setLineDash([]);
    }
  }, [track.audioBuffer, track.phrases, track.duration, track.beats]);

  return (
    <Box sx={{ 
      display: 'flex', 
      flexDirection: 'column',
      gap: 2,
      p: 2,
      border: '1px solid',
      borderColor: 'divider',
      borderRadius: 1,
      width: '100%',
      boxSizing: 'border-box',
      overflow: 'hidden'  // Prevent any overflow
    }}>
      <Box sx={{ 
        display: 'flex', 
        alignItems: 'center', 
        gap: 2,
        flexWrap: 'wrap',
        width: '100%'
      }}>
        <Box sx={{ 
          display: 'flex', 
          flexDirection: 'column', 
          flex: 1,
          minWidth: 0  // Allow text to shrink
        }}>
          <Typography variant="body2" color="text.secondary" noWrap>
            {track.metadata.title || track.file.name}
          </Typography>
          <Typography variant="caption" color="text.secondary" noWrap>
            Key: {track.metadata.key} | BPM: {Math.round(track.originalTempo)}
          </Typography>
        </Box>

        <Button
          variant="contained"
          onClick={() => onPlayPause(track.id)}
          startIcon={track.isPlaying ? <PauseIcon /> : <PlayArrowIcon />}
          sx={{ minWidth: '120px', flexShrink: 0 }}
          disabled={isLoading}
        >
          {track.isPlaying ? 'Pause' : 'Play'}
        </Button>

        <Box sx={{ 
          display: 'flex', 
          alignItems: 'center', 
          gap: 1, 
          width: { xs: '100%', sm: '200px' },
          minWidth: '200px',
          flexShrink: 0
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

      <Box sx={{ 
        width: '100%',
        overflow: 'hidden',
        position: 'relative'  // Create a new stacking context
      }}>
        <canvas 
          ref={canvasRef} 
          style={{ 
            width: '100%', 
            height: '120px',  // Double the height
            backgroundColor: '#f5f5f5',
            borderRadius: '4px',
            display: 'block',
            position: 'relative'
          }} 
        />
      </Box>
    </Box>
  );
} 