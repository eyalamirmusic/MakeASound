import type * as T from './schema';

export type Invoke = (command: string, payload: unknown) => Promise<unknown>;

export function makeBackend(invoke: Invoke)
{
    return {
        getParameters: (): Promise<T.WebParameters> =>
            invoke('getParameters', {}) as Promise<T.WebParameters>,
        beginEdit: (req: T.WebParameterId): Promise<void> =>
            invoke('beginEdit', req) as Promise<void>,
        edit: (req: T.WebParameterEdit): Promise<void> =>
            invoke('edit', req) as Promise<void>,
        endEdit: (req: T.WebParameterId): Promise<void> =>
            invoke('endEdit', req) as Promise<void>,
    };
}
