import { Box, Button, Paper, Typography } from '@mui/material';
import UploadFileIcon from '@mui/icons-material/UploadFile';
import { useAudioPlayer } from '../hooks/useAudioPlayer';
import { TrackList } from './TrackList';
import { TempoControl } from './TempoControl';

export default function AudioPlayer() {
  const {
    tracks,
    globalTempo,
    handleFileUpload,
    handlePlayPause,
    handleVolumeChange,
    handleTempoChange
  } = useAudioPlayer();

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
        </Box>

        <TempoControl 
          tempo={globalTempo}
          onChange={handleTempoChange}
        />

        <TrackList
          tracks={tracks}
          onPlayPause={handlePlayPause}
          onVolumeChange={handleVolumeChange}
        />
      </Box>
    </Paper>
  );
} 