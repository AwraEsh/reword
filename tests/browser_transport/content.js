// Loaded only by the private browser transport fixture.
let targetRevision = 0;
const inputRevisions = new WeakMap();
const boundaries = ' .,!؟?،';
let retry;
let retryTimer;

function clearRetry() {
    clearTimeout(retryTimer);
    retry = undefined;
}

function targetChanged(event) {
    if (!event.isTrusted) return;
    ++targetRevision;
    clearRetry();
}

for (const event of ['focus', 'blur']) {
    window.addEventListener(event, targetChanged, true);
}
document.addEventListener('selectionchange', () => {
    if (retry && (retry.field.selectionStart !== retry.caret ||
                  retry.field.selectionEnd !== retry.caret)) clearRetry();
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
    if (PROBE_ENGINE) observeRewrite(field, event);
    if (event.isComposing || event.inputType !== 'insertText' || !eligible(field)) return;

    const caret = field.selectionStart;
    if (!caret || caret !== field.selectionEnd) return;
    const target = targetRevision;
    const unchanged = () => target === targetRevision && inputRevisions.get(field) === inputRevision &&
        eligible(field) && field.selectionStart === caret && field.selectionEnd === caret;
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
    if (PROBE_ENGINE) {
        const permission = await message({test: field.id, policy: true});
        if (permission?.revision !== revision || !unchanged() || field.value !== expected) {
            field.dataset.outcome = 'stale';
            return;
        }
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
    if (!skipped) field.setRangeText(reply.corrected, start, caret - 1, 'preserve');
    if (PROBE_ENGINE) {
        clearRetry();
        retry = {field, start, word, segment, revision, attempt: reply.attempt,
                 prefix: expected.slice(0, start), suffix: expected.slice(caret),
                 caret: field.selectionStart, rewriting: false};
        retryTimer = setTimeout(clearRetry, 30000);
    }
    field.dataset.outcome = skipped ? 'skipped' : 'applied';
});
