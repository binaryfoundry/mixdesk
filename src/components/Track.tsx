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
  const [zoom, setZoom] = useState(1);  // Zoom factor, 1 = normal, >1 = zoomed in
  const [offset, setOffset] = useState(0);  // Horizontal offset, 0 = start, 1 = end

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Set canvas dimensions
    canvas.width = canvas.offsetWidth;  // 4x resolution
    canvas.height = 120;

    // Clear canvas and reset context state
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.setTransform(1, 0, 0, 1, 0, 0);  // Reset transform matrix
    ctx.setLineDash([]);  // Reset line dash
    ctx.lineWidth = 1;  // Reset line width
    ctx.strokeStyle = '#4a9eff';  // Reset stroke style

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
    const amp = canvas.height / 2;

    const visibleSamples = data.length / zoom;
    const step = Math.ceil(visibleSamples / canvas.width);

    // Calculate visible range based on offset
    const visibleStart = Math.floor(offset * (data.length - visibleSamples));
    const visibleEnd = Math.min(data.length, visibleStart + visibleSamples);

    // Find the max amplitude in the visible window
    let visibleMax = 0;
    for (let i = visibleStart; i < visibleEnd; i++) {
      const absValue = Math.abs(data[i]);
      if (absValue > visibleMax) visibleMax = absValue;
    }

    // Avoid division by 0
    if (visibleMax === 0) visibleMax = 1;

    // Draw the normalized waveform
    ctx.beginPath();

    for (let i = 0; i < canvas.width; i++) {
      let max = 0;

      const start = Math.floor(visibleStart + i * step);
      const end = Math.min(start + step, data.length);

      for (let j = start; j < end; j++) {
        const absValue = Math.abs(data[j]);
        if (absValue > max) max = absValue;
      }

      const normalized = max / visibleMax; // Normalize to [0, 1] based on visible window
      const height = normalized * amp * 2;
      const y = amp - height / 2;

      ctx.moveTo(i, y);
      ctx.lineTo(i, y + height);
    }

    ctx.stroke();

    // Draw beat markers
    if (track.beats && track.beats.length > 0) {
      // Define alternating shades of grey
      const lightGrey = 'rgba(200, 200, 200, 0.3)';
      const darkGrey = 'rgba(150, 150, 150, 0.3)';

      // Draw beat rectangles
      for (let i = 0; i < track.beats.length - 1; i++) {
        const currentBeat = track.beats[i];
        const nextBeat = track.beats[i + 1];

        // Convert milliseconds to seconds and account for zoom and offset
        const currentBeatTime = currentBeat / 1000;
        const nextBeatTime = nextBeat / 1000;

        const currentBeatPosition = (currentBeatTime / track.duration) * data.length;
        const nextBeatPosition = (nextBeatTime / track.duration) * data.length;

        const x1 = ((currentBeatPosition - visibleStart) / visibleSamples) * canvas.width;
        const x2 = ((nextBeatPosition - visibleStart) / visibleSamples) * canvas.width;
        
        // Only draw if at least part of the rectangle is visible
        if (x2 > 0 && x1 < canvas.width) {
          // Use alternating colors
          ctx.fillStyle = i % 2 === 0 ? lightGrey : darkGrey;
          ctx.fillRect(x1, 0, x2 - x1, canvas.height);
        }
      }
    }
  }, [track.audioBuffer, track.beats, track.duration, zoom, offset]);

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
      overflow: 'hidden'
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
          minWidth: 0
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
        position: 'relative'
      }}>
        <canvas 
          ref={canvasRef} 
          style={{ 
            width: '100%', 
            height: '300px',
            backgroundColor: '#f5f5f5',
            borderRadius: '4px',
            display: 'block',
            position: 'relative'
          }} 
        />
      </Box>

      <Box sx={{
        display: 'flex',
        flexDirection: 'column',
        gap: 1,
        width: '100%'
      }}>
        <Box sx={{
          display: 'flex',
          alignItems: 'center',
          gap: 1,
          width: '100%'
        }}>
          <Typography variant="caption" color="text.secondary" sx={{ minWidth: '60px' }}>
            Zoom:
          </Typography>
          <Slider
            value={zoom}
            onChange={(e, v) => setZoom(v as number)}
            min={0.1}
            max={20}
            step={0.1}
            size="small"
            sx={{ flex: 1 }}
          />
        </Box>

        <Box sx={{
          display: 'flex',
          alignItems: 'center',
          gap: 1,
          width: '100%'
        }}>
          <Typography variant="caption" color="text.secondary" sx={{ minWidth: '60px' }}>
            Offset:
          </Typography>
          <Slider
            value={offset}
            onChange={(e, v) => setOffset(v as number)}
            min={0}
            max={1}
            step={0.001}
            size="small"
            sx={{ flex: 1 }}
          />
        </Box>
      </Box>
    </Box>
  );
} 