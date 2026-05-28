// Generated. Do not edit by hand.
//
// Pre-wired React hooks for every registered bridge event.
// Keyed states get useXxx / useXxxIds / useXxxItem; plain
// states get useXxx; push-only events get useXxx via
// makeNativeEvent. Initial values come from toJSON(T{}).

import { backend, isBackendAvailable } from './backend';
import { makeBridgeStore, makeNativeEvent } from './react';

export const useUi = makeBridgeStore({
    backend,
    event: 'ui',
    fetch: backend.getUi,
    shouldFetch: isBackendAvailable,
    initial: {"blockSize":0,"devices":{"currentId":0,"items":[]},"midiPorts":{"items":[]},"sampleRates":{"currentId":0,"items":[]}},
});

export const useAudio = makeBridgeStore({
    backend,
    event: 'audio',
    fetch: backend.getAudio,
    shouldFetch: isBackendAvailable,
    initial: {"gain":0,"playing":false},
});

export const useMidi = makeNativeEvent({
    backend,
    event: 'midi',
    initial: {"text":""},
});
