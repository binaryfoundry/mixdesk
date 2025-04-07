import { Box, Button, Slider, Typography } from '@mui/material';
import PlayArrowIcon from '@mui/icons-material/PlayArrow';
import PauseIcon from '@mui/icons-material/Pause';
import VolumeUpIcon from '@mui/icons-material/VolumeUp';
import { useEffect, useRef, useState, useCallback } from 'react';
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
  const [clickedBeatIndex, setClickedBeatIndex] = useState<number | null>(null);

  const drawWaveform = useCallback(() => {
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

    // Find the global max amplitude
    let globalMax = 0;
    for (let i = 0; i < data.length; i++) {
      const absValue = Math.abs(data[i]);
      if (absValue > globalMax) globalMax = absValue;
    }

    // Avoid division by 0
    if (globalMax === 0) globalMax = 1;

    // Calculate the playback position in pixels
    const playbackPosition = track.currentTime / track.duration;
    const playbackPixel = Math.floor(((playbackPosition * data.length - visibleStart) / visibleSamples) * canvas.width);

    // Draw the normalized waveform in two parts - played and unplayed
    for (let i = 0; i < canvas.width; i++) {
      let max = 0;

      const start = Math.floor(visibleStart + i * step);
      const end = Math.min(start + step, data.length);

      for (let j = start; j < end; j++) {
        const absValue = Math.abs(data[j]);
        if (absValue > max) max = absValue;
      }

      const normalized = max / globalMax; // Normalize to [0, 1] based on global maximum
      const height = normalized * amp * 2;
      const y = amp - height / 2;

      // Draw the line segment in the appropriate color based on playback position
      ctx.beginPath();
      ctx.strokeStyle = i <= playbackPixel ? '#2a7edf' : '#4a9eff';
      ctx.moveTo(i, y);
      ctx.lineTo(i, y + height);
      ctx.stroke();
    }

    // Draw beat markers
    if (track.beats && track.beats.length > 0) {
      // Define alternating shades of grey
      const lightGrey = 'rgba(200, 200, 200, 0.0)';
      const darkGrey = 'rgba(200, 200, 200, 0.2)';
      const highlightColor = 'rgba(255, 223, 0, 0.3)'; // Yellow highlight for clicked beat

      // Draw beat rectangles and downbeat lines
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
          // Use highlight color for clicked beat, otherwise use alternating colors
          if (i === clickedBeatIndex) {
            ctx.fillStyle = highlightColor;
          } else {
            ctx.fillStyle = i % 2 === 0 ? darkGrey : lightGrey;
          }
          ctx.fillRect(x1, 0, x2 - x1, canvas.height);

          // Draw dashed line at the start of each bar
          if (i % 4 === track.downbeatOffset) {
            ctx.setLineDash([2, 2]); // Set dash pattern
            ctx.strokeStyle = '#888888';
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(x1, 0);
            ctx.lineTo(x1, canvas.height);
            ctx.stroke();
            ctx.setLineDash([]); // Reset dash pattern
          }
        }
      }
    }
  }, [track.audioBuffer, track.beats, track.duration, track.downbeatOffset, track.currentTime, zoom, offset, clickedBeatIndex]);

  const handleCanvasClick = (event: React.MouseEvent<HTMLCanvasElement>) => {
    const canvas = canvasRef.current;
    if (!canvas || !track.audioBuffer) return;

    // Get click position relative to canvas
    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const clickPosition = x / canvas.width;

    // Account for zoom and offset
    const visibleSamples = track.audioBuffer.length / zoom;
    const visibleStart = Math.floor(offset * (track.audioBuffer.length - visibleSamples));
    
    // Calculate the time at click position
    const sampleAtClick = visibleStart + (visibleSamples * clickPosition);
    const timeAtClick = sampleAtClick / track.audioBuffer.sampleRate;
    
    // Find nearest beat
    if (track.beats && track.beats.length > 0) {
      // Convert beats from milliseconds to seconds
      const beatTimesInSeconds = track.beats.map(beat => beat / 1000);
      
      // Find the beat that starts before the clicked time
      let selectedBeatIndex = 0;
      for (let i = 0; i < beatTimesInSeconds.length; i++) {
        if (beatTimesInSeconds[i] <= timeAtClick) {
          selectedBeatIndex = i;
        } else {
          break;
        }
      }

      // Check if this beat is a bar start (downbeat)
      const isDownbeat = selectedBeatIndex % 4 === track.downbeatOffset;

      // Update clicked beat index
      setClickedBeatIndex(selectedBeatIndex);

      // Get the beat time
      const beatTime = beatTimesInSeconds[selectedBeatIndex];

      // Update the track's current time
      track.currentTime = beatTime;

      // If the track is playing, we need to restart playback from the new position
      if (track.isPlaying) {
        onPlayPause(track.id); // This will stop and restart playback at the new position
      } else {
        // If not playing, just update the current time
        onPlayPause(track.id); // This will start playback from the new position
      }

      console.log('Clicked at time:', timeAtClick);
      console.log('Selected beat:', {
        index: selectedBeatIndex,
        time: beatTime,
        isDownbeat,
        nextBeatTime: beatTimesInSeconds[selectedBeatIndex + 1]
      });
    }
  };

  useEffect(() => {
    drawWaveform();
  }, [drawWaveform]);

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
          onClick={handleCanvasClick}
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
            min={1.0}
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