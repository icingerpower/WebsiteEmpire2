"""
Streaming RSS 2.0 + g: namespace XML renderer, shared by both providers
(ADR-026 D1/D4 "One RSS/g:-namespace renderer for both providers").

Meta's ingestion of Google-namespace RSS/XML is long-standing and documented,
so this is the ONLY XML-writing code path for feeds — per-provider
differences are expressed entirely as FeedProvider.field_map / item_attributes()
data (feeds/registry.py, feeds/providers.py), never as a second renderer.

Escaping (AC-210): every text value is escaped via xml.sax.saxutils.escape
before being written — `&`, `<`, `>` in a title/description must never break
the surrounding XML.

Streaming: writes directly to the caller-supplied file object rather than
building the whole document in memory first, bounding memory usage on large
catalogs (ADR-026 Risks — "memory is bounded by .iterator() + streaming
writes"). The caller (feeds/tasks.py) owns the temp-file + os.replace atomic
write dance; this function only ever appends to an already-open file object.
"""

from __future__ import annotations

from xml.sax.saxutils import escape

from permalinks.resolver import base_url


def _write_element(fh, tag: str, value) -> None:
    """Write one <tag>escaped-value</tag> element, or one per list entry."""
    if isinstance(value, list):
        for entry in value:
            fh.write(f"<{tag}>{escape(str(entry))}</{tag}>\n")
    else:
        fh.write(f"<{tag}>{escape(str(value))}</{tag}>\n")


def render_feed(provider, store, store_language, items, fh) -> None:
    """
    Stream one RSS 2.0 + g: namespace feed to file object `fh` (text mode,
    UTF-8).

    Args:
        provider:       a registered FeedProvider instance (feeds/registry.py)
                        — item_attributes(item) supplies this provider's
                        (tag, value) pairs per item, in field-map order.
        store:          the Store this feed belongs to (channel title only).
        store_language: the StoreLanguage this feed was generated for
                        (channel <link>, via the single base_url() resolver).
        items:          iterable of shared item dicts (feeds/items.py
                        build_feed_items().items).
        fh:             a writable text file object opened by the caller —
                        this function never opens or closes it.
    """
    channel_link = base_url(store_language)
    channel_title = escape(f"{store.name} — {provider.name}")

    fh.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    fh.write('<rss version="2.0" xmlns:g="http://base.google.com/ns/1.0">\n')
    fh.write("<channel>\n")
    fh.write(f"<title>{channel_title}</title>\n")
    fh.write(f"<link>{escape(channel_link)}</link>\n")
    fh.write(f"<description>{escape(store.name)} product catalog feed.</description>\n")

    for item in items:
        fh.write("<item>\n")
        for tag, value in provider.item_attributes(item):
            _write_element(fh, tag, value)
        fh.write("</item>\n")

    fh.write("</channel>\n")
    fh.write("</rss>\n")
