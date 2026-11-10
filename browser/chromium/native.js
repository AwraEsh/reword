function createNativeChannel(host) {
    let nativePort;
    let pendingReply;
    let pendingRelease;
    let pendingIsRoute;
    let waitingWord;

    return function sendWord(word, reply) {
        if (pendingReply) {
            if (word.releaseUndo) {
                pendingRelease?.reply({});
                pendingRelease = {word, reply};
                return;
            }
            if (pendingIsRoute && !waitingWord && !word.claimUndo) {
                waitingWord = {word, reply};
                return;
            }
            reply({error: 'native-host-busy'});
            return;
        }
        if (!nativePort) {
            const port = chrome.runtime.connectNative(host);
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
        pendingIsRoute = Boolean(word.releaseUndo || word.claimUndo);
        pendingReply = result => {
            clearTimeout(timeout);
            pendingReply = undefined;
            reply(result);
            if (pendingRelease) {
                const release = pendingRelease;
                pendingRelease = undefined;
                sendWord(release.word, release.reply);
            } else if (waitingWord) {
                const waiting = waitingWord;
                waitingWord = undefined;
                sendWord(waiting.word, waiting.reply);
            }
        };
        port.postMessage(word);
    };
}
