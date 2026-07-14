/**
 * PradizeAnalytics — lightweight client-side event beacon (TICKET-012).
 *
 * Usage:
 *   // Set once per page (in your base template):
 *   window.PRADIZE_STORE_ID = {{ store.pk }};
 *
 *   // Track events anywhere:
 *   PradizeAnalytics.track('product_view', { product_id: 42 });
 *
 *   // Events are flushed automatically on pagehide; call flush() early if needed.
 *   PradizeAnalytics.flush();
 *
 * Session ID: stored in sessionStorage under 'pa_sid'; generated once per browser tab.
 * Beacon: uses navigator.sendBeacon (non-blocking, survives page unload) with a
 * synchronous fetch fallback for browsers that do not support sendBeacon.
 */
(function () {
    'use strict';

    var BEACON_URL = '/_analytics/beacon/';
    var SESSION_KEY = 'pa_sid';

    // Generate or restore a per-tab session ID.
    var _session = (function () {
        var s = sessionStorage.getItem(SESSION_KEY);
        if (!s) {
            s = Math.random().toString(36).slice(2) + Date.now().toString(36);
            sessionStorage.setItem(SESSION_KEY, s);
        }
        return s;
    }());

    var _queue = [];

    function _utmParam(name) {
        try {
            return new URLSearchParams(location.search).get(name) || '';
        } catch (_) {
            return '';
        }
    }

    window.PradizeAnalytics = {
        /**
         * Queue an event for the next flush.
         *
         * @param {string} eventType  One of the EventType values (e.g. 'page_view').
         * @param {Object} [props]    Optional event-specific properties.
         */
        track: function (eventType, props) {
            _queue.push({
                event_type: eventType,
                properties: props || {},
                utm_source: _utmParam('utm_source'),
                utm_medium: _utmParam('utm_medium'),
                utm_campaign: _utmParam('utm_campaign'),
                utm_term: _utmParam('utm_term'),
                utm_content: _utmParam('utm_content'),
                referrer: document.referrer,
                landing_page: location.pathname,
                customer_local_hour: new Date().getHours(),
                created_at: new Date().toISOString()
            });
        },

        /**
         * Send all queued events to the beacon endpoint.
         * The queue is drained before the request is sent (splice prevents loss
         * if track() is called concurrently).
         *
         * @param {number} [storeId]  Store PK. Falls back to window.PRADIZE_STORE_ID.
         */
        flush: function (storeId) {
            var sid = storeId || window.PRADIZE_STORE_ID;
            if (!sid || !_queue.length) {
                return;
            }
            var batch = _queue.splice(0);
            var payload = JSON.stringify({
                store_id: sid,
                session_id: _session,
                events: batch
            });
            var blob = new Blob([payload], { type: 'application/json' });
            if (typeof navigator.sendBeacon === 'function') {
                navigator.sendBeacon(BEACON_URL, blob);
            } else {
                fetch(BEACON_URL, { method: 'POST', body: payload, keepalive: true });
            }
        }
    };

    // Flush on page hide (covers navigation, tab close, and mobile app switching).
    window.addEventListener('pagehide', function () {
        window.PradizeAnalytics.flush();
    });
}());
