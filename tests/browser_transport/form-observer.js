const form = document.getElementById('observed-form');
form.addEventListener('beforeinput', event => {
    const field = event.target;
    if ((field.id === 'veto' && event.inputType === 'insertReplacementText') ||
        (field.dataset.pageMode === 'vetoUndo' && event.inputType === 'historyUndo')) {
        field.dataset.pageVeto = 'yes';
        event.preventDefault();
    }
    if (field.dataset.pageMode === 'changeBefore' && event.inputType === 'insertReplacementText') {
        field.setRangeText('target', field.value.length - 6, field.value.length, 'preserve');
    }
});
form.addEventListener('input', event => {
    const field = event.target;
    if (field.dataset.pageMode === 'changeAfter' && event.inputType === 'insertReplacementText') {
        field.setRangeText('target', field.value.length - 6, field.value.length, 'preserve');
    }
    field.dataset.pageValue = field.value;
    if (event.inputType === 'insertReplacementText' || event.inputType === 'historyUndo') {
        field.dataset.pageEvent = event.inputType;
        field.dataset.pageEvents = Number(field.dataset.pageEvents ?? 0) + 1;
    }
});
