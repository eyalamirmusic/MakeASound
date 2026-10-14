// Generated. Do not edit by hand.
//
// Pre-wired React hooks for every registered bridge event.
// Initial values come from toJSON(T{}).

import { backend, isBackendAvailable } from './backend';
import { makeKeyedStore, makeNativeEvent } from './react';

export const useValues = makeNativeEvent({
    backend,
    event: 'values',
    initial: {"changed":[]},
});

const parameterValuesStore = makeKeyedStore({
    backend,
    event: 'parameterValues',
    fetch: backend.getParameterValues,
    shouldFetch: isBackendAvailable,
    initial: {"items":[]},
    getItems: (s) => s.items,
    getKey:   (i) => i.id,
});
export const useParameterValues = parameterValuesStore.useAll;
export const useWebParameterValueIds = parameterValuesStore.useIds;
export const useWebParameterValue = parameterValuesStore.useItem;
