#ifndef PAGEBLOCTEXT_H
#define PAGEBLOCTEXT_H

#include "website/pages/blocs/AbstractPageBloc.h"

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
class PageBlocText : public AbstractPageBloc
{
public:
    static constexpr const char *KEY_TEXT = "text";

    PageBlocText() = default;
    ~PageBlocText() override = default;

    /**
     * Reads KEY_TEXT from values into m_text.
     * Unknown keys are silently ignored.
     */
    QString getName() const override;

    void load(const QHash<QString, QString> &values) override;

    /**
     * Writes m_text under KEY_TEXT into values.
     */
    void save(QHash<QString, QString> &values) const override;

    /** Renders m_text to html; origContent is ignored. */
    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    /** Returns a new PageBlocTextWidget; ownership is transferred to the caller. */
    AbstractPageBlockWidget *createEditWidget() override;

private:
    QString m_text;

    /**
     * Scans text for [TAG…][/TAG] blocks and delegates each to the registered
     * AbstractShortCode handler.  The handler's HTML output is recursively
     * processed so that shortcodes emitted by a handler are also expanded.
     * Plain-text segments between shortcodes are appended verbatim.
     */
    static void processText(const QString  &text,
                            AbstractEngine &engine,
                            int             websiteIndex,
                            QString        &html,
                            QString        &css,
                            QString        &js,
                            QSet<QString>  &cssDoneIds,
                            QSet<QString>  &jsDoneIds);
};

#endif // PAGEBLOCTEXT_H
