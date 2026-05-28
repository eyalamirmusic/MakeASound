import type * as T from './schema';

export type Handlers = {
    getUi(): T.UIState | Promise<T.UIState>;
    getAudio(): T.AudioControls | Promise<T.AudioControls>;
    setPlaying(req: T.bool): void | Promise<void>;
    setGain(req: T.double): void | Promise<void>;
    setSampleRate(req: T.int): void | Promise<void>;
    setBlockSize(req: T.int): void | Promise<void>;
    setDevice(req: T.int): void | Promise<void>;
    midiPortToggle(req: T.MidiPortToggleRequest): void | Promise<void>;
};

export class UnknownCommandError extends Error
{
    httpStatus = 404;
    constructor(command: string)
    {
        super(`Unknown command: ${command}`);
    }
}

export async function dispatch(handlers: Handlers, command: string, payload: unknown): Promise<unknown>
{
    switch (command)
    {
        case 'getUi': return await handlers.getUi();
        case 'getAudio': return await handlers.getAudio();
        case 'setPlaying': return await handlers.setPlaying(payload as T.bool);
        case 'setGain': return await handlers.setGain(payload as T.double);
        case 'setSampleRate': return await handlers.setSampleRate(payload as T.int);
        case 'setBlockSize': return await handlers.setBlockSize(payload as T.int);
        case 'setDevice': return await handlers.setDevice(payload as T.int);
        case 'midiPortToggle': return await handlers.midiPortToggle(payload as T.MidiPortToggleRequest);
        default: throw new UnknownCommandError(command);
    }
}
