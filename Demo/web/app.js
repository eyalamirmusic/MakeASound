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

send({ type: 'ready' });
