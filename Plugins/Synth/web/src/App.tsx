import { useRef, useState } from 'react';
import { edits, useSchema, useValue } from './useParameters';
import type { WebParameter } from './generated/schema';

export default function App()
{
    const schema = useSchema();

    return (
        <main>
            <header>
                <h1>MakeASound Synth</h1>
                <button type="button" className="reset" onClick={edits.resetAll}>
                    Reset
                </button>
            </header>

            <section className="waveform">
                {schema.Waveform && <Waveform parameter={schema.Waveform} />}
            </section>

            <section className="knobs">
                {['Attack', 'Release', 'Level'].map((id) =>
                    schema[id] && <Knob key={id} parameter={schema[id]} />)}
            </section>

            <section className="legato">
                {schema.Legato && <Toggle parameter={schema.Legato} />}
            </section>
        </main>
    );
}

interface ControlProps
{
    parameter: WebParameter;
}

function Waveform({ parameter }: ControlProps)
{
    const value = useValue(parameter);
    const steps = Math.max(parameter.steps, 1);
    const selected = Math.round(value.normalized * steps);

    return (
        <div className="choice" role="radiogroup" aria-label={parameter.name}>
            {parameter.choices.map((choice, index) =>
                <button key={choice} type="button"
                        className={index === selected ? 'on' : ''}
                        aria-checked={index === selected} role="radio"
                        onClick={() => edits.pick(parameter.id, index / steps)}>
                    {choice}
                </button>)}
        </div>
    );
}

const dragRange = 200;
const arcDegrees = 270;

// Drag to turn, double-click to reset, click the readout to type a value. While
// dragging, the knob shows the page's own number, so a late echo never pulls it
// back under the pointer.
function Knob({ parameter }: ControlProps)
{
    const value = useValue(parameter);
    const start = useRef({ y: 0, value: 0 });
    const [dragged, setDragged] = useState<number | null>(null);
    const [typing, setTyping] = useState(false);

    const normalized = dragged ?? value.normalized;
    const angle = (normalized - 0.5) * arcDegrees;

    const onPointerDown = (e: React.PointerEvent<HTMLDivElement>) =>
    {
        e.currentTarget.setPointerCapture(e.pointerId);
        start.current = { y: e.clientY, value: value.normalized };
        setDragged(value.normalized);
        edits.begin(parameter.id);
    };

    const onPointerMove = (e: React.PointerEvent<HTMLDivElement>) =>
    {
        if (!e.currentTarget.hasPointerCapture(e.pointerId))
            return;

        const travel = (start.current.y - e.clientY) / dragRange;
        const next = Math.min(1, Math.max(0, start.current.value + travel));
        setDragged(next);
        edits.set(parameter.id, next);
    };

    const onPointerUp = (e: React.PointerEvent<HTMLDivElement>) =>
    {
        if (!e.currentTarget.hasPointerCapture(e.pointerId))
            return;

        e.currentTarget.releasePointerCapture(e.pointerId);
        setDragged(null);
        edits.end(parameter.id);
    };

    const onKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) =>
    {
        if (e.key === 'Enter')
            edits.type(parameter.id, e.currentTarget.value);

        if (e.key === 'Enter' || e.key === 'Escape')
            setTyping(false);
    };

    return (
        <div className="knob">
            <div className="dial" role="slider" aria-label={parameter.name}
                 aria-valuemin={0} aria-valuemax={1}
                 aria-valuenow={normalized}
                 aria-valuetext={value.text}
                 title={`Double-click for ${parameter.defaultText}`}
                 onPointerDown={onPointerDown} onPointerMove={onPointerMove}
                 onPointerUp={onPointerUp} onPointerCancel={onPointerUp}
                 onDoubleClick={() => edits.reset(parameter.id)}>
                <svg viewBox="0 0 100 100">
                    <circle className="track" cx="50" cy="50" r="40" />
                    <g transform={`rotate(${angle} 50 50)`}>
                        <line className="pointer" x1="50" y1="50" x2="50" y2="16" />
                    </g>
                </svg>
            </div>
            <div className="name">{parameter.name}</div>
            {typing
                ? <input className="text" autoFocus defaultValue={value.text}
                         aria-label={`${parameter.name} value`}
                         onKeyDown={onKeyDown} onBlur={() => setTyping(false)} />
                : <div className="text" onClick={() => setTyping(true)}>
                      {value.text}
                  </div>}
        </div>
    );
}

function Toggle({ parameter }: ControlProps)
{
    const on = useValue(parameter).normalized >= 0.5;

    return (
        <button type="button" className={`toggle ${on ? 'on' : ''}`}
                aria-pressed={on}
                onClick={() => edits.pick(parameter.id, on ? 0 : 1)}>
            {parameter.name}
        </button>
    );
}
