import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react-swc'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  build: {
    rollupOptions: {
      input: {
        main: 'index.html',
        'metronome-processor': 'src/metronome-processor.ts'
      },
      output: {
        entryFileNames: (chunkInfo) => {
          return chunkInfo.name === 'metronome-processor' 
            ? 'metronome-processor.js'
            : '[name]-[hash].js';
        }
      }
    }
  }
})