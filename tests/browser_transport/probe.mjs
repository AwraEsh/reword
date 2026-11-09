// Runs inside test-native-linux.py's owned X11 and D-Bus session.
import {execFileSync, spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {chmod, copyFile, mkdir, readFile, readlink, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {dirname, join} from 'node:path';
import {createInterface} from 'node:readline';
import {setTimeout as sleep} from 'node:timers/promises';
import {fileURLToPath} from 'node:url';

const [root, output, executable, engineHost] = process.argv.slice(2);
if (!root || !output || !['helium', 'google-chrome'].includes(executable)) {
    throw new Error('Expected an owned test directory, a report path and a supported test browser');
}
const fixture = fileURLToPath(new URL('.', import.meta.url));
const extension = join(root, 'extension');
const profile = join(root, 'profile');
const hostName = 'org.reword.transport_probe';
const hostPath = engineHost || join(root, 'native-host');
const hostLog = join(root, 'host-cases');
const id = createHash('sha256').update(extension).digest('hex').slice(0, 32)
    .replace(/[0-9a-f]/g, character => String.fromCharCode(97 + parseInt(character, 16)));
const extensionOrigin = 'chrome-extension://' + id + '/';
const result = {browser: executable, compiledEngine: Boolean(engineHost), stage: 'setup'};
const writeJSON = (path, value) => writeFile(path, JSON.stringify(value, null, 2) + '\n');
const page = await readFile(join(fixture, 'page.html'));
const formObserver = await readFile(join(fixture, 'form-observer.js'));
const server = createServer((request, response) => {
    if (request.url === '/form-observer.js') {
        response.writeHead(200, {'Content-Type': 'text/javascript'});
        response.end(formObserver);
    } else {
        response.writeHead(200, {'Content-Type': 'text/html',
            'Content-Security-Policy': "default-src 'none'; script-src 'self'"});
        response.end(page);
    }
});

function devTools(browser) {
    const commands = new Map();
    let nextId = 0;
    let buffer = '';
    browser.stdio[4].setEncoding('utf8');
    browser.stdio[4].on('data', chunk => {
        buffer += chunk;
        for (let end = buffer.indexOf('\0'); end >= 0; end = buffer.indexOf('\0')) {
            const message = JSON.parse(buffer.slice(0, end));
            buffer = buffer.slice(end + 1);
            commands.get(message.id)?.(message);
        }
    });
    browser.stdio[3].on('error', () => {
        // Calls time out if the browser exits before accepting a command.
    });
    return (method, params = {}, sessionId) => new Promise((resolve, reject) => {
        const key = ++nextId;
        const timeout = setTimeout(() => {
            commands.delete(key);
            reject(new Error(method + ' timed out'));
        }, method === 'Browser.getVersion' ? 15000 : 6000);
        commands.set(key, response => {
            clearTimeout(timeout);
            commands.delete(key);
            if (response.error) reject(new Error(method + ' failed: ' + response.error.message));
            else resolve(response.result);
        });
        browser.stdio[3].write(JSON.stringify({id: key, method, params, sessionId}) + '\0');
    });
}

async function waitFor(check, timeout = 2500) {
    const deadline = performance.now() + timeout;
    do {
        const value = await check();
        if (value) return value;
        await sleep(30);
    } while (performance.now() < deadline);
    return false;
}

async function hostCases() {
    try {
        return (await readFile(hostLog, 'ascii')).trim().split('\n');
    } catch (error) {
        if (error.code === 'ENOENT') return [];
        throw error;
    }
}

let browser;
await mkdir(output, {mode: 0o700});
try {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const origin = 'http://127.0.0.1:' + server.address().port;
    await mkdir(extension);
    const originConfig = 'const PROBE_ORIGIN = ' + JSON.stringify(origin) + ';\n' +
        'const PROBE_ENGINE = ' + Boolean(engineHost) + ';\n';
    await writeFile(join(extension, 'content.js'),
        originConfig + await readFile(join(fixture, 'content.js'), 'utf8'));
    const workerConfig = originConfig + 'const PROBE_HOST = ' + JSON.stringify(hostName) + ';\n';
    await writeFile(join(extension, 'worker.js'), workerConfig + await readFile(join(fixture, 'worker.js'), 'utf8'));
    await writeJSON(join(extension, 'manifest.json'), {
        manifest_version: 3, name: 'Reword private transport fixture', version: '1.0.0',
        permissions: ['nativeMessaging'], background: {service_worker: 'worker.js'},
        content_scripts: [{matches: ['http://127.0.0.1/*'], js: ['content.js']}],
    });

    if (!engineHost) {
        await copyFile(join(fixture, 'host.py'), hostPath);
        await chmod(hostPath, 0o700);
        await writeFile(join(root, 'expected-origin'), extensionOrigin);
    }
    const hosts = join(profile, 'NativeMessagingHosts');
    await mkdir(hosts, {recursive: true});
    await writeJSON(join(hosts, hostName + '.json'), {
        name: hostName, description: 'Reword transport fixture', path: hostPath, type: 'stdio',
        allowed_origins: [extensionOrigin],
    });

    const useDevToolsInstall = executable === 'google-chrome';
    result.stage = 'browser';
    browser = spawn(executable, [
        '--remote-debugging-pipe', '--user-data-dir=' + profile,
        useDevToolsInstall ? '--enable-unsafe-extension-debugging' : '--load-extension=' + extension,
        '--no-first-run', '--no-default-browser-check', '--disable-background-networking',
        '--disable-sync', '--no-proxy-server', '--password-store=basic', '--ozone-platform=x11',
        '--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE 127.0.0.1', 'about:blank',
    ], {
        env: {...process.env, HOME: root, REWORD_DATA_DIR: join(root, 'reword'),
              REWORD_BROWSER_ORIGIN: extensionOrigin, REWORD_BROWSER_HOST_DIAGNOSTICS: '1'},
        stdio: ['ignore', 'ignore', 'pipe', 'pipe', 'pipe'],
    });
    browser.on('error', error => { result.launchError = error.code; });
    if (engineHost) {
        result.nativeHostStages = [];
        createInterface({input: browser.stderr}).on('line', line => {
            const match = line.match(/^REWORD_BROWSER_HOST ([1-9]|10|11)$/);
            if (match && result.nativeHostStages.length < 256) {
                result.nativeHostStages.push(Number(match[1]));
            }
        });
    } else {
        browser.stderr.resume();
    }
    const call = devTools(browser);
    result.engine = (await call('Browser.getVersion')).product;
    if (useDevToolsInstall && (await call('Extensions.loadUnpacked', {path: extension})).id !== id) {
        throw new Error('Unexpected extension identity');
    }
    let application;
    const setPolicy = (...change) => execFileSync(
        join(dirname(engineHost), 'reword_browser_policy_fixture'),
        [join(root, 'reword'), application, ...change], {timeout: 5000});
    if (engineHost) {
        const {processInfo} = await call('SystemInfo.getProcessInfo');
        const parent = processInfo.find(process => process.type === 'browser');
        application = await readlink('/proc/' + parent.id + '/exe');
        setPolicy('rule', 'allow');
    }
    const {targetId} = await call('Target.createTarget', {url: origin});
    const {sessionId} = await call('Target.attachToTarget', {targetId, flatten: true});
    const evaluate = async expression => {
        const reply = await call('Runtime.evaluate', {expression, returnByValue: true}, sessionId);
        if (reply.exceptionDetails) throw new Error('Fixture page evaluation failed');
        return reply.result.value;
    };
    await call('Page.bringToFront', {}, sessionId);
    if (!await waitFor(() => evaluate('location.origin === ' + JSON.stringify(origin) +
        ' && document.readyState === "complete" && document.hasFocus()'), 6000)) {
        throw new Error('The owned fixture page did not receive focus');
    }
    if (engineHost) {
        result.stage = 'host-warmup';
        result.hostReady = await waitFor(
            () => evaluate("document.documentElement.dataset.rewordProbeReady === 'yes'"), 7000);
        if (!result.hostReady) throw new Error('The browser host did not warm up');
    }

    const focus = (field, caret = 0) => evaluate(
        `(() => { const field = document.getElementById('${field}');
           field.focus(); field.setSelectionRange(${caret}, ${caret}); })()`);
    const insert = text => call('Input.insertText', {text}, sessionId);
    const typeWord = async (word = 'teh') => {
        if (engineHost) {
            await insert(word);
            await insert(' ');
        } else {
            await insert(word + ' ');
        }
    };
    const pressKey = async (key, windowsVirtualKeyCode, modifiers = 0, code = key) => {
        for (const type of ['rawKeyDown', 'keyUp']) {
            await call('Input.dispatchKeyEvent', {type, key, code, windowsVirtualKeyCode, modifiers},
                       sessionId);
        }
    };
    const status = field => evaluate(
        `(() => { const field = document.getElementById('${field}');
           return {value: field.value, caret: field.selectionStart,
                   outcome: field.dataset.outcome, requested: field.dataset.requested,
                   pageValue: field.dataset.pageValue, pageEvent: field.dataset.pageEvent,
                   pageEvents: Number(field.dataset.pageEvents ?? 0), pageVeto: field.dataset.pageVeto,
                   undoOutcome: field.dataset.undoOutcome}; })()`);
    const outcome = field => waitFor(async () => {
        const current = await status(field);
        return current.outcome && current;
    }, 7000);

    for (const field of ['plain', 'multiline']) {
        result.stage = field;
        await focus(field, 7);
        await typeWord();
        const observed = await outcome(field);
        result[field] = observed.value === 'prefix the suffix' && observed.caret === 11 &&
            observed.outcome === 'applied';
        if (!result[field]) throw new Error(observed.outcome || 'No browser reply');
    }
    for (const field of ['stale', 'blur']) {
        result.stage = field;
        await focus(field);
        await typeWord();
        const requested = engineHost ? async () => (await status(field)).requested :
            async () => (await hostCases()).includes(field);
        if (!await waitFor(requested)) {
            throw new Error('The native host did not record the pending request');
        }
        if (field === 'stale') await insert('x');
        else {
            await focus('password');
            await focus('blur', 4);
        }
        const observed = await outcome(field);
        result[field] = observed.value === (field === 'stale' ? 'teh x' : 'teh ') &&
            observed.outcome === 'stale';
    }

    result.stage = 'protected-controls';
    await focus('password');
    await typeWord();
    await focus('readonly');
    await typeWord();
    await focus('otp');
    await typeWord();
    const password = await status('password');
    const readonly = await status('readonly');
    const otp = await status('otp');
    result.password = password.value === 'teh ' && !password.requested;
    result.readonly = readonly.value === '' && !readonly.requested;
    result.oneTimeCode = otp.value === 'teh ' && !otp.requested;
    if (!engineHost) result.hostCases = await hostCases();
    if (!['plain', 'multiline', 'stale', 'blur', 'password', 'readonly', 'oneTimeCode']
        .every(key => result[key]) ||
        (!engineHost && result.hostCases.join() !== 'plain,multiline,stale,blur')) {
        throw new Error('A browser transport check failed');
    }
    if (engineHost) {
        result.stage = 'pending-policy-change';
        const replies = () => result.nativeHostStages.filter(stage => stage === 5).length;
        const pendingEdit = async (field, change) => {
            const before = replies();
            await focus(field);
            await typeWord();
            if (!await waitFor(() => replies() >= before + 2)) {
                throw new Error('The native host did not answer the pending correction');
            }
            await change();
            const observed = await outcome(field);
            return observed.value === 'teh ' && observed.outcome === 'stale';
        };
        result.pendingPolicyRevoked = await pendingEdit('revoke', () => setPolicy('rule', 'deny'));
        if (!result.pendingPolicyRevoked) throw new Error('The revoked correction was not discarded');
        setPolicy('rule', 'allow');
        result.pendingPauseResume = await pendingEdit('pause-cycle', () => {
            setPolicy('pauseUntil', '-1');
            setPolicy('pauseUntil', '0');
        });
        if (!result.pendingPauseResume) throw new Error('Pause and Resume left a stale correction valid');
        result.pendingCaretMove = await pendingEdit('pending-caret', async () => {
            await pressKey('ArrowLeft', 37);
            if ((await status('pending-caret')).caret !== 3) throw new Error('The caret did not move left');
            await pressKey('ArrowRight', 39);
            if ((await status('pending-caret')).caret !== 4) throw new Error('The caret did not return');
        });
        if (!result.pendingCaretMove) throw new Error('Caret movement left a stale correction valid');

        result.stage = 'live-policy';
        result.policy = {};
        const verifyWord = async (key, word, expected, textRequest = true) => {
            await evaluate(`(() => { const field = document.getElementById('policy');
                field.value = ''; delete field.dataset.outcome; delete field.dataset.requested; })()`);
            await focus('policy');
            await typeWord(word);
            const observed = await outcome('policy');
            result.policy[key] = observed.value === expected + ' ' &&
                observed.outcome === (word === expected ? 'unchanged' : 'applied') &&
                Boolean(observed.requested) === textRequest;
        };
        for (const mode of ['shortcut', 'deny', 'remove']) {
            setPolicy('rule', mode);
            await verifyWord(mode, 'teh', 'teh', false);
        }
        setPolicy('rule', 'allow');
        setPolicy('pauseUntil', '-1');
        await verifyWord('paused', 'teh', 'teh', false);
        setPolicy('pauseUntil', '0');
        await verifyWord('resumed', 'teh', 'the');
        setPolicy('browsers', 'false');
        await verifyWord('browserDisabled', 'teh', 'teh', false);
        setPolicy('browsers', 'true');
        await verifyWord('protectedToken', 'www.teh', 'www.teh');
        await verifyWord('persian', 'سلاام', 'سلام');
        setPolicy('persian', 'false');
        await verifyWord('persianDisabled', 'سلاام', 'سلاام');
        await verifyWord('englishStillEnabled', 'teh', 'the');
        setPolicy('persian', 'true');
        setPolicy('english', 'false');
        await verifyWord('englishDisabled', 'teh', 'teh');
        await verifyWord('persianStillEnabled', 'سلاام', 'سلام');
        if (!Object.values(result.policy).every(Boolean)) {
            throw new Error('A browser live policy check failed');
        }
        result.stage = 'retry-cycle';
        setPolicy('english', 'true');
        await focus('retry', 7);
        result.retry = [];
        const removeWord = async () => {
            for (let index = 0; index < 4; index++) await pressKey('Backspace', 8);
            const erased = await status('retry');
            if (erased.value !== 'prefix suffix' || erased.caret !== 7) {
                throw new Error('The browser did not erase the previous attempt in place');
            }
        };
        const verifyRetry = async (expected, state) => {
            await evaluate("delete document.getElementById('retry').dataset.outcome");
            await typeWord();
            const observed = await outcome('retry');
            return observed.value === 'prefix ' + expected + ' suffix' &&
                observed.caret === 11 && observed.outcome === state;
        };
        for (const [attempt, expected] of ['the', 'teh', 'the'].entries()) {
            if (attempt) await removeWord();
            result.retry.push(await verifyRetry(expected, attempt === 1 ? 'skipped' : 'applied'));
        }
        if (!result.retry.every(Boolean)) throw new Error('The browser rewrite did not alternate');
        await pressKey('ArrowLeft', 37);
        await pressKey('ArrowRight', 39);
        await removeWord();
        result.retryAfterCaretMove = await verifyRetry('the', 'applied');
        await removeWord();
        await focus('plain', 0);
        await focus('retry', 7);
        result.retryAfterFocusChange = await verifyRetry('the', 'applied');
        await insert('x');
        await pressKey('Backspace', 8);
        await removeWord();
        result.retryAfterContinuing = await verifyRetry('the', 'applied');
        if (!result.retryAfterCaretMove || !result.retryAfterFocusChange || !result.retryAfterContinuing) {
            throw new Error('The rewrite cycle persisted after leaving the original edit');
        }

        result.stage = 'page-events-and-undo';
        // The dictionary-validated common correction keeps this range test
        // independent of the time budget for a full Hunspell suggestion search.
        const misspelling = 'adress';
        const replacement = 'address';
        await focus('undo', 7);
        const correctionStageStart = result.nativeHostStages.length;
        await typeWord(misspelling);
        const corrected = await outcome('undo');
        const original = 'prefix ' + misspelling + ' suffix';
        result.correctionChecks = {
            applied: corrected.outcome === 'applied',
            valueMatches: corrected.value === 'prefix ' + replacement + ' suffix',
            caretMatches: corrected.caret === 8 + replacement.length,
            pageModelMatches: Boolean(corrected.value) && corrected.pageValue === corrected.value,
            pageEventMatches: corrected.pageEvent === 'insertReplacementText',
            eventCountMatches: corrected.pageEvents === 1,
        };
        result.correctionOutcome = corrected.outcome ?? 'no-outcome';
        result.pageCorrection = Object.values(result.correctionChecks).every(Boolean);
        result.correctionHostStages = result.nativeHostStages.slice(correctionStageStart);
        await pressKey('z', 90, 3, 'KeyZ'); // CDP Alt and Ctrl bits.
        const undone = await waitFor(async () => {
            const current = await status('undo');
            return current.value === original && current;
        });
        result.liveUndo = undone.value === original && undone.caret === 8 + misspelling.length &&
            undone.pageValue === original && undone.pageEvent === 'historyUndo' && undone.pageEvents === 2;
        await focus('veto', 7);
        await typeWord();
        const vetoed = await outcome('veto');
        result.pageVeto = vetoed.value === 'prefix teh suffix' && vetoed.pageVeto === 'yes' &&
            vetoed.pageEvents === 0;
        if (!result.pageCorrection || !result.liveUndo || !result.pageVeto) {
            throw new Error('A browser page event or Undo check failed');
        }

        result.stage = 'undo-guards';
        const prepareUndo = async ({word = 'teh', pageMode = '', maxLength = -1} = {}) => {
            await evaluate(`(() => { const field = document.getElementById('undo');
                for (const key of Object.keys(field.dataset)) delete field.dataset[key];
                field.dataset.pageMode = ${JSON.stringify(pageMode)};
                if (${maxLength} < 0) field.removeAttribute('maxlength');
                else field.maxLength = ${maxLength};
                field.value = 'prefix suffix'; })()`);
            await focus('undo', 7);
            await typeWord(word);
            return outcome('undo');
        };
        const undoKey = () => pressKey('z', 90, 3, 'KeyZ');
        const undoResult = () => waitFor(async () => {
            const current = await status('undo');
            return current.undoOutcome && current;
        });
        result.undoGuards = {};
        await prepareUndo();
        await insert('x');
        await undoKey();
        const continued = await status('undo');
        result.undoGuards.laterTyping = continued.value === 'prefix the xsuffix' &&
            continued.pageEvents === 1 && !continued.undoOutcome;

        await prepareUndo();
        await evaluate(`(() => { const field = document.getElementById('undo');
            field.value = 'prefix the target'; field.setSelectionRange(11, 11); })()`);
        await undoKey();
        const silent = await undoResult();
        result.undoGuards.silentPageEdit = silent.value === 'prefix the target' &&
            silent.undoOutcome === 'declined' && silent.pageEvents === 1;

        await prepareUndo();
        setPolicy('pauseUntil', '-1');
        setPolicy('pauseUntil', '0');
        await undoKey();
        const paused = await undoResult();
        result.undoGuards.pauseRoundTrip = paused.value === 'prefix the suffix' &&
            paused.undoOutcome === 'declined' && paused.pageEvents === 1;

        await prepareUndo({pageMode: 'vetoUndo'});
        await undoKey();
        const vetoedUndo = await undoResult();
        result.undoGuards.pageVetoUndo = vetoedUndo.value === 'prefix the suffix' &&
            vetoedUndo.undoOutcome === 'declined' && vetoedUndo.pageVeto === 'yes' &&
            vetoedUndo.pageEvents === 1;

        const changedBefore = await prepareUndo({pageMode: 'changeBefore'});
        result.undoGuards.pageChangeBefore = changedBefore.value === 'prefix teh target' &&
            changedBefore.outcome === 'declined' && changedBefore.pageEvents === 0;
        const changedAfter = await prepareUndo({pageMode: 'changeAfter'});
        await undoKey();
        const after = await status('undo');
        result.undoGuards.pageChangeAfter = changedAfter.outcome === 'changed' &&
            after.value === 'prefix the target' && !after.undoOutcome && after.pageEvents === 1;

        const limited = await prepareUndo({word: misspelling, maxLength: original.length});
        result.undoGuards.maxLength = limited.value === original && limited.outcome === 'declined' &&
            limited.pageEvents === 0;

        setPolicy('undoShortcut', 'Ctrl+Alt+K');
        await prepareUndo();
        await undoKey();
        const oldKey = await status('undo');
        await pressKey('k', 75, 3, 'KeyK');
        const custom = await undoResult();
        result.undoGuards.customShortcut = oldKey.value === 'prefix the suffix' &&
            custom.value === 'prefix teh suffix' && custom.undoOutcome === 'applied' &&
            custom.pageEvent === 'historyUndo' && custom.pageEvents === 2;
        if (!Object.values(result.undoGuards).every(Boolean)) {
            throw new Error('A browser Undo guard failed');
        }
    }
    result.stage = 'complete';
    await call('Browser.close');
} catch (error) {
    result.error = error.message;
    process.exitCode = 1;
} finally {
    if (browser && browser.exitCode === null && browser.signalCode === null) browser.kill();
    server.close();
    await writeJSON(join(output, 'report.json'), result);
}
