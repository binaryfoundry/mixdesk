// Material-UI imports
import { Box, Slider, Typography } from '@mui/material';
import { useEffect, useState } from 'react';
import { METRONOME_BEAT_EVENT } from '../hooks/useAudioPlayer';

interface TempoControlProps {
  tempo: number;
  onChange: (value: number | number[]) => void;
  metronomeEmitter: EventTarget;
}

export function TempoControl({ tempo, onChange, metronomeEmitter }: TempoControlProps) {
  const [activeBeat, setActiveBeat] = useState<number | null>(null);

  useEffect(() => {
    const handleBeat = (event: Event) => {
      const beatEvent = event as CustomEvent;
      setActiveBeat(beatEvent.detail.beatNumber);
      // Reset the active beat after a short delay for the visual flash effect
      setTimeout(() => setActiveBeat(null), 100);
    };

    metronomeEmitter.addEventListener(METRONOME_BEAT_EVENT, handleBeat);
    return () => {
      metronomeEmitter.removeEventListener(METRONOME_BEAT_EVENT, handleBeat);
    };
  }, [metronomeEmitter]);

  return (
    <Box sx={{
      display: 'flex',
      alignItems: 'center',
      gap: 2,
      p: 2,
      border: '1px solid',
      borderColor: 'divider',
      borderRadius: 1,
      mb: 2
    }}>
      <Box sx={{ 
        display: 'flex',
        alignItems: 'center',
        gap: 2,
        minWidth: '160px',
        whiteSpace: 'nowrap'
      }}>
        <Typography variant="body1">
          Global Tempo: {tempo.toFixed(1)}
        </Typography>
        <Box sx={{ display: 'flex', gap: 1 }}>
          {[1, 2, 3, 4].map((beat) => (
            <Box
              key={beat}
              sx={{
                width: 12,
                height: 12,
                borderRadius: '50%',
                backgroundColor: activeBeat === beat ? '#4a9eff' : '#e0e0e0',
                transition: 'background-color 0.1s ease',
                // Make the first beat (downbeat) slightly larger
                ...(beat === 1 && {
                  width: 14,
                  height: 14,
                  backgroundColor: activeBeat === 1 ? '#2979ff' : '#bdbdbd'
                })
              }}
            />
          ))}
        </Box>
      </Box>
      <Slider
        value={tempo}
        onChange={(e, v) => onChange(v)}
        min={90}
        max={150}
        step={0.1}
        sx={{ flex: 1 }}
      />
    </Box>
  );
}