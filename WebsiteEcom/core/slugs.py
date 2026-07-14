import unicodedata
import uuid

from django.utils.text import slugify


def make_slug(value: str) -> str:
    """
    NFD-normalize then slugify.  Never returns an empty string (AC-013 / SLUG-003).

    WHY NFD first: Django's slugify operates on the string as-is, so 'é' (NFC, 1 codepoint)
    would survive but 'é' (NFD, 2 codepoints — e + combining acute) would become 'e'.
    Normalizing to NFD first decomposes all composed characters before slugify strips the
    combining marks, producing consistent ASCII slugs regardless of the input normalization form.
    This matches design-pattern-ideas.txt §II: single normalization function, shared everywhere.

    UUID8 fallback: when the input is entirely non-ASCII (e.g. CJK characters, emoji) and
    slugify produces an empty string, fall back to the first 8 hex chars of a random UUID.
    This ensures every object always gets a usable slug instead of a silent empty string.
    """
    nfd = unicodedata.normalize("NFD", value)
    result = slugify(nfd)
    if not result:
        result = str(uuid.uuid4())[:8]
    return result
