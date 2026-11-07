chrome.runtime.onMessage.addListener((request, sender, send) => {
    if (sender.id !== chrome.runtime.id || sender.origin !== PROBE_ORIGIN || sender.frameId !== 0 ||
        !['plain', 'multiline', 'stale', 'blur'].includes(request?.test)) return false;

    chrome.runtime.sendNativeMessage(PROBE_HOST, {test: request.test}, result => {
        if (chrome.runtime.lastError) send({error: 'native-host-unavailable'});
        else send(result ?? {error: 'missing-response'});
    });
    return true;
});
