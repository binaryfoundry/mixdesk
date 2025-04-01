import { Box, Button, Slider, Typography } from '@mui/material';
import PlayArrowIcon from '@mui/icons-material/PlayArrow';
import PauseIcon from '@mui/icons-material/Pause';
import VolumeUpIcon from '@mui/icons-material/VolumeUp';

interface Track {
  id: string;
  file: File;
  metadata: {
    title: string;
    key: string;
    bpm: number;
  };
  isPlaying: boolean;
  volume: number;
  bpm: number;
}

interface TrackListProps {
  tracks: Track[];
  onPlayPause: (trackId: string) => void;
  onVolumeChange: (trackId: string, value: number | number[]) => void;
}

export function TrackList({ tracks, onPlayPause, onVolumeChange }: TrackListProps) {
  return (
    <>
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
              Key: {track.metadata.key} | BPM: {Math.round(track.bpm)}
            </Typography>
          </Box>

          <Button
            variant="contained"
            onClick={() => onPlayPause(track.id)}
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
              onChange={(e, v) => onVolumeChange(track.id, v)}
              min={0}
              max={1}
              step={0.01}
              size="small"
            />
          </Box>
        </Box>
      ))}
    </>
  );
} 