// ─── Tiny DOM helpers ────────────────────────────────────
function el(tag, attrs, children) {
    const n = document.createElement(tag);
    if (attrs) {
        for (const k in attrs) {
            if (k === 'class')    n.className = attrs[k];
            else if (k === 'html') n.innerHTML = attrs[k];
            else if (k === 'text') n.textContent = attrs[k];
            else if (k.indexOf('on') === 0 && typeof attrs[k] === 'function') {
                n.addEventListener(k.slice(2).toLowerCase(), attrs[k]);
            } else if (k === 'style' && typeof attrs[k] === 'object') {
                Object.assign(n.style, attrs[k]);
            } else if (attrs[k] === false || attrs[k] === null || attrs[k] === undefined) {
                // skip
            } else {
                n.setAttribute(k, attrs[k]);
            }
        }
    }
    if (children) appendChildren(n, children);
    return n;
}
function appendChildren(parent, children) {
    if (children === null || children === undefined || children === false) return;
    if (Array.isArray(children)) {
        for (const c of children) appendChildren(parent, c);
        return;
    }
    if (typeof children === 'string' || typeof children === 'number') {
        parent.appendChild(document.createTextNode(String(children)));
        return;
    }
    parent.appendChild(children);
}
function icon(id, opts) {
    opts = opts || {};
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('class', 'icon' + (opts.bold ? ' icon-bold' : '') + (opts.cls ? ' ' + opts.cls : ''));
    svg.setAttribute('viewBox', '0 0 24 24');
    if (opts.size) svg.style.fontSize = opts.size + 'px';
    const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
    use.setAttributeNS('http://www.w3.org/1999/xlink', 'href', '#' + id);
    use.setAttribute('href', '#' + id);
    svg.appendChild(use);
    return svg;
}
function clear(n) { while (n.firstChild) n.removeChild(n.firstChild); }
