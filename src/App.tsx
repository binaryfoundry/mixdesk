import { Box } from '@mui/material';
import AudioPlayer from './components/AudioPlayer';

function App() {
  return (
    <Box sx={{ 
      width: '100%',
      height: '100%',
      display: 'flex',
      flexDirection: 'column',
      padding: 4,
      backgroundColor: '#f5f5f5'
    }}>
      <AudioPlayer />
    </Box>
  );
}

export default App;
