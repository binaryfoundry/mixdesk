// Material-UI imports
import { Box, Button, Paper, Typography } from '@mui/material';
import UploadFileIcon from '@mui/icons-material/UploadFile';

// Local imports
import { useAudioPlayer } from '../hooks/useAudioPlayer';
import { TempoControl } from './TempoControl';
import { TrackList } from './TrackList';

export default function AudioPlayer() {
  const {
    tracks,
    handleFileUpload,
    handlePlayPause,
    handleVolumeChange,
    handleTempoChange,
    metronome,
    error
  } = useAudioPlayer();

  return (
    <Paper elevation={3} sx={{
      p: 2,
      width: '100%',
      height: '100%',
      borderRadius: 0,
      display: 'flex',
      flexDirection: 'column',
      boxSizing: 'border-box',
      overflow: 'hidden'
    }}>
      {error && (
        <Box sx={{
          p: 2,
          mb: 2,
          backgroundColor: 'error.light',
          color: 'error.contrastText',
          borderRadius: 1
        }}>
          <Typography variant="body2">{error}</Typography>
        </Box>
      )}
      <Box sx={{
        display: 'flex',
        flexDirection: 'column',
        gap: 2,
        height: '100%',
        width: '100%',
        boxSizing: 'border-box',
        overflow: 'hidden'
      }}>
        <Box sx={{
          display: 'flex',
          gap: 2,
          alignItems: 'center',
          width: '100%',
          boxSizing: 'border-box',
          overflow: 'hidden',
          flexShrink: 0
        }}>
          <Box sx={{
            display: 'flex',
            gap: 2,
            alignItems: 'center',
            minWidth: '300px',
            flexShrink: 0
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

          <Box sx={{
            flex: 1,
            minWidth: 0
          }}>
            <TempoControl
              metronome={metronome}
              onChange={handleTempoChange}
            />
          </Box>
        </Box>

        <Box sx={{
          flex: 1,
          overflow: 'auto',
          width: '100%',
          boxSizing: 'border-box',
          minHeight: 0  // Allow flex container to shrink
        }}>
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