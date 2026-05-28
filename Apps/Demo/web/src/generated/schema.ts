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
    blockSize: number;
    devices: DropdownInfo;
    sampleRates: DropdownInfo;
    midiPorts: ToggleListInfo;
}

export interface AudioControls {
    playing: boolean;
    gain: number;
}

export interface MidiPortToggleRequest {
    id: number;
    on: boolean;
}

export interface MidiLogEntry {
    text: string;
}

