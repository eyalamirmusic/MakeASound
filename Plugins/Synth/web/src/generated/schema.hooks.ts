// Generated. Do not edit by hand.
//
// Pre-wired React hooks for every registered bridge event.
// Initial values come from toJSON(T{}).

import { backend } from './backend';
import { makeNativeEvent } from './react';

export const useValues = makeNativeEvent({
    backend,
    event: 'values',
    initial: {"changed":[]},
});
