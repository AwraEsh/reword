const site = document.getElementById('site');
const toggle = document.getElementById('toggle');
const status = document.getElementById('status');

async function setup() {
    const [tab] = await chrome.tabs.query({active: true, currentWindow: true});
    const pattern = sitePattern(tab?.url);
    if (!pattern || tab.id === undefined) {
        status.textContent = 'Open an HTTP or HTTPS page to enable Reword.';
        return;
    }
    site.textContent = pattern.slice(0, -2);
    let enabled = await registeredSite(pattern) && await chrome.permissions.contains({origins: [pattern]});
    const render = () => {
        toggle.textContent = enabled ? 'Disable on this site' : 'Enable on this site';
        status.textContent = enabled ? 'Reword also needs permission for this browser in Applications.' :
            'Reword is off on this site.';
    };
    render();
    toggle.disabled = false;
    toggle.addEventListener('click', async () => {
        toggle.disabled = true;
        try {
            if (enabled) {
                await unregisterSite(pattern);
                await chrome.permissions.remove({origins: [pattern]});
                enabled = false;
            } else {
                if (!await chrome.permissions.request({origins: [pattern]})) {
                    status.textContent = 'Site access was declined.';
                    return;
                }
                await registerSite(pattern);
                enabled = true;
                await chrome.scripting.executeScript({target: {tabId: tab.id}, files: rewordContentFiles});
            }
            render();
        } catch {
            render();
            status.textContent = 'Reload this page and try again.';
        } finally {
            toggle.disabled = false;
        }
    });
}

setup().catch(() => { status.textContent = 'Reword could not read this tab.'; });
