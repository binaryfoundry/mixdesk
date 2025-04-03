import { Box } from '@mui/material';
import { Track } from './Track';

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
  audioBuffer: AudioBuffer;
}

interface TrackListProps {
  tracks: Track[];
  onPlayPause: (trackId: string) => void;
  onVolumeChange: (trackId: string, value: number | number[]) => void;
}

export function TrackList({ tracks, onPlayPause, onVolumeChange }: TrackListProps) {
  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
      {tracks.map(track => (
        <Track
          key={track.id}
          track={track}
          onPlayPause={onPlayPause}
          onVolumeChange={onVolumeChange}
        />
      ))}
    </Box>
  );
} 