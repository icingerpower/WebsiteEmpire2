import re

from django.core.exceptions import ValidationError

YOUTUBE_RE = re.compile(
    r'^https?://(www\.)?(youtube\.com/watch\?v=|youtu\.be/)[\w-]+'
)
VIMEO_RE = re.compile(
    r'^https?://(www\.)?vimeo\.com/\d+'
)


def validate_video_url(value):
    """Only YouTube and Vimeo embed URLs are accepted (no file uploads — DECIDED AF-C2)."""
    if not value:
        return
    if not (YOUTUBE_RE.match(value) or VIMEO_RE.match(value)):
        raise ValidationError(
            "Only YouTube (youtube.com/watch?v=... or youtu.be/...) "
            "and Vimeo (vimeo.com/...) URLs are accepted."
        )
