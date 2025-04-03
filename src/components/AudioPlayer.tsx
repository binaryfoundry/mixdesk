import { Box, Button, Paper } from '@mui/material';
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
        height: '100%'
      }}>
        <Box sx={{ 
          display: 'flex', 
          gap: 2,
          alignItems: 'center'
        }}>
          <Box sx={{ 
            display: 'flex', 
            gap: 2,
            alignItems: 'center',
            minWidth: '300px'
          }}>
            <input
              type="file"
              accept="audio/*"
              onChange={(e) => {
                const file = e.target.files?.[0];
                if (file) {
                  handleFileUpload(e);
                }
              }}
              style={{ display: 'none' }}
              id="audio-upload"
            />
            <label htmlFor="audio-upload">
              <Button
                variant="contained"
                component="span"
                startIcon={<UploadFileIcon />}
              >
                Upload Track
              </Button>
            </label>
          </Box>

          <Box sx={{ flex: 1 }}>
            <TempoControl 
              tempo={globalTempo} 
              onChange={handleTempoChange} 
            />
          </Box>
        </Box>

        <Box sx={{ flex: 1, overflow: 'auto' }}>
          <TrackList 
            tracks={tracks} 
            onPlayPause={handlePlayPause} 
            onVolumeChange={handleVolumeChange} 
          />
        </Box>
      </Box>
    </Paper>
  );
} 