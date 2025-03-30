import { CssBaseline, ThemeProvider, createTheme } from '@mui/material';
import AudioPlayer from './components/AudioPlayer';

const theme = createTheme({
  palette: {
    mode: 'light',
    primary: {
      main: '#4a9eff',
    },
    secondary: {
      main: '#2c5282',
    },
  },
});

function App() {
  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <div style={{ 
        height: '100vh', 
        backgroundColor: '#f5f5f5',
        display: 'flex',
        flexDirection: 'column',
        overflow: 'hidden'
      }}>
        <AudioPlayer />
      </div>
    </ThemeProvider>
  );
}

export default App;
