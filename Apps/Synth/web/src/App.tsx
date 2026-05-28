import { useEffect, useState } from 'react';
import { backend } from './generated/backend';
import { useAudio, useUi } from './generated/hooks';
import type { AudioControls, DropdownInfo, ToggleListInfo } from './generated/schema';

const noteNames = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const maxMidiLog = 100;

function noteName(midi: number): string
{
    if (midi < 0)
        return '';

    const octave = Math.floor(midi / 12) - 1;
    return noteNames[midi % 12] + octave;
}

export default function App()
{
    const ui = useUi();
    const audio = useAudio();
    const [midiLog, setMidiLog] = useState<string[]>([]);

    useEffect(() => backend.on?.('midi', (entry) =>
        setMidiLog((prev) => [entry.text, ...prev].slice(0, maxMidiLog))), []);

    return (
        <main>
            <h1>MakeASound Synth</h1>

            <Row label="Voice">
                <Voice audio={audio} />
            </Row>

            <Row label="Gain">
                <input type="range" min={0} max={1} step={0.01}
                       value={audio.gain}
                       onChange={(e) => void backend.setGain(Number(e.target.value))} />
                <span className="value">{audio.gain.toFixed(2)}</span>
            </Row>

            <Row label="Output device">
                <Dropdown info={ui.devices}
                          onChange={(id) => void backend.setDevice(id)} />
            </Row>

            <Row label="Sample rate">
                <Dropdown info={ui.sampleRates}
                          onChange={(rate) => void backend.setSampleRate(rate)} />
            </Row>

            <Row label="Block size">
                <Dropdown info={ui.blockSizes}
                          onChange={(size) => void backend.setBlockSize(size)} />
            </Row>

            <Row label="MIDI inputs" align="start">
                <ToggleList info={ui.midiPorts}
                            onToggle={(id, on) =>
                                void backend.midiPortToggle({ id, on })} />
            </Row>

            <Row label="">
                <button type="button" onClick={() => void backend.allNotesOff()}>
                    All notes off
                </button>
            </Row>

            <h2>MIDI log</h2>
            <MidiLog entries={midiLog} />
        </main>
    );
}

function Voice({ audio }: { audio: AudioControls })
{
    if (!audio.playing || audio.note < 0)
        return <span className="value-large">silent</span>;

    const vel = Math.round(audio.velocity * 127);
    const freq = audio.frequency.toFixed(2);

    return (
        <span className="value-large active">
            {noteName(audio.note)} ({audio.note}) — {freq} Hz · vel {vel}
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
