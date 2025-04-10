// Metronome.ts

export class Metronome {
  private audioContext: AudioContext;
  private workletNode!: AudioWorkletNode;
  private tickListeners: Array<(beatCount: number) => void> = [];
  private currentTempo: number;
  private lastTickTime: number = 0;
  private currentBeatCount: number = 0;

  constructor(private initialTempo: number = 120) {
    try {
      // Create AudioContext in a suspended state to avoid issues with browser autoplay policies
      this.audioContext = new (window.AudioContext || (window as any).webkitAudioContext)({ latencyHint: 'interactive' });
      this.audioContext.suspend(); // Start in suspended state
      this.currentTempo = initialTempo;
    } catch (error) {
      console.error('Error creating AudioContext:', error);
      throw new Error('Failed to initialize audio context. Please check your audio device and browser permissions.');
    }
  }

  /**
   * Initializes the metronome by loading the AudioWorklet module and creating the node.
   * Must be called before starting the metronome.
   */
  public async initialize(): Promise<void> {
    try {
      // Load the AudioWorklet module (make sure the path is correct).
      await this.audioContext.audioWorklet.addModule('/metronome-processor.js');

      // Create the AudioWorkletNode using the name registered in the processor.
      this.workletNode = new AudioWorkletNode(this.audioContext, 'metronome-processor');

      // Set the initial tempo.
      this.workletNode.parameters.get('tempo')!.value = this.currentTempo;

      // The worklet produces silent audio. Connecting it to the destination ensures it is processed.
      // No audible sound will result.
      this.workletNode.connect(this.audioContext.destination);

      // Listen for tick events from the processor.
      this.workletNode.port.onmessage = (event) => {
        if (event.data.type === 'tick') {
          this.lastTickTime = this.audioContext.currentTime;
          this.currentBeatCount = event.data.beatCount;
          this.tickListeners.forEach(callback => callback(this.currentBeatCount));
        }
      };
    } catch (error) {
      console.error('Error initializing metronome:', error);
      throw new Error('Failed to initialize metronome. Please check your audio device and browser permissions.');
    }
  }

  /**
   * Starts the metronome. The AudioContext is resumed if needed.
   */
  public async start(): Promise<void> {
    try {
      if (this.audioContext.state === 'suspended') {
        await this.audioContext.resume();
      }
    } catch (error) {
      console.error('Error starting metronome:', error);
      throw new Error('Failed to start metronome. Please check your audio device and browser permissions.');
    }
  }

  /**
   * Stops the metronome by suspending the AudioContext.
   */
  public async stop(): Promise<void> {
    if (this.audioContext.state === 'running') {
      await this.audioContext.suspend();
    }
  }

  /**
   * Register a callback to be invoked on each tick.
   * @param callback Function that receives the current beat count
   */
  public addTickListener(callback: (beatCount: number) => void): void {
    this.tickListeners.push(callback);
  }

  /**
   * Get the current tempo in BPM.
   * @returns The current tempo in beats per minute
   */
  public getTempo(): number {
    return this.currentTempo;
  }

  /**
   * Get the current beat count.
   * @returns The current beat count
   */
  public getBeatCount(): number {
    return this.currentBeatCount;
  }

  /**
   * Adjust the metronome tempo (in BPM). This change is applied smoothly,
   * and new tick intervals will be computed on the fly.
   * @param newTempo The new tempo in beats per minute (20-300 BPM)
   */
  public setTempo(newTempo: number): void {
    if (newTempo < 20 || newTempo > 300) {
      throw new Error('Tempo must be between 20 and 300 BPM');
    }
    this.currentTempo = newTempo;
    this.workletNode.parameters.get('tempo')!.value = newTempo;
  }

  /**
   * Get the time remaining until the next beat in seconds.
   * @returns The time in seconds until the next beat, or null if the metronome hasn't started yet
   */
  public getTimeUntilNextBeat(): number | null {
    if (this.lastTickTime === 0) {
      return null; // Metronome hasn't started yet
    }

    const beatInterval = 60 / this.currentTempo; // Time between beats in seconds
    const currentTime = this.audioContext.currentTime;
    const timeSinceLastTick = currentTime - this.lastTickTime;
    const timeUntilNextTick = beatInterval - (timeSinceLastTick % beatInterval);

    return timeUntilNextTick;
  }
}
