import { Box } from '@mui/material';
import { Track as TrackComponent } from './Track';
import { Track } from '../hooks/useAudioPlayer';

interface TrackListProps {
  tracks: Track[];
  onPlayPause: (trackId: string) => void;
  onVolumeChange: (trackId: string, value: number | number[]) => void;
}

export function TrackList({ tracks, onPlayPause, onVolumeChange }: TrackListProps) {
  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
      {tracks.map(track => (
        <TrackComponent
          key={track.id}
          track={track}
          onPlayPause={onPlayPause}
          onVolumeChange={onVolumeChange}
        />
      ))}
    </Box>
  );
} 