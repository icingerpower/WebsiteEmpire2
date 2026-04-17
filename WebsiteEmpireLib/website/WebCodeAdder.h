#ifndef WEBCODEADDER_H
#define WEBCODEADDER_H

#include <QList>
#include <QSet>
#include <QString>
#include <QStringView>

class AbstractEngine;

/**
 * Interface for objects that contribute HTML, CSS and JS fragments during page generation.
 *
 * Implementations append their content to the three output strings.
 * origContent is a view into the raw source text (e.g. a shortcode block); no copy is made.
 *
 * engine and websiteIndex identify the website being generated.  Implementations
 * that need domain-specific information (e.g. lang code, domain name) query them
 * via the engine.  Implementations that do not need them should Q_UNUSED both.
 *
 * Translation protocol (three-method contract)
 * --------------------------------------------
 * 1. collectTranslatables() — enumerate every field that needs translation.
 *    Called once per source page by the translation job builder before sending
 *    content to the AI.  origContent mirrors what addCode() would receive.
 *    Default: no-op (components with no translatable text need not override).
 *
 * 2. applyTranslation() — store one translated field value returned by the AI.
 *    Called once per (fieldId, lang) pair after the AI job completes.
 *    Default: no-op.
 *
 * 3. isTranslationComplete() — predicate checked by the generator before
 *    calling addCode().  Returns true when every field emitted by
 *    collectTranslatables() has a stored translation for lang.
 *    Default: true (components with nothing to translate are always "complete").
 */
struct TranslatableField {
    QString id;          ///< stable key within this component
    QString sourceText;  ///< text in the author language
};

class WebCodeAdder
{
public:
    virtual ~WebCodeAdder() = default;

    /**
     * Append generated HTML, CSS and JS fragments to the output strings.
     *
     * cssDoneIds / jsDoneIds track which CSS / JS blocks have already been
     * emitted into the page so that identical blocks are not duplicated.
     * Implementations must check these sets before appending CSS/JS and
     * insert the block's id into the set when they write for the first time.
     */
    virtual void addCode(QStringView     origContent,
                         AbstractEngine &engine,
                         int             websiteIndex,
                         QString        &html,
                         QString        &css,
                         QString        &js,
                         QSet<QString>  &cssDoneIds,
                         QSet<QString>  &jsDoneIds) const = 0;

    /**
     * Enumerate all translatable fields in this component.
     * Appends to out; never clears it.
     * For stateless components (shortcodes) origContent supplies the source text.
     * For stateful components (blocs) origContent is ignored.
     */
    virtual void collectTranslatables(QStringView              origContent,
                                      QList<TranslatableField> &out) const
    {
        Q_UNUSED(origContent)
        Q_UNUSED(out)
    }

    /**
     * Store one AI-produced translation for fieldId + lang.
     * For stateless components origContent identifies the shortcode instance.
     */
    virtual void applyTranslation(QStringView   origContent,
                                  const QString &fieldId,
                                  const QString &lang,
                                  const QString &text)
    {
        Q_UNUSED(origContent)
        Q_UNUSED(fieldId)
        Q_UNUSED(lang)
        Q_UNUSED(text)
    }

    /**
     * Returns true when every field emitted by collectTranslatables() has a
     * stored translation for lang, or when this component has no translatable
     * content.  The generator calls this before addCode() and skips or throws
     * based on the result.
     */
    virtual bool isTranslationComplete(QStringView   origContent,
                                       const QString &lang) const
    {
        Q_UNUSED(origContent)
        Q_UNUSED(lang)
        return true;
    }
};

#endif // WEBCODEADDER_H
