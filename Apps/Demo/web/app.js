const send = (payload) =>
    window.webkit.messageHandlers.demo.postMessage(payload);

const $ = (id) => document.getElementById(id);

function applyDropdown(selectId, info) {
    const sel = $(selectId);
    const sig = info.items.map((i) => i.id + '|' + i.label).join(',,');

    if (sel.dataset.signature !== sig) {
        sel.innerHTML = '';
        for (const item of info.items) {
            const opt = document.createElement('option');
            opt.value = String(item.id);
            opt.textContent = item.label;
            sel.appendChild(opt);
        }
        sel.dataset.signature = sig;
    }

    sel.value = String(info.currentId);
}

function applyToggleList(containerId, info, onToggle) {
    const root = $(containerId);
    const sig = info.items.map((i) => i.id + '|' + i.label).join(',,');

    if (root.dataset.signature !== sig) {
        root.innerHTML = '';

        if (info.items.length === 0) {
            const empty = document.createElement('div');
            empty.className = 'empty';
            empty.textContent = '(no MIDI inputs)';
            root.appendChild(empty);
        } else {
            for (const item of info.items) {
                const label = document.createElement('label');
                label.className = 'toggle-item';
                const cb = document.createElement('input');
                cb.type = 'checkbox';
                cb.dataset.id = String(item.id);
                cb.addEventListener('change', (e) =>
                    onToggle(item.id, e.target.checked));
                label.appendChild(cb);
                label.appendChild(document.createTextNode(' ' + item.label));
                root.appendChild(label);
            }
        }

        root.dataset.signature = sig;
    }

    for (const item of info.items) {
        const cb = root.querySelector(`input[data-id="${item.id}"]`);
        if (cb) cb.checked = !!item.selected;
    }
}

const sendMidiToggle = (id, on) =>
    send({ type: 'midiPortToggle', id, on });

window.demoSetState = function(state) {
    applyDropdown('device', state.devices);
    applyDropdown('sampleRate', state.sampleRates);
    applyToggleList('midiPorts', state.midiPorts, sendMidiToggle);
    $('blockSize').value = String(state.blockSize);
    $('playing').checked = state.playing;
    $('gain').value = state.gain;
    $('gainValue').textContent = Number(state.gain).toFixed(2);
};

window.demoSetMidiPorts = function(info) {
    applyToggleList('midiPorts', info, sendMidiToggle);
};

window.demoUpdateAudio = function(controls) {
    $('playing').checked = controls.playing;
    $('gain').value = controls.gain;
    $('gainValue').textContent = Number(controls.gain).toFixed(2);
};

window.demoMidiEvent = function(text) {
    const log = $('midiLog');
    const empty = log.querySelector('.empty');
    if (empty) empty.remove();

    const line = document.createElement('div');
    line.textContent = text;
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

$('device').addEventListener('change', (e) =>
    send({ type: 'device', id: parseInt(e.target.value, 10) }));

$('sampleRate').addEventListener('change', (e) =>
    send({ type: 'sampleRate', value: parseInt(e.target.value, 10) }));

$('blockSize').addEventListener('change', (e) =>
    send({ type: 'blockSize', value: parseInt(e.target.value, 10) }));

$('midiLog').innerHTML = '<div class="empty">no MIDI events yet</div>';

send({ type: 'ready' });
