import { useEffect, useState } from 'react';
import { backend } from './generated/backend';
import { useBackendAvailable } from './generated/react';
import { useWebParameterValue } from './generated/hooks';
import type { WebParameter, WebParameterValue } from './generated/schema';

export type Schema = Record<string, WebParameter>;

// The plugin's parameters by id, as they were when the bridge came up: names,
// steps, choices and defaults. Live values come from useValue.
export function useSchema(): Schema
{
    const available = useBackendAvailable();
    const [schema, setSchema] = useState<Schema>({});

    useEffect(() =>
    {
        if (!available)
            return;

        void backend.getParameters().then(({ parameters: list }) =>
            setSchema(Object.fromEntries(list.map((p) => [p.id, p]))));
    }, [available]);

    return schema;
}

// One parameter's live value, from the keyed `parameterValues` store: a control
// re-renders only when its own value moves. Before the first event it is the
// value the schema was fetched with.
export function useValue(parameter: WebParameter): WebParameterValue
{
    return useWebParameterValue(parameter.id) ?? parameter;
}

// Every edit the page makes. begin, set and end are one drag; the rest are each
// a whole gesture the plugin brackets itself.
export const edits = {
    begin: (id: string) => void backend.beginEdit({ id }),
    set: (id: string, normalized: number) => void backend.edit({ id, normalized }),
    end: (id: string) => void backend.endEdit({ id }),
    pick: (id: string, normalized: number) => void backend.edit({ id, normalized }),
    type: (id: string, text: string) => void backend.editText({ id, text }),
    reset: (id: string) => void backend.resetParam({ id }),
    resetAll: () => void backend.resetAll(),
};
