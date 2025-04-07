import { Box } from '@mui/material';
import { Track as TrackView } from './Track';
import { Track as TrackType } from '../hooks/useAudioPlayer';

interface TrackListProps {
  tracks: TrackType[];
  onPlayPause: (trackId: string) => void;
  onVolumeChange: (trackId: string, value: number | number[]) => void;
}

export function TrackList({ tracks, onPlayPause, onVolumeChange }: TrackListProps) {
  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
      {tracks.map(track => (
        <TrackView
          key={track.id}
          track={track}
          onPlayPause={onPlayPause}
          onVolumeChange={onVolumeChange}
        />
      ))}
    </Box>
  );
} 