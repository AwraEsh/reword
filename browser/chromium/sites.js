function sitePattern(address) {
    try {
        const url = new URL(address);
        if (url.protocol === 'http:' || url.protocol === 'https:') {
            return url.protocol + '//' + url.hostname + '/*';
        }
    } catch {
        // Browser pages and opaque origins are outside the site grant.
    }
    return null;
}

async function siteScriptId(pattern) {
    const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(pattern));
    return 'reword-' + Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
}

async function registeredSite(pattern) {
    const id = await siteScriptId(pattern);
    const registered = await chrome.scripting.getRegisteredContentScripts({ids: [id]});
    return registered.some(script => script.id === id && script.matches.includes(pattern));
}

const rewordContentFiles = ['content.js', 'content-entry.js'];

async function registerSite(pattern) {
    if (!await registeredSite(pattern)) {
        const id = await siteScriptId(pattern);
        await chrome.scripting.registerContentScripts([{id, matches: [pattern], js: rewordContentFiles}]);
    }
}

async function unregisterSite(pattern) {
    const id = await siteScriptId(pattern);
    await chrome.scripting.unregisterContentScripts({ids: [id]});
}
