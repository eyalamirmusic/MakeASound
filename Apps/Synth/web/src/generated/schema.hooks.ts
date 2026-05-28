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
    initial: {"blockSizes":{"currentId":0,"items":[]},"devices":{"currentId":0,"items":[]},"midiPorts":{"items":[]},"sampleRates":{"currentId":0,"items":[]}},
});

export const useAudio = makeBridgeStore({
    backend,
    event: 'audio',
    fetch: backend.getAudio,
    shouldFetch: isBackendAvailable,
    initial: {"frequency":0,"gain":0,"note":-1,"playing":false,"velocity":0},
});

export const useMidi = makeNativeEvent({
    backend,
    event: 'midi',
    initial: {"text":""},
});
