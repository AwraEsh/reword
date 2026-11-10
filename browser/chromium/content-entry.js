if (!globalThis.rewordContentStarted) {
    globalThis.rewordContentStarted = true;
    startRewordContent({send: (_field, request) => chrome.runtime.sendMessage(request).catch(() => null)});
}
