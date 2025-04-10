export class Metronome {
  private audioContext: AudioContext;
  private tempo: number; // in Beats Per Minute (BPM)
  private nextTickTime: number;
  private schedulerTimerId: number | null = null;
  // How far ahead (in seconds) to schedule ticks.
  private scheduleAheadTime: number = 0.1;
  // Interval (in ms) for checking and scheduling upcoming ticks.
  private lookahead: number = 25;
  // Array of callbacks to be invoked on each tick.
  private tickListeners: Array<(beatNumber: number) => void> = [];
  private currentBeat: number = 0;

  constructor(initialTempo: number = 120) {
    // Create an AudioContext for high-resolution timing.
    this.audioContext = new (window.AudioContext || (window as any).webkitAudioContext)();
    this.tempo = initialTempo;
    // Initialize the next tick time to the current audio context time.
    this.nextTickTime = this.audioContext.currentTime;
  }

  /**
   * Start the metronome.
   */
  public start(): void {
    if (this.schedulerTimerId === null) {
      // Reset nextTickTime to current time for a fresh start.
      this.nextTickTime = this.audioContext.currentTime;
      this.currentBeat = 0;
      this.scheduler();
    }
  }

  /**
   * Stop the metronome.
   */
  public stop(): void {
    if (this.schedulerTimerId !== null) {
      clearTimeout(this.schedulerTimerId);
      this.schedulerTimerId = null;
    }
  }

  /**
   * Register a callback function that will be called on every tick.
   */
  public addTickListener(callback: (beatNumber: number) => void): void {
    this.tickListeners.push(callback);
  }

  /**
   * Update the tempo (BPM) for the metronome.
   * Future ticks will use the new tempo value.
   */
  public setTempo(newTempo: number): void {
    this.tempo = newTempo;
  }

  public getTempo(): number {
    return this.tempo;
  }

  /**
   * The scheduler function checks ahead of time and schedules tick events.
   */
  private scheduler = () => {
    // Schedule ticks as long as they fall within the scheduleAheadTime window.
    while (this.nextTickTime < this.audioContext.currentTime + this.scheduleAheadTime) {
      this.scheduleTick(this.nextTickTime);
      // Advance nextTickTime by the interval defined by the current tempo.
      this.nextTickTime += 60 / this.tempo;
    }
    // Set a timer to run the scheduler again.
    this.schedulerTimerId = window.setTimeout(this.scheduler, this.lookahead);
  };

  /**
   * Schedule a tick event at the given high-resolution time.
   * Because the metronome is virtual, we simply fire an event (or callback)
   * at the scheduled time.
   */
  private scheduleTick(scheduledTime: number): void {
    const delay = Math.max(scheduledTime - this.audioContext.currentTime, 0) * 1000;
    setTimeout(() => {
      this.currentBeat++;
      this.tickListeners.forEach(callback => callback(this.currentBeat));
    }, delay);
  }
}
