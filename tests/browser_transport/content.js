// Loaded only by the private browser transport fixture.
let focusRevision = 0;
const inputRevisions = new WeakMap();
for (const event of ['focus', 'blur']) {
    window.addEventListener(event, () => { ++focusRevision; }, true);
}

function eligible(field) {
    const supported = field instanceof HTMLTextAreaElement ||
        (field instanceof HTMLInputElement && field.type === 'text');
    return supported && field.isConnected && !field.readOnly && !field.disabled &&
        document.hasFocus() && document.activeElement === field;
}

document.addEventListener('input', async event => {
    if (location.origin !== PROBE_ORIGIN || !event.isTrusted) return;
    const field = event.target;
    const inputRevision = (inputRevisions.get(field) ?? 0) + 1;
    inputRevisions.set(field, inputRevision);
    if (event.isComposing || event.inputType !== 'insertText' || !eligible(field)) return;

    const caret = field.selectionStart;
    const expected = field.value;
    if (expected.length > 64 || caret !== field.selectionEnd ||
        expected.slice(caret - 4, caret) !== 'teh ') return;

    const focused = focusRevision;
    field.dataset.requested = 'yes';
    const reply = await chrome.runtime.sendMessage({test: field.id}).catch(() => null);
    if (reply?.corrected !== 'the') {
        field.dataset.outcome = reply?.error ?? 'invalid-response';
        return;
    }
    if (focused !== focusRevision || inputRevisions.get(field) !== inputRevision ||
        !eligible(field) || field.value !== expected ||
        field.selectionStart !== caret || field.selectionEnd !== caret) {
        field.dataset.outcome = 'stale';
        return;
    }
    field.setRangeText(reply.corrected, caret - 4, caret - 1, 'preserve');
    field.dataset.outcome = 'applied';
});
