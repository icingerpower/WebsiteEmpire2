"""
FirstTouchUTMMiddleware — captures UTM parameters, referrer, and landing page
on the first request of a session.

Stores the attribution dict in request.session['first_touch_utm'] only if not
already set (first-touch model: later requests do not overwrite the original).

Fields captured:
  utm_source, utm_medium, utm_campaign, utm_term, utm_content — from query params
  first_referrer — HTTP Referer header of the first request
  landing_page   — full URL of the first page visited

This middleware requires SessionMiddleware to have run first (session must be
accessible on request). Add it in MIDDLEWARE after SessionMiddleware.
"""


class FirstTouchUTMMiddleware:
    UTM_PARAMS = ['utm_source', 'utm_medium', 'utm_campaign', 'utm_term', 'utm_content']

    def __init__(self, get_response):
        self.get_response = get_response

    def __call__(self, request):
        if 'first_touch_utm' not in request.session:
            utm = {k: request.GET.get(k, '') for k in self.UTM_PARAMS}
            utm['first_referrer'] = request.META.get('HTTP_REFERER', '')
            utm['landing_page'] = request.build_absolute_uri()
            request.session['first_touch_utm'] = utm
        return self.get_response(request)
