export interface DropdownItem {
    id: number;
    label: string;
}

export interface DropdownInfo {
    items: DropdownItem[];
    currentId: number;
}

export interface ToggleListItem {
    id: number;
    label: string;
    selected: boolean;
}

export interface ToggleListInfo {
    items: ToggleListItem[];
}

export interface UIState {
    devices: DropdownInfo;
    sampleRates: DropdownInfo;
    blockSizes: DropdownInfo;
    midiPorts: ToggleListInfo;
}

export interface AudioControls {
    playing: boolean;
    gain: number;
    note: number;
    frequency: number;
    velocity: number;
}

export interface MidiPortToggleRequest {
    id: number;
    on: boolean;
}

export interface MidiLogEntry {
    text: string;
}

