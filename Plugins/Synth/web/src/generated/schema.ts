export interface WebParameter {
    id: string;
    name: string;
    label: string;
    normalized: number;
    text: string;
    defaultNormalized: number;
    defaultText: string;
    steps: number;
    choices: string[];
    automatable: boolean;
}

export interface WebParameters {
    parameters: WebParameter[];
}

export interface WebParameterValue {
    id: string;
    normalized: number;
    text: string;
}

export interface WebParameterSnapshot {
    items: WebParameterValue[];
}

export interface WebParameterId {
    id: string;
}

export interface WebParameterEdit {
    id: string;
    normalized: number;
}

export interface WebParameterText {
    id: string;
    text: string;
}

export interface WebParameterValues {
    changed: WebParameterValue[];
}

