import type * as T from './schema';

export interface ServerEvents
{
    ui: T.UIState;
    audio: T.AudioControls;
    midi: T.MidiLogEntry;
}
