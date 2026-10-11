import { useEffect, useRef, useState } from 'react';
import { backend } from './generated/backend';
import { useBackendAvailable } from './generated/react';
import type { WebParameter, WebParameterValues } from './generated/schema';

export type Parameters = Record<string, WebParameter>;

export interface ParameterControl
{
    parameters: Parameters;
    begin: (id: string) => void;
    set: (id: string, normalized: number) => void;
    end: (id: string) => void;
    // One whole gesture, for a click.
    toggle: (id: string, normalized: number) => void;
}

// The plugin's parameters by id: fetched once the bridge is up, then merged with
// every `values` event. A value the page is dragging keeps the page's own number,
// so a late echo never pulls the control back under the pointer.
export function useParameters(): ParameterControl
{
    const available = useBackendAvailable();
    const [parameters, setParameters] = useState<Parameters>({});
    const dragging = useRef<string | null>(null);

    useEffect(() =>
    {
        if (!available)
            return;

        void backend.getParameters().then(({ parameters: list }) =>
            setParameters(Object.fromEntries(list.map((p) => [p.id, p]))));

        return backend.on?.('values', ({ changed }: WebParameterValues) =>
            setParameters((previous) =>
            {
                const next = { ...previous };

                for (const change of changed)
                {
                    const current = next[change.id];

                    if (current == null)
                        continue;

                    const normalized = dragging.current === change.id
                        ? current.normalized
                        : change.normalized;

                    next[change.id] = { ...current, normalized, text: change.text };
                }

                return next;
            }));
    }, [available]);

    const setLocal = (id: string, normalized: number) =>
        setParameters((previous) => previous[id] == null
            ? previous
            : { ...previous, [id]: { ...previous[id], normalized } });

    return {
        parameters,
        begin: (id) =>
        {
            dragging.current = id;
            void backend.beginEdit({ id });
        },
        set: (id, normalized) =>
        {
            setLocal(id, normalized);
            void backend.edit({ id, normalized });
        },
        end: (id) =>
        {
            dragging.current = null;
            void backend.endEdit({ id });
        },
        toggle: (id, normalized) =>
        {
            setLocal(id, normalized);
            void backend.edit({ id, normalized });
        },
    };
}
