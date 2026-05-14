import { useCallback, useEffect, useState } from 'react';
import { invoke, useBridgeEvent } from './bridge';

interface DropdownItem
{
    id: number;
    label: string;
}

interface DropdownInfo
{
    items: DropdownItem[];
    currentId: number;
}

interface ToggleListItem
{
    id: number;
    label: string;
    selected: boolean;
}

interface ToggleListInfo
{
    items: ToggleListItem[];
}

interface SynthState
{
    playing: boolean;
    gain: number;
    note: number;
    frequency: number;
    velocity: number;
    devices: DropdownInfo;
    sampleRates: DropdownInfo;
    blockSizes: DropdownInfo;
    midiPorts: ToggleListInfo;
}

interface AudioControls
{
    playing: boolean;
    gain: number;
    note: number;
    frequency: number;
    velocity: number;
}

const noteNames = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const maxMidiLog = 100;

function noteName(midi: number): string
{
    if (midi < 0)
        return '';

    const octave = Math.floor(midi / 12) - 1;
    return noteNames[midi % 12] + octave;
}

function withCurrentId(info: DropdownInfo, id: number): DropdownInfo
{
    return { ...info, currentId: id };
}

function withSelected(info: ToggleListInfo, id: number, on: boolean): ToggleListInfo
{
    return {
        items: info.items.map((item) =>
            item.id === id ? { ...item, selected: on } : item),
    };
}

export default function App()
{
    const [state, setState] = useState<SynthState | null>(null);
    const [midiLog, setMidiLog] = useState<string[]>([]);

    const patch = useCallback((next: Partial<SynthState>) =>
        setState((prev) => prev ? { ...prev, ...next } : prev), []);

    const sendAndPatch = useCallback((next: Partial<SynthState>,
                                      command: string,
                                      payload: unknown) =>
    {
        patch(next);
        invoke(command, payload);
    }, [patch]);

    useEffect(() =>
    {
        invoke<SynthState>('getState').then(setState);
    }, []);

    useBridgeEvent<SynthState>('state', setState);

    useBridgeEvent<ToggleListInfo>('midiPorts', useCallback((info: ToggleListInfo) =>
    {
        setState((prev) => prev ? { ...prev, midiPorts: info } : prev);
    }, []));

    useBridgeEvent<AudioControls>('audio', useCallback((controls: AudioControls) =>
    {
        setState((prev) => prev ? { ...prev, ...controls } : prev);
    }, []));

    useBridgeEvent<string>('midi', useCallback((text: string) =>
    {
        setMidiLog((prev) => [text, ...prev].slice(0, maxMidiLog));
    }, []));

    if (!state)
        return <div className="loading">Loading…</div>;

    return (
        <main>
            <h1>MakeASound Synth</h1>

            <Row label="Voice">
                <Voice state={state} />
            </Row>

            <Row label="Gain">
                <input type="range" min={0} max={1} step={0.01}
                       value={state.gain}
                       onChange={(e) =>
                       {
                           const value = Number(e.target.value);
                           sendAndPatch({ gain: value }, 'setGain', value);
                       }} />
                <span className="value">{state.gain.toFixed(2)}</span>
            </Row>

            <Row label="Output device">
                <Dropdown info={state.devices}
                          onChange={(id) =>
                              sendAndPatch({ devices: withCurrentId(state.devices, id) },
                                           'setDevice', id)} />
            </Row>

            <Row label="Sample rate">
                <Dropdown info={state.sampleRates}
                          onChange={(id) =>
                              sendAndPatch({ sampleRates: withCurrentId(state.sampleRates, id) },
                                           'setSampleRate', id)} />
            </Row>

            <Row label="Block size">
                <Dropdown info={state.blockSizes}
                          onChange={(id) =>
                              sendAndPatch({ blockSizes: withCurrentId(state.blockSizes, id) },
                                           'setBlockSize', id)} />
            </Row>

            <Row label="MIDI inputs" align="start">
                <ToggleList info={state.midiPorts}
                            onToggle={(id, on) =>
                                sendAndPatch(
                                    { midiPorts: withSelected(state.midiPorts, id, on) },
                                    'midiPortToggle', { id, on })} />
            </Row>

            <Row label="">
                <button type="button" onClick={() => invoke('allNotesOff')}>
                    All notes off
                </button>
            </Row>

            <h2>MIDI log</h2>
            <MidiLog entries={midiLog} />
        </main>
    );
}

function Voice({ state }: { state: SynthState })
{
    if (!state.playing || state.note < 0)
        return <span className="value-large">silent</span>;

    const vel = Math.round(state.velocity * 127);
    const freq = state.frequency.toFixed(2);

    return (
        <span className="value-large active">
            {noteName(state.note)} ({state.note}) — {freq} Hz · vel {vel}
        </span>
    );
}

interface RowProps
{
    label: string;
    align?: 'center' | 'start';
    children: React.ReactNode;
}

function Row({ label, align = 'center', children }: RowProps)
{
    return (
        <div className={`row ${align === 'start' ? 'align-start' : ''}`}>
            <label>{label}</label>
            {children}
        </div>
    );
}

interface DropdownProps
{
    info: DropdownInfo;
    onChange: (id: number) => void;
}

function Dropdown({ info, onChange }: DropdownProps)
{
    return (
        <select value={info.currentId}
                onChange={(e) => onChange(parseInt(e.target.value, 10))}>
            {info.items.map((item) =>
                <option key={item.id} value={item.id}>{item.label}</option>)}
        </select>
    );
}

interface ToggleListProps
{
    info: ToggleListInfo;
    onToggle: (id: number, on: boolean) => void;
}

function ToggleList({ info, onToggle }: ToggleListProps)
{
    if (info.items.length === 0)
        return <div className="empty">(no MIDI inputs)</div>;

    return (
        <div className="toggle-list">
            {info.items.map((item) =>
                <label key={item.id} className="toggle-item">
                    <input type="checkbox"
                           checked={item.selected}
                           onChange={(e) => onToggle(item.id, e.target.checked)} />
                    {' ' + item.label}
                </label>)}
        </div>
    );
}

function MidiLog({ entries }: { entries: string[] })
{
    if (entries.length === 0)
        return <div id="midiLog"><div className="empty">no MIDI events yet</div></div>;

    return (
        <div id="midiLog">
            {entries.map((text, index) => <div key={index}>{text}</div>)}
        </div>
    );
}
