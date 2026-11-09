// Loaded only by the private browser transport fixture.
let targetRevision = 0;
const inputRevisions = new WeakMap();
const boundaries = ' .,!؟?،';
let retry;
let retryTimer;
let undo;
let undoTimer;

function clearUndo() {
    clearTimeout(undoTimer);
    const record = undo;
    undo = undefined;
    if (PROBE_ROUTE && record)
        return message({test: record.field.id, route: {releaseUndo: record.attempt}});
}

function clearRetry() {
    clearTimeout(retryTimer);
    retry = undefined;
}

function targetChanged(event) {
    if (!event.isTrusted) return;
    ++targetRevision;
    clearRetry();
    clearUndo();
}

for (const event of ['focus', 'blur']) {
    window.addEventListener(event, targetChanged, true);
}
document.addEventListener('selectionchange', () => {
    if (retry && (retry.field.selectionStart !== retry.caret ||
                  retry.field.selectionEnd !== retry.caret)) clearRetry();
    if (undo && !caretAt(undo.field, undo.caret)) clearUndo();
}, true);
document.addEventListener('pointerdown', targetChanged, true);
document.addEventListener('keydown', event => {
    if (['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home', 'End',
         'PageUp', 'PageDown'].includes(event.key)) targetChanged(event);
}, true);

function eligible(field) {
    const supported = field instanceof HTMLTextAreaElement ||
        (field instanceof HTMLInputElement && field.type === 'text');
    return supported && !/\b(?:username|current-password|new-password|one-time-code|cc-[\w-]+)\b/i
        .test(field.autocomplete) && field.isConnected && !field.readOnly && !field.disabled &&
        document.hasFocus() && document.activeElement === field;
}

const message = request => chrome.runtime.sendMessage(request).catch(() => null);

function caretAt(field, caret) {
    return field.selectionStart === caret && field.selectionEnd === caret;
}

function replaceRange(field, {start, end, replacement, inputType}, isCurrent) {
    const fits = () => field.maxLength < 0 ||
        field.value.length + replacement.length - (end - start) <= field.maxLength;
    if (!isCurrent() || !fits()) return false;
    const event = {bubbles: true, composed: true, inputType,
                   data: inputType === 'historyUndo' ? null : replacement};
    if (!field.dispatchEvent(new InputEvent('beforeinput', {...event, cancelable: true})) ||
        !isCurrent() || !fits()) return false;
    field.setRangeText(replacement, start, end, 'preserve');
    field.dispatchEvent(new InputEvent('input', event));
    return true;
}

function matchesShortcut(event, shortcut) {
    if (typeof shortcut !== 'string' || event.getModifierState('AltGraph')) return false;
    const modifiers = shortcut.split('+');
    const key = modifiers.pop();
    const pressed = [['Ctrl', event.ctrlKey], ['Alt', event.altKey],
                     ['Shift', event.shiftKey], ['Meta', event.metaKey]]
        .filter(([, down]) => down).map(([name]) => name);
    return pressed.length > 0 && /^[A-Z]$/.test(key) && event.key.toUpperCase() === key &&
        pressed.length === modifiers.length && pressed.every(name => modifiers.includes(name));
}

document.addEventListener('keydown', async event => {
    const record = undo;
    if (!record || !event.isTrusted || event.repeat || event.isComposing || event.defaultPrevented ||
        !matchesShortcut(event, record.shortcut)) return;

    const field = record.field;
    const samePosition = () => record.target === targetRevision && eligible(field) &&
        inputRevisions.get(field) === record.inputRevision && caretAt(field, record.caret);
    if (!samePosition()) {
        clearUndo();
        return;
    }
    event.preventDefault();
    event.stopImmediatePropagation();
    clearRetry();
    await clearUndo();

    const policy = await message({test: field.id, policy: true});
    const isCurrent = () => samePosition() && field.value === record.expected;
    if (policy?.revision !== record.revision || policy.undoShortcut !== record.shortcut ||
        !isCurrent() || !replaceRange(field, {start: record.start, end: record.end,
            replacement: record.original, inputType: 'historyUndo'}, isCurrent)) {
        field.dataset.undoOutcome = 'declined';
        return;
    }
    const restored = record.expected.slice(0, record.start) + record.original +
        record.expected.slice(record.end);
    field.dataset.undoOutcome = eligible(field) && field.value === restored &&
        caretAt(field, record.caret + record.original.length - (record.end - record.start))
        ? 'applied' : 'changed';
}, true);

function observeRewrite(field, event) {
    if (!retry) return;
    if (retry.field !== field || event.isComposing || !eligible(field)) {
        clearRetry();
        return;
    }
    const caret = field.selectionStart;
    if (caret < retry.start || caret !== field.selectionEnd) {
        clearRetry();
        return;
    }
    // Track trusted edit events without reading the field again until policy allows it.
    const removed = retry.caret - caret;
    const deleting = event.inputType === 'deleteContentBackward' && removed > 0;
    const inserted = event.data?.length <= retry.word.length + 1 ? event.data : '';
    const segment = deleting ? retry.segment.slice(0, -removed) : retry.segment + inserted;
    const completed = segment.length === retry.word.length + 1 && boundaries.includes(segment.at(-1)) &&
        segment.slice(0, -1) === retry.word;
    const rewriting = event.inputType === 'insertText' && retry.rewriting &&
        inserted.length > 0 && caret === retry.caret + inserted.length &&
        (retry.word.startsWith(segment) || completed);
    if (!deleting && !rewriting) {
        clearRetry();
        return;
    }
    retry.segment = segment;
    retry.caret = caret;
    if (!segment) retry.rewriting = true;
}

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
    if (PROBE_ENGINE) {
        clearUndo();
        observeRewrite(field, event);
    }
    if (event.isComposing || event.inputType !== 'insertText' || !eligible(field)) return;

    const caret = field.selectionStart;
    if (!caret || caret !== field.selectionEnd) return;
    const target = targetRevision;
    const unchanged = () => target === targetRevision && inputRevisions.get(field) === inputRevision &&
        eligible(field) && caretAt(field, caret);
    let revision;
    if (PROBE_ENGINE) {
        if (event.data?.length !== 1 || !boundaries.includes(event.data)) return;
        const permission = await message({test: field.id, policy: true});
        if (!unchanged()) return;
        revision = permission?.revision;
        if (typeof revision !== 'string') {
            clearRetry();
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
        if (retry?.field === field && retry.revision === revision && retry.rewriting &&
            retry.segment === retry.word + boundary && expected.slice(0, retry.start) === retry.prefix &&
            expected.slice(retry.start, caret - 1) === retry.word && expected.slice(caret) === retry.suffix) {
            request.word.continuation = retry.attempt;
        }
    }
    const reply = await message(request);
    const skipped = PROBE_ENGINE && reply?.skipped === true;
    if (!skipped && typeof reply?.corrected !== 'string') {
        if (PROBE_ENGINE && unchanged()) clearRetry();
        field.dataset.outcome = reply?.error ?? (reply ? 'unchanged' : 'invalid-response');
        return;
    }
    if (!unchanged() || field.value !== expected) {
        field.dataset.outcome = 'stale';
        return;
    }
    let shortcut;
    if (PROBE_ENGINE) {
        const permission = await message({test: field.id, policy: true});
        if (permission?.revision !== revision || !unchanged() || field.value !== expected) {
            field.dataset.outcome = 'stale';
            return;
        }
        shortcut = permission.undoShortcut;
    }
    const start = PROBE_ENGINE ? reply.start : caret - 4;
    if (!Number.isInteger(start) || start < 0 || start >= caret - 1 ||
        (PROBE_ENGINE && (reply.length !== caret - 1 - start ||
                          typeof reply.attempt !== 'string'))) {
        field.dataset.outcome = 'invalid-response';
        return;
    }
    const word = expected.slice(start, caret - 1);
    const segment = (skipped ? word : reply.corrected) + boundary;
    if (!skipped) {
        const end = caret - 1;
        if (!replaceRange(field, {start, end, replacement: reply.corrected, inputType: 'insertReplacementText'},
                          () => unchanged() && field.value === expected)) {
            clearRetry();
            field.dataset.outcome = 'declined';
            return;
        }
        const after = expected.slice(0, start) + reply.corrected + expected.slice(end);
        const newCaret = caret + reply.corrected.length - word.length;
        if (target !== targetRevision || inputRevisions.get(field) !== inputRevision ||
            !eligible(field) || field.value !== after || !caretAt(field, newCaret)) {
            clearRetry();
            field.dataset.outcome = 'changed';
            return;
        }
        if (PROBE_ENGINE && typeof shortcut === 'string') {
            undo = {field, target, inputRevision, expected: after, caret: newCaret,
                    original: word, start, end: start + reply.corrected.length, revision, shortcut,
                    attempt: reply.attempt};
            undoTimer = setTimeout(clearUndo, 30000);
            if (PROBE_ROUTE) {
                const record = undo;
                const route = await message({test: field.id, route: {claimUndo: record.attempt, revision}});
                if (undo !== record) return;
                field.dataset.undoRoute = route?.claimed ? 'claimed' : 'unavailable';
            }
        }
    }
    if (PROBE_ENGINE) {
        clearRetry();
        retry = {field, start, word, segment, revision, attempt: reply.attempt,
                 prefix: expected.slice(0, start), suffix: expected.slice(caret),
                 caret: field.selectionStart, rewriting: false};
        retryTimer = setTimeout(clearRetry, 30000);
    }
    field.dataset.outcome = skipped ? 'skipped' : 'applied';
});
