// Runs in an owned X11 session or a disposable Windows CI account.
import {execFileSync, spawn, spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {once} from 'node:events';
import {existsSync} from 'node:fs';
import {chmod, copyFile, cp, mkdir, readFile, readlink, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {dirname, join, normalize} from 'node:path';
import {createInterface} from 'node:readline';
import {setTimeout as sleep} from 'node:timers/promises';
import {fileURLToPath} from 'node:url';

const [root, output, executable, engineHost, x11Mode, python] = process.argv.slice(2);
const production = process.env.REWORD_PROBE_EXTENSION === '1';
const windows = process.platform === 'win32';
const windowsGui = windows && x11Mode === 'gui';
const supported = windows ? engineHost && production && (!x11Mode || windowsGui) && /[\\/]chrome\.exe$/i.test(executable ?? '') :
    ['helium', 'google-chrome'].includes(executable);
if (!root || !output || !supported) {
    throw new Error('Expected an owned test directory, a report path and a supported test browser');
}
const fixture = fileURLToPath(new URL('.', import.meta.url));
const packagedHost = process.env.REWORD_PROBE_BROWSER_HOST;
const installedWindows = windows && Boolean(packagedHost);
const extensionSources = packagedHost ? join(dirname(packagedHost),
    windows ? 'browser/chromium' : '../share/reword/browser/chromium') :
    fileURLToPath(new URL('../../browser/chromium/', import.meta.url));
const extension = join(root, 'extension');
const profile = join(root, 'profile');
const hostName = engineHost ? 'org.reword.browser' : 'org.reword.transport_probe';
const hostPath = packagedHost || engineHost || join(root, 'native-host');
const hostEnvironment = {...process.env, REWORD_DATA_DIR: join(root, 'reword')};
if (packagedHost) {
    for (const key of ['LD_LIBRARY_PATH', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH',
        'REWORD_DICTIONARY_DIR', 'REWORD_PROBE_WINDOWS_FIXTURE_PATH', 'REWORD_PROBE_PWSH']) {
        delete hostEnvironment[key];
    }
}
const fixtureEnvironment = installedWindows ? {...process.env} : process.env;
if (installedWindows) {
    if (!process.env.REWORD_PROBE_WINDOWS_FIXTURE_PATH || !process.env.REWORD_PROBE_PWSH) {
        throw new Error('The installed Windows test needs an explicit fixture PATH and PowerShell path');
    }
    for (const key of Object.keys(fixtureEnvironment)) {
        if (key.toLowerCase() === 'path') delete fixtureEnvironment[key];
    }
    fixtureEnvironment.PATH = process.env.REWORD_PROBE_WINDOWS_FIXTURE_PATH;
}
const hostLog = join(root, 'host-cases');
const extensionManifest = JSON.parse(await readFile(join(extensionSources, 'manifest.json'), 'utf8'));
const extensionKey = engineHost ? extensionManifest.key : undefined;
const id = createHash('sha256').update(extensionKey ? Buffer.from(extensionKey, 'base64') : extension)
    .digest('hex').slice(0, 32)
    .replace(/[0-9a-f]/g, character => String.fromCharCode(97 + parseInt(character, 16)));
const extensionOrigin = 'chrome-extension://' + id + '/';
const result = {browser: executable, compiledEngine: Boolean(engineHost),
                packagedRuntime: Boolean(packagedHost), stage: 'setup'};
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
let gui;
let nativeEditor;
let guiUndoRequests = 0;
let guiLiveAttempts = 0;
let browserPid;
let windowsRegistered = false;
const x11 = (pid, action) => spawnSync(python, [join(fixture, 'x11_input.py'),
    root, String(pid), action], {stdio: 'ignore', timeout: 3500}).status === 0;
const nativeFixture = windowsGui ? join(dirname(engineHost), 'reword_win_fixture.exe') : null;
const windowsInput = (pid, action) => spawnSync(nativeFixture, ['--input', String(pid), action],
    {stdio: 'ignore', windowsHide: true, timeout: 3500, env: fixtureEnvironment}).status === 0;
const policyFor = (application, ...change) => execFileSync(
    join(dirname(engineHost), 'reword_browser_policy_fixture' + (windows ? '.exe' : '')),
    [join(root, 'reword'), application, ...change], {encoding: 'utf8', timeout: 5000, env: fixtureEnvironment}).trim();
await mkdir(output, {mode: 0o700});
try {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const origin = 'http://127.0.0.1:' + server.address().port;
    await mkdir(extension);
    const originConfig = 'const PROBE_ORIGIN = ' + JSON.stringify(origin) + ';\n' +
        'const PROBE_ENGINE = ' + Boolean(engineHost) + ';\n' +
        'const PROBE_ROUTE = ' + (x11Mode === 'gui') + ';\n';
    if (production) {
        await cp(extensionSources, extension, {recursive: true});
        // Pre-grant only this test host so the probe can run without a UI prompt.
        await writeJSON(join(extension, 'manifest.json'),
            {...extensionManifest, host_permissions: ['http://127.0.0.1/*']});
    } else {
        await writeFile(join(extension, 'content.js'),
            originConfig + await readFile(join(extensionSources, 'content.js'), 'utf8') +
            await readFile(join(fixture, 'content.js'), 'utf8'));
        await copyFile(join(extensionSources, 'native.js'), join(extension, 'native.js'));
        const workerConfig = originConfig + 'const PROBE_HOST = ' + JSON.stringify(hostName) + ';\n';
        await writeFile(join(extension, 'worker.js'),
            workerConfig + await readFile(join(fixture, 'worker.js'), 'utf8'));
        await writeJSON(join(extension, 'manifest.json'), {
            manifest_version: 3, name: 'Reword private transport fixture', version: '1.0.0', key: extensionKey,
            permissions: ['nativeMessaging'], background: {service_worker: 'worker.js'},
            content_scripts: [{matches: ['http://127.0.0.1/*'], js: ['content.js']}],
        });
    }

    if (engineHost) {
        await mkdir(profile, {mode: 0o700});
        if (windows) {
            const key = 'HKCU\\Software\\Google\\Chrome\\NativeMessagingHosts\\org.reword.browser';
            const existing = spawnSync('reg.exe', ['query', key, '/ve'], {timeout: 5000});
            if (existing.error || existing.status !== (installedWindows ? 0 : 1))
                throw new Error('The Windows test registry entry did not match the setup mode');
            const rejected = spawnSync(hostPath, [extensionOrigin, '--parent-window=0'],
                {timeout: 5000, env: hostEnvironment, input: Buffer.from([2, 0, 0, 0, 123, 125])});
            result.untrustedLaunchRejected = rejected.status === 2 && !rejected.stdout.length &&
                !existsSync(join(hostEnvironment.REWORD_DATA_DIR, 'reword.sqlite'));
            if (!result.untrustedLaunchRejected) throw new Error('A non-browser process reached the native host');
            if (installedWindows) {
                result.windowsRegistrationFromInstaller = true;
            } else {
                execFileSync(hostPath, ['--register-browser', 'chrome'], {timeout: 5000, env: hostEnvironment});
                windowsRegistered = true;
            }
        } else {
            execFileSync(hostPath, ['--register-browser', profile], {timeout: 5000, env: hostEnvironment});
        }
    } else {
        await copyFile(join(fixture, 'host.py'), hostPath);
        await chmod(hostPath, 0o700);
        await writeFile(join(root, 'expected-origin'), extensionOrigin);
        const hosts = join(profile, 'NativeMessagingHosts');
        await mkdir(hosts, {recursive: true});
        await writeJSON(join(hosts, hostName + '.json'), {
            name: hostName, description: 'Reword transport fixture', path: hostPath, type: 'stdio',
            allowed_origins: [extensionOrigin],
        });
    }

    if (x11Mode === 'gui') {
        result.stage = 'desktop-gui';
        if (windowsGui) policyFor(normalize(executable), 'rule', 'allow');
        gui = spawn(join(dirname(installedWindows ? hostPath : engineHost), windowsGui ? 'reword.exe' : 'reword'), [], {
            env: {...hostEnvironment,
                  QT_QPA_PLATFORM: windowsGui ? 'windows' : 'xcb', QT_QUICK_BACKEND: 'software',
                  REWORD_RETRY_DIAGNOSTICS: '1'},
            stdio: ['ignore', 'ignore', 'pipe'],
        });
        gui.on('error', error => { result.guiLaunchError = error.code; });
        createInterface({input: gui.stderr}).on('line', line => {
            if (/^RTRY \d+ 18 -?\d+$/.test(line)) ++guiUndoRequests;
            if (/^RTRY \d+ 1 -?\d+$/.test(line)) ++guiLiveAttempts;
        });
        result.guiForeground = await waitFor(() => gui.pid &&
            (windowsGui ? windowsInput(gui.pid, 'foreground') : x11(gui.pid, 'foreground')), 10000);
        if (!result.guiForeground) {
            throw new Error('The Reword GUI did not reach the owned desktop foreground');
        }
    }
    const useDevToolsInstall = windows || executable === 'google-chrome';
    result.stage = 'browser';
    browser = spawn(executable, [
        '--remote-debugging-pipe', '--user-data-dir=' + profile,
        useDevToolsInstall ? '--enable-unsafe-extension-debugging' : '--load-extension=' + extension,
        '--no-first-run', '--no-default-browser-check', '--disable-background-networking',
        '--disable-sync', '--no-proxy-server', '--password-store=basic',
        windows ? '--disable-gpu' : '--ozone-platform=x11',
        '--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE 127.0.0.1', 'about:blank',
    ], {
        env: {...hostEnvironment, HOME: root, REWORD_BROWSER_HOST_DIAGNOSTICS: '1'},
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
    const setPolicy = (...change) => policyFor(application, ...change);
    const isPaused = () => setPolicy('pauseUntil', 'get') === 'paused';
    if (engineHost) {
        const {processInfo} = await call('SystemInfo.getProcessInfo');
        const parent = processInfo.find(process => process.type === 'browser');
        browserPid = parent.id;
        application = windows ? execFileSync(process.env.REWORD_PROBE_PWSH || 'pwsh.exe', ['-NoProfile', '-NonInteractive', '-Command',
            '(Get-Process -Id ' + Number(browserPid) + ').Path'], {encoding: 'utf8', timeout: 10000}).trim() :
            await readlink('/proc/' + parent.id + '/exe');
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
    if (engineHost && !production) {
        result.stage = 'host-warmup';
        result.hostReady = await waitFor(
            () => evaluate("document.documentElement.dataset.rewordProbeReady === 'yes'"), 7000);
        if (!result.hostReady) throw new Error('The browser host did not warm up');
    }
    if (x11Mode && !windowsGui) {
        result.x11Mode = x11Mode;
        result.browserForeground = await waitFor(() => x11(browserPid, 'foreground'));
        if (!result.browserForeground) throw new Error('The browser was not the active X11 window');
    }
    if (windowsGui) {
        result.stage = 'desktop-shortcuts';
        result.browserForeground = await waitFor(() => windowsInput(browserPid, 'foreground'), 5000);
        if (!result.browserForeground) throw new Error('Chrome did not become the foreground Windows app');
        result.guiPause = windowsInput(browserPid, 'pause') && await waitFor(isPaused);
        result.guiResume = result.guiPause && windowsInput(browserPid, 'pause') &&
            await waitFor(() => !isPaused());
        if (!result.guiResume) throw new Error('Reword did not receive Windows Pause and Resume');
    } else if (gui) {
        result.stage = 'desktop-shortcuts';
        result.guiPause = x11(browserPid, 'pause') && await waitFor(() => x11(browserPid, 'paused'));
        result.guiResume = result.guiPause && x11(browserPid, 'pause') &&
            await waitFor(() => x11(browserPid, 'resumed'));
        if (!result.guiResume) throw new Error('The GUI did not handle the physical Pause shortcut');
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

    if (production) {
        result.stage = 'unpacked-extension';
        await focus('undo', 7);
        await typeWord();
        result.siteStartsDisabled = (await status('undo')).value === 'prefix teh suffix' &&
            !result.nativeHostStages.length;
        const worker = await waitFor(async () => {
            const {targetInfos} = await call('Target.getTargets');
            return targetInfos.find(target => target.type === 'service_worker' &&
                target.url === extensionOrigin + 'worker.js');
        });
        if (!worker) throw new Error('The unpacked extension worker was not available');
        const workerSession = (await call('Target.attachToTarget',
            {targetId: worker.targetId, flatten: true})).sessionId;
        const configureSite = async operation => {
            const reply = await call('Runtime.evaluate', {
                expression: operation + '(sitePattern(' + JSON.stringify(origin) + '))',
                awaitPromise: true, returnByValue: true,
            }, workerSession);
            if (reply.exceptionDetails) throw new Error('The extension site registration failed');
        };
        const prepareWindowsCorrection = async tag => {
            const url = origin + '/?' + tag;
            await call('Page.navigate', {url}, sessionId);
            if (!await waitFor(() => evaluate(`location.search === '?${tag}' && document.readyState === 'complete'`)))
                return false;
            const injection = await call('Runtime.evaluate', {expression: `(async () => {
                const [tab] = (await chrome.tabs.query({})).filter(tab => tab.url === ${JSON.stringify(url)});
                if (!tab) return false;
                await chrome.scripting.executeScript({target: {tabId: tab.id}, files: rewordContentFiles});
                return true;
            })()`, awaitPromise: true, returnByValue: true}, workerSession);
            if (injection.result?.value !== true) return false;
            await focus('undo', 7);
            await typeWord();
            return waitFor(async () => {
                const current = await status('undo');
                return current.value === 'prefix the suffix' && current.pageEvent === 'insertReplacementText';
            });
        };
        await configureSite('registerSite');
        await call('Page.navigate', {url: origin + '/?enabled'}, sessionId);
        if (windows) {
            const sendHost = async payload => (await call('Runtime.evaluate', {
                expression: `new Promise(resolve => sendNative(${JSON.stringify(payload)}, resolve))`,
                awaitPromise: true, returnByValue: true,
            }, workerSession)).result?.value;
            const policy = await sendHost({policy: true});
            result.windowsHostPolicy = typeof policy?.revision === 'string';
            if (!result.windowsHostPolicy) {
                result.windowsHostError = policy?.error;
                throw new Error('Chrome could not exchange policy with the Windows host');
            }
            // 26 bytes puts the Windows text-mode EOF character in the binary frame header.
            const control = {ignored: '012345678901'};
            const controlReply = await sendHost(control);
            result.windowsBinaryControlByte = Buffer.byteLength(JSON.stringify(control)) === 26 &&
                JSON.stringify(controlReply) === '{}';
            if (!result.windowsBinaryControlByte) throw new Error('The Windows pipe did not preserve a binary header');
        }
        if (!await waitFor(async () => await evaluate('location.search === "?enabled" && document.readyState === "complete"') &&
            (windows || result.nativeHostStages.includes(3)), 7000)) {
            throw new Error('The registered extension did not connect to the native host');
        }
        await focus('undo', 7);
        await typeWord();
        result.enabledSiteCorrected = Boolean(await waitFor(async () => {
            const current = await status('undo');
            return current.value === 'prefix the suffix' && current.pageEvent === 'insertReplacementText';
        }));
        if (windowsGui) {
            result.stage = 'physical-browser-undo';
            if (!windowsInput(browserPid, 'undo')) throw new Error('Windows Undo chord could not be sent');
        } else {
            await pressKey('z', 90, 3, 'KeyZ');
        }
        result.enabledSiteUndo = Boolean(await waitFor(async () => {
            const current = await status('undo');
            return current.value === 'prefix teh suffix' && current.pageEvent === 'historyUndo';
        }));
        if (windowsGui) {
            result.guiInterceptedBrowserUndo = guiUndoRequests > 0;
            if (!result.enabledSiteUndo || result.guiInterceptedBrowserUndo)
                throw new Error('The desktop Undo shortcut intercepted the browser Undo chord');
            setPolicy('undoShortcut', 'Ctrl+Alt+K');
            result.windowsCustomUndo = Boolean(await prepareWindowsCorrection('custom') &&
                windowsInput(browserPid, 'custom-undo') && await waitFor(async () => {
                    const current = await status('undo');
                    return current.value === 'prefix teh suffix' && current.pageEvent === 'historyUndo';
                }));
            if (!result.windowsCustomUndo) throw new Error('The custom Windows browser Undo chord did not restore text');
            result.windowsPauseDuringLease = Boolean(await prepareWindowsCorrection('pause') &&
                windowsInput(browserPid, 'pause') && await waitFor(isPaused));
            if (!result.windowsPauseDuringLease) throw new Error('Browser Undo prevented the Windows Pause chord');
            await sleep(180);
            const beforePauseUndo = guiUndoRequests;
            result.windowsPauseRestoredUndo = windowsInput(browserPid, 'custom-undo') &&
                await waitFor(() => guiUndoRequests > beforePauseUndo) &&
                (await status('undo')).value === 'prefix the suffix';
            if (!result.windowsPauseRestoredUndo || !windowsInput(browserPid, 'pause') ||
                !await waitFor(() => !isPaused())) throw new Error('Windows Undo did not recover during Pause');
        }
        result.noFixtureMarkers = await evaluate(
            "!document.documentElement.dataset.rewordProbeReady && !document.getElementById('undo').dataset.outcome");
        result.stage = 'disabled-site';
        await configureSite('unregisterSite');
        const previousPageEdits = await evaluate(`(() => {
            const field = document.getElementById('undo');
            field.value = 'prefix suffix';
            return Number(field.dataset.pageEvents ?? 0);
        })()`);
        await focus('undo', 7);
        const frames = result.nativeHostStages.length;
        const beforeDisabledSiteAttempts = guiLiveAttempts;
        await typeWord();
        await sleep(400);
        const disabledSite = await status('undo');
        if (windowsGui)
            result.windowsDisabledSiteNativeAttempts = guiLiveAttempts - beforeDisabledSiteAttempts;
        result.disabledSiteUntouched = disabledSite.value === 'prefix teh suffix' &&
            disabledSite.pageEvents === previousPageEdits &&
            result.nativeHostStages.length === frames &&
            (!windowsGui || result.windowsDisabledSiteNativeAttempts === 0);
        if (!result.disabledSiteUntouched) {
            result.disabledSiteObservation = {
                inputUnchanged: disabledSite.value === 'prefix suffix',
                expectedInput: disabledSite.value === 'prefix teh suffix',
                correctedInput: disabledSite.value === 'prefix the suffix',
                pageEditEvents: disabledSite.pageEvents - previousPageEdits,
                fieldFocused: await evaluate("document.activeElement?.id === 'undo'"),
                documentFocused: await evaluate('document.hasFocus()'),
            };
        }
        if (!result.siteStartsDisabled || !result.enabledSiteCorrected || !result.enabledSiteUndo ||
            !result.noFixtureMarkers || !result.disabledSiteUntouched) {
            throw new Error('The unpacked extension site gate failed');
        }
        if (windowsGui) {
            result.stage = 'windows-focus-correction';
            await configureSite('registerSite');
            result.windowsFocusChangeStartedWithCorrection = Boolean(await prepareWindowsCorrection('focus'));
            if (!result.windowsFocusChangeStartedWithCorrection)
                throw new Error('The focus-change fixture did not create a browser correction');
        }
    } else {
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
            if (gui) {
                result.browserClaimedUndo = await evaluate(
                    "document.getElementById('undo').dataset.undoRoute === 'claimed'");
            }
            result.pageCorrection = Object.values(result.correctionChecks).every(Boolean);
            result.correctionHostStages = result.nativeHostStages.slice(correctionStageStart);
            const guiUndoBefore = guiUndoRequests;
            if (x11Mode) {
                result.physicalUndoSent = x11(browserPid, 'undo');
                if (!result.physicalUndoSent) throw new Error('The physical Undo chord was not sent');
            } else {
                await pressKey('z', 90, 3, 'KeyZ'); // CDP Alt and Ctrl bits.
            }
            const undone = await waitFor(async () => {
                const current = await status('undo');
                return current.value === original && current;
            });
            result.liveUndo = undone.value === original && undone.caret === 8 + misspelling.length &&
                undone.pageValue === original && undone.pageEvent === 'historyUndo' && undone.pageEvents === 2;
            if (gui) result.guiReceivedUndo = guiUndoRequests > guiUndoBefore;
            if (result.guiReceivedUndo && result.liveUndo) {
                throw new Error('The physical Undo chord reached both handlers');
            }
            if (gui && result.liveUndo) {
                const before = guiUndoRequests;
                await sleep(150);
                result.guiRegainedUndo = x11(browserPid, 'undo') &&
                    await waitFor(() => guiUndoRequests > before);
                if (!result.browserClaimedUndo || !result.guiRegainedUndo) {
                    throw new Error('Undo ownership did not return to the desktop after restoration');
                }
            }
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
            if (gui) {
                result.guiPauseDuringBrowserUndo = x11(browserPid, 'pause') &&
                    await waitFor(() => x11(browserPid, 'paused'));
                if (!result.guiPauseDuringBrowserUndo) throw new Error('Browser Undo blocked Pause');
                const before = guiUndoRequests;
                // The browser lease is rechecked on a bounded interval after Pause.
                await sleep(250);
                result.guiRegainedUndoOnPause = x11(browserPid, 'undo') &&
                    await waitFor(() => guiUndoRequests > before);
                if (!result.guiRegainedUndoOnPause || !x11(browserPid, 'pause') ||
                    !await waitFor(() => x11(browserPid, 'resumed'))) {
                    throw new Error('The GUI did not reclaim Undo and Resume after Pause');
                }
                await prepareUndo();
            }
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
            if (gui) {
                const before = guiUndoRequests;
                result.physicalCustomUndoSent = x11(browserPid, 'custom-undo');
                if (!result.physicalCustomUndoSent) throw new Error('The custom physical Undo chord was not sent');
                const custom = await undoResult();
                result.guiReceivedCustomUndo = guiUndoRequests > before;
                if (result.guiReceivedCustomUndo || custom.undoOutcome !== 'applied') {
                    throw new Error('The GUI intercepted the custom browser Undo chord');
                }
            } else {
                await pressKey('k', 75, 3, 'KeyK');
            }
            const custom = await undoResult();
            result.undoGuards.customShortcut = oldKey.value === 'prefix the suffix' &&
                custom.value === 'prefix teh suffix' && custom.undoOutcome === 'applied' &&
                custom.pageEvent === 'historyUndo' && custom.pageEvents === 2;
            if (!Object.values(result.undoGuards).every(Boolean)) {
                throw new Error('A browser Undo guard failed');
            }
        }
    }
    if (windowsGui) {
        result.stage = 'native-editor-undo';
        policyFor(nativeFixture, 'rule', 'allow');
        nativeEditor = spawn(nativeFixture, [], {stdio: ['pipe', 'pipe', 'ignore'], env: fixtureEnvironment});
        let observed = '', caret = -1;
        createInterface({input: nativeEditor.stdout}).on('line', line => {
            const snapshot = line.match(/^TEXT:(\d+):(.*)$/);
            if (snapshot) {
                caret = Number(snapshot[1]);
                observed = snapshot[2];
            }
        });
        if (!await waitFor(() => nativeEditor.pid && windowsInput(nativeEditor.pid, 'focus-fixture'), 5000) ||
            !await waitFor(() => windowsInput(nativeEditor.pid, 'foreground'), 5000))
            throw new Error('The Windows edit fixture did not receive focus after a verified click');
        const fieldContains = expected => waitFor(() => {
            nativeEditor.stdin.write('snapshot\n');
            return observed === expected && caret === expected.length;
        });
        nativeEditor.stdin.write('text:teh\n');
        if (!await fieldContains('teh')) throw new Error('The Windows edit fixture was not initialized');
        await sleep(300);
        result.windowsNativeSpaceSent = windowsInput(nativeEditor.pid, 'space');
        result.windowsNativeCorrection = result.windowsNativeSpaceSent && await fieldContains('the ');
        result.windowsNativeCaretAfterSpace = caret;
        result.windowsNativeBoundaryAtEnd = observed === 'teh ' || observed === 'the ';
        const beforeNativeUndo = guiUndoRequests;
        result.windowsNativeChordSent = windowsInput(nativeEditor.pid, 'custom-undo');
        result.windowsNativeShortcutReturned = result.windowsNativeChordSent &&
            await waitFor(() => guiUndoRequests > beforeNativeUndo);
        result.windowsNativeUndo = result.windowsNativeCorrection && result.windowsNativeShortcutReturned &&
            await fieldContains('teh ');
        if (!result.windowsNativeUndo) throw new Error('Native editor Undo was unavailable after browser focus ended');
    }
    result.stage = 'complete';
    await call('Browser.close');
} catch (error) {
    result.error = error.message;
    process.exitCode = 1;
} finally {
    await Promise.all([browser, gui, nativeEditor].map(async child => {
        if (child?.pid && child.exitCode === null && child.signalCode === null) {
            const exited = once(child, 'exit');
            child.kill();
            await Promise.race([exited, sleep(2000)]);
        }
    }));
    server.close();
    if (windowsRegistered) {
        const cleanup = spawnSync(hostPath, ['--unregister-browser', 'chrome'],
            {timeout: 5000, env: hostEnvironment});
        result.windowsRegistrationRemoved = cleanup.status === 0;
        if (!result.windowsRegistrationRemoved) process.exitCode = 1;
    }
    await writeJSON(join(output, 'report.json'), result);
}
