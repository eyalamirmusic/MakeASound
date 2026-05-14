import { useEffect, useState } from 'react';

declare global
{
    interface Window
    {
        eacp: {
            invoke<Res = unknown>(command: string, payload?: unknown): Promise<Res>;
            on<P = unknown>(event: string, handler: (payload: P) => void): () => void;
        };
    }
}

export const invoke = <Res = unknown>(command: string, payload?: unknown) =>
    window.eacp.invoke<Res>(command, payload);

export const onEvent = <P = unknown>(event: string, handler: (payload: P) => void) =>
    window.eacp.on<P>(event, handler);

export function useBridgeEvent<P>(event: string, handler: (payload: P) => void): void
{
    useEffect(() => onEvent<P>(event, handler), [event, handler]);
}

export function useInitialState<S>(command: string): S | null
{
    const [state, setState] = useState<S | null>(null);

    useEffect(() =>
    {
        let mounted = true;
        invoke<S>(command).then((value) => { if (mounted) setState(value); });
        return () => { mounted = false; };
    }, [command]);

    return state;
}
