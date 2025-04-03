import { Box } from '@mui/material';
import AudioPlayer from './components/AudioPlayer';

function App() {
  return (
    <Box sx={{ 
      position: 'fixed',
      top: 0,
      left: 0,
      right: 0,
      bottom: 0,
      display: 'flex',
      flexDirection: 'column',
      overflow: 'hidden'
    }}>
      <AudioPlayer />
    </Box>
  );
}

export default App;
