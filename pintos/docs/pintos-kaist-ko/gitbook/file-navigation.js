(function () {
    if (window.location.protocol !== 'file:') return;

    function isPlainLeftClick(event) {
        return event.button === 0 && !event.metaKey && !event.altKey && !event.ctrlKey && !event.shiftKey;
    }

    function isLocalHref(anchor) {
        var raw = anchor.getAttribute('href');
        if (!raw || raw.charAt(0) === '#') return false;
        if (anchor.getAttribute('target')) return false;
        return !/^(?:[a-z][a-z0-9+.-]*:)?\/\//i.test(raw) &&
            !/^(?:mailto|javascript):/i.test(raw);
    }

    document.addEventListener('click', function (event) {
        var anchor = event.target.closest && event.target.closest('a[href]');
        if (!anchor || !isPlainLeftClick(event) || !isLocalHref(anchor)) return;

        event.preventDefault();
        event.stopPropagation();
        event.stopImmediatePropagation();
        window.location.href = anchor.href;
    }, true);
}());
