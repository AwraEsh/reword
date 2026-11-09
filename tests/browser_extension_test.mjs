import assert from 'node:assert/strict';
import {createHash, webcrypto} from 'node:crypto';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const extension = new URL('../browser/chromium/', import.meta.url);
const read = name => readFile(new URL(name, extension), 'utf8');

test('the unpacked extension has a fixed identity and asks for site access at runtime', async () => {
    const manifest = JSON.parse(await read('manifest.json'));
    const header = await readFile(new URL('../src/platform/browser_extension_origin.h', import.meta.url), 'utf8');
    const id = createHash('sha256').update(Buffer.from(manifest.key, 'base64')).digest('hex')
        .slice(0, 32).replace(/[0-9a-f]/g, value => String.fromCharCode(97 + parseInt(value, 16)));
    assert.ok(header.includes('chrome-extension://' + id + '/'));
    assert.equal(manifest.manifest_version, 3);
    assert.deepEqual(manifest.optional_host_permissions, ['http://*/*', 'https://*/*']);
    assert.equal(manifest.host_permissions, undefined);
    assert.equal(manifest.content_scripts, undefined);
    assert.equal(manifest.externally_connectable, undefined);
});

test('the worker forwards only recognized requests from an authorized top-frame site', async () => {
    let handler;
    let permission = true;
    const forwarded = [];
    const chrome = {
        runtime: {id: 'fixture', onMessage: {addListener: listener => { handler = listener; }}},
        permissions: {contains: async ({origins}) => {
            assert.deepEqual([...origins], ['https://example.com/*']);
            return permission;
        }},
        scripting: {getRegisteredContentScripts: async ({ids}) =>
            ids.map(id => ({id, matches: ['https://example.com/*']}))},
    };
    const context = vm.createContext({chrome, URL, TextEncoder, crypto: webcrypto, importScripts() {},
        createNativeChannel: host => (payload, reply) => {
            assert.equal(host, 'org.reword.browser');
            forwarded.push(payload);
            reply({revision: '1'});
        }});
    vm.runInContext(await read('sites.js'), context);
    vm.runInContext(await read('worker.js'), context);
    const sender = {id: 'fixture', tab: {id: 2}, frameId: 0,
        origin: 'https://example.com', url: 'https://example.com/form'};
    const send = (request, caller) => new Promise(resolve => {
        if (handler(request, caller, resolve) !== true) resolve(undefined);
    });
    assert.equal((await send({policy: true}, sender)).revision, '1');
    assert.equal(await send({policy: true}, {...sender, frameId: 1}), undefined);
    assert.equal(await send({policy: true}, {...sender, origin: 'https://other.example'}), undefined);
    assert.equal(await send({policy: true, extra: true}, sender), undefined);
    permission = false;
    assert.equal((await send({policy: true}, sender)).error, 'site-disabled');
    assert.equal(forwarded.length, 1);
});
