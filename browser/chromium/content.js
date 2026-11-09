// Shared by the browser extension and the isolated transport tests.
function startRewordContent({send, report = () => {}, engine = true, route = true}) {
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
        if (route && record)
            return send(record.field, {route: {releaseUndo: record.attempt}});
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

        const policy = await send(field, {policy: true});
        const isCurrent = () => samePosition() && field.value === record.expected;
        if (policy?.revision !== record.revision || policy.undoShortcut !== record.shortcut ||
            !isCurrent() || !replaceRange(field, {start: record.start, end: record.end,
                replacement: record.original, inputType: 'historyUndo'}, isCurrent)) {
            report(field, 'undoOutcome', 'declined');
            return;
        }
        const restored = record.expected.slice(0, record.start) + record.original +
            record.expected.slice(record.end);
        report(field, 'undoOutcome', eligible(field) && field.value === restored &&
            caretAt(field, record.caret + record.original.length - (record.end - record.start))
            ? 'applied' : 'changed');
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

    if (engine) {
        send(null, {policy: true}).then(permission => {
            report(document.documentElement, 'rewordProbeReady',
                typeof permission?.revision === 'string' ? 'yes' : 'no');
        });
    }

    document.addEventListener('input', async event => {
        if (!event.isTrusted) return;
        const field = event.target;
        const inputRevision = (inputRevisions.get(field) ?? 0) + 1;
        inputRevisions.set(field, inputRevision);
        if (engine) {
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
        if (engine) {
            if (event.data?.length !== 1 || !boundaries.includes(event.data)) return;
            const permission = await send(field, {policy: true});
            if (!unchanged()) return;
            revision = permission?.revision;
            if (typeof revision !== 'string') {
                clearRetry();
                report(field, 'outcome', permission?.error ?? 'unchanged');
                return;
            }
        }
        const expected = field.value;
        const boundary = expected[caret - 1];
        if (engine ? expected.length > 384 || event.data !== boundary :
            expected.length > 64 || expected.slice(caret - 4, caret) !== 'teh ') {
            return;
        }

        report(field, 'requested', 'yes');
        const request = {};
        if (engine) {
            request.word = {text: expected.slice(0, caret - 1) + expected.slice(caret),
                            caret: caret - 1, boundary, revision};
            if (retry?.field === field && retry.revision === revision && retry.rewriting &&
                retry.segment === retry.word + boundary && expected.slice(0, retry.start) === retry.prefix &&
                expected.slice(retry.start, caret - 1) === retry.word && expected.slice(caret) === retry.suffix) {
                request.word.continuation = retry.attempt;
            }
        }
        const reply = await send(field, request);
        const skipped = engine && reply?.skipped === true;
        if (!skipped && typeof reply?.corrected !== 'string') {
            if (engine && unchanged()) clearRetry();
            report(field, 'outcome', reply?.error ?? (reply ? 'unchanged' : 'invalid-response'));
            return;
        }
        if (!unchanged() || field.value !== expected) {
            report(field, 'outcome', 'stale');
            return;
        }
        let shortcut;
        let reserveUndo = false;
        if (engine) {
            const permission = await send(field, {policy: true});
            if (permission?.revision !== revision || !unchanged() || field.value !== expected) {
                report(field, 'outcome', 'stale');
                return;
            }
            shortcut = permission.undoShortcut;
            reserveUndo = route && permission.undoBeforeEdit === true;
        }
        const start = engine ? reply.start : caret - 4;
        if (!Number.isInteger(start) || start < 0 || start >= caret - 1 ||
            (engine && (reply.length !== caret - 1 - start ||
                              typeof reply.attempt !== 'string'))) {
            report(field, 'outcome', 'invalid-response');
            return;
        }
        const word = expected.slice(start, caret - 1);
        const segment = (skipped ? word : reply.corrected) + boundary;
        if (!skipped) {
            const end = caret - 1;
            const isCurrent = () => unchanged() && field.value === expected;
            const releaseReservation = () => {
                if (reserveUndo) void send(field, {route: {releaseUndo: reply.attempt}});
            };
            if (reserveUndo) {
                const claim = await send(field, {route: {claimUndo: reply.attempt, revision}});
                if (!claim?.claimed || typeof shortcut !== 'string' || !isCurrent()) {
                    if (claim?.claimed) releaseReservation();
                    clearRetry();
                    report(field, 'outcome', 'undo-unavailable');
                    return;
                }
            }
            if (!replaceRange(field, {start, end, replacement: reply.corrected, inputType: 'insertReplacementText'},
                              isCurrent)) {
                releaseReservation();
                clearRetry();
                report(field, 'outcome', 'declined');
                return;
            }
            const after = expected.slice(0, start) + reply.corrected + expected.slice(end);
            const newCaret = caret + reply.corrected.length - word.length;
            if (target !== targetRevision || inputRevisions.get(field) !== inputRevision ||
                !eligible(field) || field.value !== after || !caretAt(field, newCaret)) {
                releaseReservation();
                clearRetry();
                report(field, 'outcome', 'changed');
                return;
            }
            if (engine && typeof shortcut === 'string') {
                undo = {field, target, inputRevision, expected: after, caret: newCaret,
                        original: word, start, end: start + reply.corrected.length, revision, shortcut,
                        attempt: reply.attempt};
                undoTimer = setTimeout(clearUndo, 30000);
                if (reserveUndo) {
                    report(field, 'undoRoute', 'claimed');
                } else if (route) {
                    const record = undo;
                    const claim = await send(field, {route: {claimUndo: record.attempt, revision}});
                    if (undo !== record) return;
                    report(field, 'undoRoute', claim?.claimed ? 'claimed' : 'unavailable');
                }
            }
        }
        if (engine) {
            clearRetry();
            retry = {field, start, word, segment, revision, attempt: reply.attempt,
                     prefix: expected.slice(0, start), suffix: expected.slice(caret),
                     caret: field.selectionStart, rewriting: false};
            retryTimer = setTimeout(clearRetry, 30000);
        }
        report(field, 'outcome', skipped ? 'skipped' : 'applied');
    });
}
