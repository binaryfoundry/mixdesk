declare module 'signalsmith-stretch' {
  interface TimeMapSegment {
    input: number;
    output: number;
    rate: number;
  }

  interface StretchNode extends AudioWorkletNode {
    inputTime: number;
    setState(options: { sample: { speed: number } }): void;
    setUpdateInterval(seconds: number, callback?: (time: number) => void): void;
    setTimeMap(segments: TimeMapSegment[]): void;
    schedule(options: { rate: number }): void;
  }

  function SignalsmithStretch(audioContext: AudioContext, options?: AudioWorkletNodeOptions): Promise<StretchNode>;
  export default SignalsmithStretch;
} 