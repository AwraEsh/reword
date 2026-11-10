importScripts('native.js');
const sendWord = createNativeChannel(PROBE_HOST);

chrome.runtime.onMessage.addListener((request, sender, send) => {
    if (sender.id !== chrome.runtime.id || sender.origin !== PROBE_ORIGIN || sender.frameId !== 0 ||
        !['plain', 'multiline', 'stale', 'blur', 'policy', 'revoke', 'pause-cycle', 'pending-caret',
          'retry', 'undo', 'veto']
            .includes(request?.test)) return false;

    if (PROBE_ENGINE) {
        if (request.policy === true) sendWord({policy: true}, send);
        else if (PROBE_ROUTE && request.route) sendWord(request.route, send);
        else {
            sendWord(request.word, reply => {
                if (['stale', 'blur', 'revoke', 'pause-cycle', 'pending-caret'].includes(request.test)) {
                    setTimeout(() => send(reply), 1000);
                } else {
                    send(reply);
                }
            });
        }
    } else {
        chrome.runtime.sendNativeMessage(PROBE_HOST, {test: request.test}, result => {
            const reply = chrome.runtime.lastError ? {error: 'native-host-unavailable'} :
                result ?? {error: 'missing-response'};
            send(reply);
        });
    }
    return true;
});
