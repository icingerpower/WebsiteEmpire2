#ifndef PAGEBLOCKTEXT_H
#define PAGEBLOCKTEXT_H

#include "website/pages/AbstractPageBloc.h"

/**
 * A page bloc that holds plain text with optional inline shortcodes.
 *
 * addCode() splits origContent on blank lines into paragraphs, wraps each in
 * <p>…</p>, and expands every [TAG…][/TAG] shortcode encountered.
 *
 * When a shortcode's own HTML output contains further shortcode tags
 * (e.g. a SPINNABLE whose spun result is a [VIDEO][/VIDEO] tag), those
 * inner tags are expanded by a recursive call to processText(), so the
 * final HTML is always free of raw shortcode markers.
 *
 * Unknown tags (not registered in AbstractShortCode::ALL_SHORTCODES()) are
 * passed through verbatim rather than raising an error.
 */
class PageBlockText : public AbstractPageBloc
{
public:
    PageBlockText() = default;
    ~PageBlockText() override = default;

    void addCode(QStringView     origContent,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    /** Returns a new PageBlockTextWidget; ownership is transferred to the caller. */
    AbstractPageBlockWidget *createEditWidget() override;

private:
    /**
     * Scans text for [TAG…][/TAG] blocks and delegates each to the registered
     * AbstractShortCode handler.  The handler's HTML output is recursively
     * processed so that shortcodes emitted by a handler are also expanded.
     * Plain-text segments between shortcodes are appended verbatim.
     */
    static void processText(const QString  &text,
                            QString        &html,
                            QString        &css,
                            QString        &js,
                            QSet<QString>  &cssDoneIds,
                            QSet<QString>  &jsDoneIds);
};

#endif // PAGEBLOCKTEXT_H
