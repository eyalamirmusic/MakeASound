import type * as T from './schema';

export type Invoke = (command: string, payload: unknown) => Promise<unknown>;

export function makeBackend(invoke: Invoke)
{
    return {
        getUi: (): Promise<T.UIState> =>
            invoke('getUi', {}) as Promise<T.UIState>,
        getAudio: (): Promise<T.AudioControls> =>
            invoke('getAudio', {}) as Promise<T.AudioControls>,
        setGain: (req: T.double): Promise<void> =>
            invoke('setGain', req) as Promise<void>,
        setSampleRate: (req: T.int): Promise<void> =>
            invoke('setSampleRate', req) as Promise<void>,
        setBlockSize: (req: T.int): Promise<void> =>
            invoke('setBlockSize', req) as Promise<void>,
        setDevice: (req: T.int): Promise<void> =>
            invoke('setDevice', req) as Promise<void>,
        midiPortToggle: (req: T.MidiPortToggleRequest): Promise<void> =>
            invoke('midiPortToggle', req) as Promise<void>,
        allNotesOff: (): Promise<void> =>
            invoke('allNotesOff', {}) as Promise<void>,
    };
}
