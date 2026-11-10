if (location.origin === PROBE_ORIGIN) {
    startRewordContent({
        engine: PROBE_ENGINE,
        route: PROBE_ROUTE,
        send: (field, request) => chrome.runtime.sendMessage({test: field?.id ?? 'plain', ...request})
            .catch(() => null),
        report: (field, name, value) => { field.dataset[name] = value; },
    });
}
