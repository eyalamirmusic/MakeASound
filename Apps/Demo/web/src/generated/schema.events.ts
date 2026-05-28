import type * as T from './schema';

export interface Events
{
    'ui': T.UIState;
    'audio': T.AudioControls;
    'midi': T.MidiLogEntry;
}

export type EventName = keyof Events;

export interface EventBus
{
    subscribe<K extends EventName>(
        name: K,
        handler: (payload: Events[K]) => void,
    ): () => void;
}
