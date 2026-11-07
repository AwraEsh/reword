// Loaded only by the private browser transport fixture.
let focusRevision = 0;
const inputRevisions = new WeakMap();
for (const event of ['focus', 'blur']) {
    window.addEventListener(event, () => { ++focusRevision; }, true);
}

function eligible(field) {
    const supported = field instanceof HTMLTextAreaElement ||
        (field instanceof HTMLInputElement && field.type === 'text');
    return supported && !/\b(?:username|current-password|new-password|one-time-code|cc-[\w-]+)\b/i
        .test(field.autocomplete) && field.isConnected && !field.readOnly && !field.disabled &&
        document.hasFocus() && document.activeElement === field;
}

const message = request => chrome.runtime.sendMessage(request).catch(() => null);

if (PROBE_ENGINE && location.origin === PROBE_ORIGIN) {
    message({test: 'plain', policy: true}).then(permission => {
        document.documentElement.dataset.rewordProbeReady =
            typeof permission?.revision === 'string' ? 'yes' : 'no';
    });
}

document.addEventListener('input', async event => {
    if (location.origin !== PROBE_ORIGIN || !event.isTrusted) return;
    const field = event.target;
    const inputRevision = (inputRevisions.get(field) ?? 0) + 1;
    inputRevisions.set(field, inputRevision);
    if (event.isComposing || event.inputType !== 'insertText' || !eligible(field)) return;

    const caret = field.selectionStart;
    if (!caret || caret !== field.selectionEnd) return;
    const focused = focusRevision;
    const unchanged = () => focused === focusRevision && inputRevisions.get(field) === inputRevision &&
        eligible(field) && field.selectionStart === caret && field.selectionEnd === caret;
    let revision;
    if (PROBE_ENGINE) {
        if (event.data?.length !== 1 || !' .,!؟?،'.includes(event.data)) return;
        const permission = await message({test: field.id, policy: true});
        if (!unchanged()) return;
        revision = permission?.revision;
        if (typeof revision !== 'string') {
            field.dataset.outcome = permission?.error ?? 'unchanged';
            return;
        }
    }
    const expected = field.value;
    const boundary = expected[caret - 1];
    if (PROBE_ENGINE ? expected.length > 384 || event.data !== boundary :
        expected.length > 64 || expected.slice(caret - 4, caret) !== 'teh ') {
        return;
    }

    field.dataset.requested = 'yes';
    const request = {test: field.id};
    if (PROBE_ENGINE) {
        request.word = {text: expected.slice(0, caret - 1) + expected.slice(caret),
                        caret: caret - 1, boundary, revision};
    }
    const reply = await message(request);
    if (typeof reply?.corrected !== 'string') {
        field.dataset.outcome = reply?.error ?? (reply ? 'unchanged' : 'invalid-response');
        return;
    }
    if (!unchanged() || field.value !== expected) {
        field.dataset.outcome = 'stale';
        return;
    }
    if (PROBE_ENGINE) {
        const permission = await message({test: field.id, policy: true});
        if (permission?.revision !== revision || !unchanged() || field.value !== expected) {
            field.dataset.outcome = 'stale';
            return;
        }
    }
    const start = PROBE_ENGINE ? reply.start : caret - 4;
    if (!Number.isInteger(start) || start < 0 || start >= caret - 1 ||
        (PROBE_ENGINE && reply.length !== caret - 1 - start)) {
        field.dataset.outcome = 'invalid-response';
        return;
    }
    field.setRangeText(reply.corrected, start, caret - 1, 'preserve');
    field.dataset.outcome = 'applied';
});
