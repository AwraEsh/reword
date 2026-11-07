// Runs inside test-native-linux.py's owned X11 and D-Bus session.
import {execFileSync, spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {chmod, copyFile, mkdir, readFile, readlink, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {join} from 'node:path';
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
const server = createServer((_request, response) => {
    response.writeHead(200, {'Content-Type': 'text/html', 'Content-Security-Policy': "default-src 'none'"});
    response.end(page);
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
            if (match && result.nativeHostStages.length < 128) {
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
    const setPolicy = (...change) => execFileSync('/usr/bin/python3',
        [join(fixture, 'settings.py'), join(root, 'reword'), application, ...change], {timeout: 5000});
    if (engineHost) {
        const {processInfo} = await call('SystemInfo.getProcessInfo');
        const parent = processInfo.find(process => process.type === 'browser');
        application = await readlink('/proc/' + parent.id + '/exe');
        setPolicy();
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
    const status = field => evaluate(
        `(() => { const field = document.getElementById('${field}');
           return {value: field.value, caret: field.selectionStart,
                   outcome: field.dataset.outcome, requested: field.dataset.requested}; })()`);
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
        const before = replies();
        await focus('revoke');
        await typeWord();
        if (!await waitFor(() => replies() >= before + 2)) {
            throw new Error('The native host did not answer the pending correction');
        }
        setPolicy('rule', 'deny');
        const revoked = await outcome('revoke');
        result.pendingPolicyRevoked = revoked.value === 'teh ' && revoked.outcome === 'stale';
        if (!result.pendingPolicyRevoked) throw new Error('The revoked correction was not discarded');
        setPolicy('rule', 'allow');

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
