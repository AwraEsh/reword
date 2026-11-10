importScripts('native.js', 'sites.js');

const sendNative = createNativeChannel('org.reword.browser');
const validNumber = value => typeof value === 'string' && /^\d{1,20}$/.test(value);

function nativeRequest(request) {
    if (!request || Object.keys(request).length !== 1) return null;
    if (request.policy === true) return {policy: true};
    const route = request.route;
    if (route && typeof route === 'object') {
        const size = Object.keys(route).length;
        if (size === 1 && validNumber(route.releaseUndo)) return {releaseUndo: route.releaseUndo};
        if (size === 2 && validNumber(route.claimUndo) && validNumber(route.revision)) {
            return {claimUndo: route.claimUndo, revision: route.revision};
        }
        return null;
    }
    const word = request.word;
    if (!word || typeof word.text !== 'string' || !word.text.length || word.text.length >= 384 ||
        !Number.isInteger(word.caret) || word.caret < 1 || word.caret > word.text.length ||
        typeof word.boundary !== 'string' || word.boundary.length !== 1 ||
        !' .,!؟?،'.includes(word.boundary) || !validNumber(word.revision)) return null;
    const {text, caret, boundary, revision, continuation} = word;
    return validNumber(continuation) ? {text, caret, boundary, revision, continuation} :
        {text, caret, boundary, revision};
}

chrome.runtime.onMessage.addListener((request, sender, reply) => {
    const pattern = sitePattern(sender.url);
    if (!pattern || sender.id !== chrome.runtime.id || !sender.tab || sender.frameId !== 0 ||
        sender.origin !== new URL(sender.url).origin) return false;
    const payload = nativeRequest(request);
    if (!payload) return false;
    // A document that just lost its grant can still release its own Undo token.
    if (payload.releaseUndo) {
        sendNative(payload, reply);
        return true;
    }
    Promise.all([chrome.permissions.contains({origins: [pattern]}), registeredSite(pattern)])
        .then(([permission, registered]) => {
            if (!permission || !registered) reply({error: 'site-disabled'});
            else sendNative(payload, reply);
        }).catch(() => reply({error: 'site-unavailable'}));
    return true;
});
