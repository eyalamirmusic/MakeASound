import type * as T from './schema';

export type Invoke = (command: string, payload: unknown) => Promise<unknown>;

export function makeBackend(invoke: Invoke)
{
    return {
        getUi: (): Promise<T.UIState> =>
            invoke('getUi', {}) as Promise<T.UIState>,
        getAudio: (): Promise<T.AudioControls> =>
            invoke('getAudio', {}) as Promise<T.AudioControls>,
        setPlaying: (req: T.bool): Promise<void> =>
            invoke('setPlaying', req) as Promise<void>,
        setGain: (req: T.double): Promise<void> =>
            invoke('setGain', req) as Promise<void>,
        setSampleRate: (req: T.int): Promise<void> =>
            invoke('setSampleRate', req) as Promise<void>,
        setBlockSize: (req: T.int): Promise<void> =>
            invoke('setBlockSize', req) as Promise<void>,
        setDevice: (req: T.int): Promise<void> =>
            invoke('setDevice', req) as Promise<void>,
        setInputDevice: (req: T.int): Promise<void> =>
            invoke('setInputDevice', req) as Promise<void>,
        setOutputChannels: (req: T.int): Promise<void> =>
            invoke('setOutputChannels', req) as Promise<void>,
        setInputChannels: (req: T.int): Promise<void> =>
            invoke('setInputChannels', req) as Promise<void>,
        midiPortToggle: (req: T.MidiPortToggleRequest): Promise<void> =>
            invoke('midiPortToggle', req) as Promise<void>,
    };
}
