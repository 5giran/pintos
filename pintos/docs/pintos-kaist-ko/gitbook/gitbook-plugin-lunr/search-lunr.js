require([
    'gitbook',
    'jquery'
], function(gitbook, $) {
    function LocalKoreanSearchEngine() {
        this.documents = [];
        this.name = 'LocalKoreanSearchEngine';
    }

    LocalKoreanSearchEngine.prototype.init = function() {
        var that = this;
        var d = $.Deferred();
        var url = gitbook.state.basePath + '/search_index.json';

        function ready(data) {
            that.documents = data.documents || Object.keys(data.store || {}).map(function(k) { return data.store[k]; });
            d.resolve();
        }

        if (window.KO_SEARCH_INDEX) {
            ready(window.KO_SEARCH_INDEX);
        } else if (window.location.protocol === 'file:') {
            d.resolve();
        } else {
            $.getJSON(url).then(ready, function() { d.resolve(); });
        }
        return d.promise();
    };

    LocalKoreanSearchEngine.prototype.search = function(q, offset, length) {
        q = (q || '').toLowerCase().trim();
        var terms = q.split(/\s+/).filter(Boolean);
        var results = [];
        if (terms.length) {
            this.documents.forEach(function(doc) {
                var haystack = [doc.title, doc.body, doc.keywords].join(' ').toLowerCase();
                var score = 0;
                terms.forEach(function(term) {
                    if (haystack.indexOf(term) !== -1) score += (doc.title || '').toLowerCase().indexOf(term) !== -1 ? 5 : 1;
                });
                if (score) {
                    results.push({ title: doc.title, url: doc.url, body: doc.summary || doc.body, score: score });
                }
            });
            results.sort(function(a, b) { return b.score - a.score || a.title.localeCompare(b.title); });
        }
        return $.Deferred().resolve({ query: q, results: results.slice(offset || 0, (offset || 0) + (length || 15)), count: results.length }).promise();
    };

    gitbook.events.bind('start', function(e, config) {
        if (!gitbook.search.getEngine()) gitbook.search.setEngine(LocalKoreanSearchEngine, config);
    });
});
