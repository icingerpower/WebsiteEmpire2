#ifndef PAGEBLOCTEXT_H
#define PAGEBLOCTEXT_H

#include "website/commonblocs/BlocTranslations.h"
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

    /** Renders m_text (or its translation) to html; origContent is ignored. */
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

    // ---- Translation protocol ------------------------------------------------

    /** Emits {KEY_TEXT, m_text} when m_text is non-empty. */
    void collectTranslatables(QStringView              origContent,
                              QList<TranslatableField> &out) const override;

    /**
     * Stores a translated value for fieldId + lang in m_translations.
     * Changes are persisted on the next save() call.
     */
    void applyTranslation(QStringView   origContent,
                          const QString &fieldId,
                          const QString &lang,
                          const QString &text) override;

    /**
     * Returns true when m_translations.isComplete(lang) is true,
     * i.e. every non-empty source field has a translation for lang.
     */
    bool isTranslationComplete(QStringView   origContent,
                               const QString &lang) const override;

private:
    QString          m_text;
    BlocTranslations m_translations; ///< per-language store for m_text

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
