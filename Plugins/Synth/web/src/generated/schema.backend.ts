import type * as T from './schema';

export type Invoke = (command: string, payload: unknown) => Promise<unknown>;

export function makeBackend(invoke: Invoke)
{
    return {
        getParameters: (): Promise<T.WebParameters> =>
            invoke('getParameters', {}) as Promise<T.WebParameters>,
        getParameterValues: (): Promise<T.WebParameterSnapshot> =>
            invoke('getParameterValues', {}) as Promise<T.WebParameterSnapshot>,
        beginEdit: (req: T.WebParameterId): Promise<void> =>
            invoke('beginEdit', req) as Promise<void>,
        edit: (req: T.WebParameterEdit): Promise<void> =>
            invoke('edit', req) as Promise<void>,
        endEdit: (req: T.WebParameterId): Promise<void> =>
            invoke('endEdit', req) as Promise<void>,
        editText: (req: T.WebParameterText): Promise<void> =>
            invoke('editText', req) as Promise<void>,
        resetParam: (req: T.WebParameterId): Promise<void> =>
            invoke('resetParam', req) as Promise<void>,
        resetAll: (): Promise<void> =>
            invoke('resetAll', {}) as Promise<void>,
    };
}
