import { useEffect, useState } from 'react';
import { backend } from './generated/backend';
import { useAudio, useUi } from './generated/hooks';
import type { DropdownInfo, ToggleListInfo } from './generated/schema';

const blockSizes = [64, 128, 256, 512, 1024, 2048];
const maxMidiLog = 100;

export default function App()
{
    const ui = useUi();
    const audio = useAudio();
    const [midiLog, setMidiLog] = useState<string[]>([]);

    useEffect(() => backend.on?.('midi', (entry) =>
        setMidiLog((prev) => [entry.text, ...prev].slice(0, maxMidiLog))), []);

    return (
        <main>
            <h1>MakeASound Demo</h1>

            <Row label="White noise">
                <input type="checkbox"
                       checked={audio.playing}
                       onChange={(e) => void backend.setPlaying(e.target.checked)} />
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
                <select value={ui.blockSize}
                        onChange={(e) =>
                            void backend.setBlockSize(parseInt(e.target.value, 10))}>
                    {blockSizes.map((size) =>
                        <option key={size} value={size}>{size}</option>)}
                </select>
            </Row>

            <Row label="MIDI inputs" align="start">
                <ToggleList info={ui.midiPorts}
                            onToggle={(id, on) =>
                                void backend.midiPortToggle({ id, on })} />
            </Row>

            <h2>MIDI log</h2>
            <MidiLog entries={midiLog} />
        </main>
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
