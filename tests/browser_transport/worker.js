let nativePort;
let pendingReply;

function sendWord(word, reply) {
    if (pendingReply) {
        reply({error: 'native-host-busy'});
        return;
    }
    if (!nativePort) {
        const port = chrome.runtime.connectNative(PROBE_HOST);
        nativePort = port;
        port.onMessage.addListener(result => {
            if (nativePort === port) pendingReply?.(result);
        });
        port.onDisconnect.addListener(() => {
            void chrome.runtime.lastError;
            if (nativePort !== port) return;
            nativePort = undefined;
            pendingReply?.({error: 'native-host-unavailable'});
        });
    }
    const port = nativePort;
    const timeout = setTimeout(() => {
        nativePort = undefined;
        port.disconnect();
        pendingReply?.({error: 'native-host-timeout'});
    }, 5000);
    pendingReply = result => {
        clearTimeout(timeout);
        pendingReply = undefined;
        reply(result);
    };
    port.postMessage(word);
}

chrome.runtime.onMessage.addListener((request, sender, send) => {
    if (sender.id !== chrome.runtime.id || sender.origin !== PROBE_ORIGIN || sender.frameId !== 0 ||
        !['plain', 'multiline', 'stale', 'blur', 'policy', 'revoke'].includes(request?.test)) return false;

    if (PROBE_ENGINE) {
        if (request.policy === true) sendWord({policy: true}, send);
        else {
            sendWord(request.word, reply => {
                if (['stale', 'blur', 'revoke'].includes(request.test)) {
                    setTimeout(() => send(reply), request.test === 'revoke' ? 1000 : 650);
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
