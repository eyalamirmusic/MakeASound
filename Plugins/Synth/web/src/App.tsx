import { useRef } from 'react';
import { useParameters, type ParameterControl } from './useParameters';
import type { WebParameter } from './generated/schema';

export default function App()
{
    const control = useParameters();
    const { parameters } = control;

    return (
        <main>
            <header>
                <h1>MakeASound Synth</h1>
            </header>

            <section className="waveform">
                {parameters.Waveform && <Waveform parameter={parameters.Waveform}
                                                  control={control} />}
            </section>

            <section className="knobs">
                {['Attack', 'Release', 'Level'].map((id) =>
                    parameters[id] && <Knob key={id} parameter={parameters[id]}
                                            control={control} />)}
            </section>

            <section className="legato">
                {parameters.Legato && <Toggle parameter={parameters.Legato}
                                              control={control} />}
            </section>
        </main>
    );
}

interface ControlProps
{
    parameter: WebParameter;
    control: ParameterControl;
}

function Waveform({ parameter, control }: ControlProps)
{
    const steps = Math.max(parameter.steps, 1);
    const selected = Math.round(parameter.normalized * steps);

    return (
        <div className="choice" role="radiogroup" aria-label={parameter.name}>
            {parameter.choices.map((choice, index) =>
                <button key={choice} type="button"
                        className={index === selected ? 'on' : ''}
                        aria-checked={index === selected} role="radio"
                        onClick={() => control.toggle(parameter.id, index / steps)}>
                    {choice}
                </button>)}
        </div>
    );
}

const dragRange = 200;
const arcDegrees = 270;

function Knob({ parameter, control }: ControlProps)
{
    const start = useRef({ y: 0, value: 0 });
    const angle = (parameter.normalized - 0.5) * arcDegrees;

    const onPointerDown = (e: React.PointerEvent<HTMLDivElement>) =>
    {
        e.currentTarget.setPointerCapture(e.pointerId);
        start.current = { y: e.clientY, value: parameter.normalized };
        control.begin(parameter.id);
    };

    const onPointerMove = (e: React.PointerEvent<HTMLDivElement>) =>
    {
        if (!e.currentTarget.hasPointerCapture(e.pointerId))
            return;

        const travel = (start.current.y - e.clientY) / dragRange;
        const next = Math.min(1, Math.max(0, start.current.value + travel));
        control.set(parameter.id, next);
    };

    const onPointerUp = (e: React.PointerEvent<HTMLDivElement>) =>
    {
        if (!e.currentTarget.hasPointerCapture(e.pointerId))
            return;

        e.currentTarget.releasePointerCapture(e.pointerId);
        control.end(parameter.id);
    };

    return (
        <div className="knob">
            <div className="dial" role="slider" aria-label={parameter.name}
                 aria-valuemin={0} aria-valuemax={1}
                 aria-valuenow={parameter.normalized}
                 aria-valuetext={parameter.text}
                 onPointerDown={onPointerDown} onPointerMove={onPointerMove}
                 onPointerUp={onPointerUp} onPointerCancel={onPointerUp}>
                <svg viewBox="0 0 100 100">
                    <circle className="track" cx="50" cy="50" r="40" />
                    <g transform={`rotate(${angle} 50 50)`}>
                        <line className="pointer" x1="50" y1="50" x2="50" y2="16" />
                    </g>
                </svg>
            </div>
            <div className="name">{parameter.name}</div>
            <div className="text">{parameter.text}</div>
        </div>
    );
}

function Toggle({ parameter, control }: ControlProps)
{
    const on = parameter.normalized >= 0.5;

    return (
        <button type="button" className={`toggle ${on ? 'on' : ''}`}
                aria-pressed={on}
                onClick={() => control.toggle(parameter.id, on ? 0 : 1)}>
            {parameter.name}
        </button>
    );
}
