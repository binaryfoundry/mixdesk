import { Box, Slider, Typography } from '@mui/material';

interface TempoControlProps {
  tempo: number;
  onChange: (value: number | number[]) => void;
}

export function TempoControl({ tempo, onChange }: TempoControlProps) {
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
      <Typography variant="body1" sx={{ minWidth: '100px' }}>
        Global Tempo: {tempo}%
      </Typography>
      <Slider
        value={tempo}
        onChange={(e, v) => onChange(v)}
        min={90}
        max={150}
        step={1}
        sx={{ flex: 1 }}
      />
    </Box>
  );
} 