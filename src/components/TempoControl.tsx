// Material-UI imports
import { Box, Slider, Typography } from '@mui/material';
import { useEffect, useState, useRef } from 'react';
import { Metronome } from '../Metronome';

interface TempoControlProps {
  onChange: (value: number | number[]) => void;
  metronome: Metronome;
}

export function TempoControl({ onChange, metronome }: TempoControlProps) {
  const [currentBeat, setCurrentBeat] = useState<number>(4);
  const [isDragging, setIsDragging] = useState(false);
  const [sliderValue, setSliderValue] = useState(metronome?.getTempo() || 120);
  const tickListenerAdded = useRef(false);

  // Update local state when metronome tempo changes
  useEffect(() => {
    if (metronome) {
      setSliderValue(metronome.getTempo());
    }
  }, [metronome?.getTempo()]);

  // Set up metronome tick listener only once
  useEffect(() => {
    if (!metronome || tickListenerAdded.current) return;

    const handleTick = (beatNumber: number) => {
      // Update the visual beat indicator (1-4)
      setCurrentBeat((beatNumber % 4) + 1);
    };

    metronome.addTickListener(handleTick);
    tickListenerAdded.current = true;
  }, [metronome]);

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
        whiteSpace: 'nowrap',
        flexShrink: 0
      }}>
        <Typography variant="body1">
          Global Tempo: {metronome?.getTempo()?.toFixed(1) || '120.0'}
        </Typography>
        <Box sx={{ display: 'flex', gap: 1 }}>
          {[1, 2, 3, 4].map((beat) => (
            <Box
              key={beat}
              sx={{
                width: 12,
                height: 12,
                borderRadius: '50%',
                backgroundColor: beat === currentBeat ? '#4a9eff' : '#e0e0e0',
                transition: 'background-color 0.1s ease',
                // Make the first beat (downbeat) slightly larger and brighter when active
                ...(beat === 1 && {
                  width: 14,
                  height: 14,
                  backgroundColor: beat === currentBeat ? '#2979ff' : '#bdbdbd'
                })
              }}
            />
          ))}
        </Box>
      </Box>
      <Box sx={{ flex: 1, minWidth: 0 }}>
        <Slider
          value={sliderValue}
          onChange={(_, value) => {
            const newTempo = value as number;
            setSliderValue(newTempo);
            metronome?.setTempo(newTempo);
            onChange(value);
          }}
          min={90}
          max={150}
          step={0.1}
          onMouseDown={() => setIsDragging(true)}
          onMouseUp={() => setIsDragging(false)}
        />
      </Box>
    </Box>
  );
}