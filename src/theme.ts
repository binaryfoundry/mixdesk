import { createTheme } from '@mui/material/styles';

// Add Google Fonts link to the document head
const link = document.createElement('link');
link.rel = 'stylesheet';
link.href = 'https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;500;600&display=swap';
document.head.appendChild(link);

export const theme = createTheme({
  typography: {
    fontFamily: '"JetBrains Mono", "Fira Mono", monospace',
    fontWeightRegular: 400,
    fontWeightMedium: 500,
    fontWeightBold: 600,
    body1: {
      fontFamily: '"JetBrains Mono", "Fira Mono", monospace',
    },
    body2: {
      fontFamily: '"JetBrains Mono", "Fira Mono", monospace',
    },
    caption: {
      fontFamily: '"JetBrains Mono", "Fira Mono", monospace',
    },
    button: {
      fontFamily: '"JetBrains Mono", "Fira Mono", monospace',
    },
  },
}); 