// ─── Components ──────────────────────────────────────────
function statusDot(tone, pulse) {
    const color = tone === 'success' ? 'var(--success)' :
                  tone === 'warning' ? 'var(--warning)' :
                  tone === 'danger'  ? 'var(--danger)'  : 'var(--text-dim)';
    return el('span', { class: 'status-dot' + (pulse ? ' pulse' : ''), style: { color: color } });
}
function chip(text, opts) {
    opts = opts || {};
    const tone = opts.tone || 'neutral';
    return el('span', {
        class: 'chip' + (opts.size === 'sm' ? ' is-sm' : '') + ' tone-' + tone,
    }, [opts.icon || null, text]);
}
function field(label, hint, control) {
    return el('div', { class: 'field' }, [
        el('div', { class: 'field-label', text: label }),
        hint ? el('div', { class: 'field-hint', text: hint }) : null,
        el('div', { class: 'field-control' }, control),
    ]);
}
function textInput(opts) {
    opts = opts || {};
    const input = el('input', {
        type: opts.type || 'text',
        value: opts.value !== undefined && opts.value !== null ? opts.value : '',
        placeholder: opts.placeholder || '',
        autocomplete: opts.autocomplete || 'off',
    });
    if (opts.onChange) input.addEventListener('input', () => opts.onChange(input.value));
    const wrap = el('div', { class: 'text-input' + (opts.mono ? ' mono' : '') }, [input]);
    if (opts.suffix) wrap.appendChild(el('span', { class: 'suffix', text: opts.suffix }));
    return wrap;
}
function numberInput(opts) {
    opts = opts || {};
    const min = opts.min !== undefined ? opts.min : null;
    const max = opts.max !== undefined ? opts.max : null;
    const step = opts.step || 1;
    const inp = el('input', {
        type: 'number',
        value: String(opts.value !== undefined && opts.value !== null ? opts.value : ''),
        min: min !== null ? min : false,
        max: max !== null ? max : false,
        step: step,
    });
    function clamp(v) {
        let x = Number(v);
        if (isNaN(x)) x = min !== null ? min : 0;
        if (min !== null) x = Math.max(min, x);
        if (max !== null) x = Math.min(max, x);
        return x;
    }
    inp.addEventListener('change', () => {
        const v = clamp(inp.value);
        inp.value = v;
        if (opts.onChange) opts.onChange(v);
    });
    const dec = el('button', { type: 'button', text: '−', onclick: () => { inp.value = clamp(Number(inp.value || 0) - step); inp.dispatchEvent(new Event('change')); } });
    const inc = el('button', { type: 'button', text: '+', onclick: () => { inp.value = clamp(Number(inp.value || 0) + step); inp.dispatchEvent(new Event('change')); } });
    const wrap = el('div', { class: 'number-input' }, [inp]);
    if (opts.suffix) wrap.appendChild(el('span', { class: 'suffix', text: opts.suffix }));
    wrap.appendChild(el('div', { class: 'num-step' }, [inc, dec]));
    return wrap;
}
function selectInput(opts) {
    opts = opts || {};
    const sel = el('select');
    (opts.options || []).forEach(o => {
        const op = el('option', { value: o.value, text: o.label });
        if (String(o.value) === String(opts.value)) op.selected = true;
        sel.appendChild(op);
    });
    sel.addEventListener('change', () => opts.onChange && opts.onChange(sel.value));
    return el('div', { class: 'select-input' }, [sel]);
}
function segmented(opts) {
    opts = opts || {};
    const wrap = el('div', { class: 'segmented' + (opts.size === 'sm' ? ' sz-sm' : '') });
    (opts.options || []).forEach(o => {
        const b = el('button', {
            type: 'button', class: o.value === opts.value ? 'is-active' : '',
            onclick: () => opts.onChange && opts.onChange(o.value),
        }, [
            o.icon ? icon(o.icon, { size: 15, bold: true }) : null,
            el('span', { text: o.label }),
        ]);
        wrap.appendChild(b);
    });
    return wrap;
}
function toggle(opts) {
    const b = el('button', {
        type: 'button',
        class: 'toggle' + (opts.value ? ' is-on' : ''),
        'aria-pressed': opts.value ? 'true' : 'false',
        onclick: () => opts.onChange && opts.onChange(!opts.value),
    });
    return b;
}
function bar(value, opts) {
    opts = opts || {};
    const fill = el('div', { class: 'bar-fill', style: { width: Math.max(0, Math.min(100, value)) + '%' } });
    if (opts.color) fill.style.background = opts.color;
    return el('div', { class: 'bar' + (opts.thin ? ' is-thin' : '') + (opts.thick ? ' is-thick' : '') }, [fill]);
}
function miniBars(data) {
    const max = Math.max(1, ...data);
    return el('div', { class: 'mini-bars' }, data.map(v => {
        const h = (v / max) * 100;
        return el('div', {
            class: 'mb-bar' + (v > 0 ? ' is-data' : ''),
            style: { height: h + '%', opacity: v > 0 ? (0.4 + 0.6 * (v / max)) : 1 },
        });
    }));
}
function button(label, opts) {
    opts = opts || {};
    const cls = ['btn'];
    if (opts.variant === 'secondary') cls.push('btn-secondary');
    else if (opts.variant === 'ghost') cls.push('btn-ghost');
    if (opts.danger) { cls.length = 1; cls.push('btn'); cls.push('btn-danger'); }
    if (opts.size === 'sm') cls.push('sz-sm');
    if (opts.size === 'lg') cls.push('sz-lg');
    if (opts.full) cls.push('full');
    const b = el('button', { type: 'button', class: cls.join(' ') });
    if (opts.icon) b.appendChild(opts.icon);
    b.appendChild(el('span', { text: label }));
    if (opts.onClick) b.addEventListener('click', opts.onClick);
    if (opts.disabled) b.disabled = true;
    return b;
}
function sectionLabel(text, iconId) {
    return el('div', { class: 'section-label' }, [
        iconId ? icon(iconId) : null,
        el('span', { text: text }),
    ]);
}
function card(opts, children) {
    opts = opts || {};
    const cls = ['card'];
    if (opts.flush) cls.push('is-flush');
    if (opts.clickable) cls.push('is-clickable');
    if (opts.accent) cls.push('is-accent');
    const c = el('div', { class: cls.join(' ') }, children);
    if (opts.onClick) {
        c.addEventListener('click', opts.onClick);
        c.setAttribute('role', 'button');
    }
    return c;
}
function cardHeader(text, iconId) {
    return el('div', { class: 'card-header' }, [iconId ? icon(iconId) : null, el('span', { text: text })]);
}
function listRow(opts, children) {
    opts = opts || {};
    const cls = ['list-row'];
    if (opts.last) cls.push('last');
    if (opts.onClick) cls.push('is-clickable');
    const r = el('div', { class: cls.join(' ') }, children);
    if (opts.onClick) r.addEventListener('click', opts.onClick);
    return r;
}

// ─── Screen header ───────────────────────────────────────
function screenHeader(title, subtitle, right) {
    const block = el('div', { class: 'screen-title-block' }, [
        el('div', { class: 'screen-title', text: title }),
        subtitle ? el('div', { class: 'screen-subtitle' }, subtitle) : null,
    ]);
    return el('div', { class: 'screen-header' }, [
        el('div', { class: 'screen-header-row' }, [block, right || null]),
    ]);
}
