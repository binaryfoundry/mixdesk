// Material-UI imports
import { Box } from '@mui/material';
import { ThemeProvider } from '@mui/material/styles';

// Local imports
import AudioPlayer from './components/AudioPlayer';
import { theme } from './theme';

function App() {
  return (
    <ThemeProvider theme={theme}>
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
    </ThemeProvider>
  );
}

export default App;
