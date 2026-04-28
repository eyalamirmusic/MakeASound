const send = (payload) =>
    window.webkit.messageHandlers.demo.postMessage(payload);

const $ = (id) => document.getElementById(id);

const devicesById = new Map();

function rebuildSampleRates(device, current) {
    const sel = $('sampleRate');
    sel.innerHTML = '';
    for (const rate of device.sampleRates) {
        const opt = document.createElement('option');
        opt.value = rate;
        opt.textContent = rate + ' Hz';
        if (rate === current) opt.selected = true;
        sel.appendChild(opt);
    }
}

window.demoSetState = function(state) {
    const dev = $('device');
    dev.innerHTML = '';
    devicesById.clear();
    for (const d of state.outputDevices) {
        devicesById.set(d.id, d);
        const opt = document.createElement('option');
        opt.value = d.id;
        opt.textContent = d.name;
        if (d.id === state.currentDeviceId) opt.selected = true;
        dev.appendChild(opt);
    }

    const current = devicesById.get(state.currentDeviceId);
    if (current) rebuildSampleRates(current, state.sampleRate);

    $('blockSize').value = String(state.blockSize);
    $('playing').checked = state.playing;
    $('gain').value = state.gain;
    $('gainValue').textContent = Number(state.gain).toFixed(2);

    const midi = $('midiPort');
    midi.innerHTML = '';
    const none = document.createElement('option');
    none.value = -1;
    none.textContent = '(none)';
    if (state.currentMidiPortId === -1) none.selected = true;
    midi.appendChild(none);

    for (const p of state.midiInputPorts) {
        const opt = document.createElement('option');
        opt.value = p.id;
        opt.textContent = p.name;
        if (p.id === state.currentMidiPortId) opt.selected = true;
        midi.appendChild(opt);
    }
};

window.demoUpdateAudio = function(controls) {
    $('playing').checked = controls.playing;
    $('gain').value = controls.gain;
    $('gainValue').textContent = Number(controls.gain).toFixed(2);
};

const hex = (b) => b.toString(16).padStart(2, '0');

function formatMidi(msg) {
    const b = msg.bytes;
    if (!b || b.length === 0) return '(empty)';
    const dump = '[' + b.map(hex).join(' ') + ']';
    const status = b[0] & 0xF0;
    const ch = (b[0] & 0x0F) + 1;

    if (status === 0x80)
        return `Note Off    ch:${ch} note:${b[1]} vel:${b[2]}  ${dump}`;
    if (status === 0x90 && b[2] === 0)
        return `Note Off    ch:${ch} note:${b[1]}        ${dump}`;
    if (status === 0x90)
        return `Note On     ch:${ch} note:${b[1]} vel:${b[2]}  ${dump}`;
    if (status === 0xA0)
        return `Polytouch   ch:${ch} note:${b[1]} val:${b[2]}  ${dump}`;
    if (status === 0xB0)
        return `CC          ch:${ch} cc:${b[1]} val:${b[2]}    ${dump}`;
    if (status === 0xC0)
        return `Program     ch:${ch} prog:${b[1]}             ${dump}`;
    if (status === 0xD0)
        return `ChanPress   ch:${ch} val:${b[1]}              ${dump}`;
    if (status === 0xE0)
        return `Pitch Bend  ch:${ch} val:${(b[2] << 7) | b[1]}     ${dump}`;
    return `System      ${dump}`;
}

window.demoMidiEvent = function(msg) {
    const log = $('midiLog');
    const empty = log.querySelector('.empty');
    if (empty) empty.remove();

    const line = document.createElement('div');
    line.textContent = formatMidi(msg);
    log.prepend(line);
    while (log.children.length > 100) log.removeChild(log.lastChild);
};

$('playing').addEventListener('change', (e) =>
    send({ type: 'playing', value: e.target.checked }));

$('gain').addEventListener('input', (e) => {
    const v = parseFloat(e.target.value);
    $('gainValue').textContent = v.toFixed(2);
    send({ type: 'gain', value: v });
});

$('device').addEventListener('change', (e) => {
    const id = parseInt(e.target.value, 10);
    const d = devicesById.get(id);
    if (d) rebuildSampleRates(d, d.sampleRates[0]);
    send({ type: 'device', id });
});

$('sampleRate').addEventListener('change', (e) =>
    send({ type: 'sampleRate', value: parseInt(e.target.value, 10) }));

$('blockSize').addEventListener('change', (e) =>
    send({ type: 'blockSize', value: parseInt(e.target.value, 10) }));

$('midiPort').addEventListener('change', (e) =>
    send({ type: 'midiPort', id: parseInt(e.target.value, 10) }));

$('midiLog').innerHTML = '<div class="empty">no MIDI events yet</div>';

send({ type: 'ready' });
