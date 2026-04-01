#ifndef ABSTRACTSHORTCODELINK_H
#define ABSTRACTSHORTCODELINK_H

#include "AbstractShortCode.h"

/**
 * Shared base for link shortcodes ([LINKFIX] and [LINKTR]).
 *
 * Both produce:  <a href="url" rel="rel">inner content</a>
 *
 * Shared behaviour:
 *   - rel argument is optional; when absent it defaults to DEFAULT_REL ("dofollow").
 *   - rel accepts any non-empty string (no allowedValues restriction).
 *   - isArgumentValueValid rejects empty url or rel.
 *   - addCode emits no CSS or JS.
 *
 * Subclasses differ only in the url argument declaration (mandatory flag and
 * translatability) and the tag name.  They must implement:
 *   - getTag()         — unique tag (e.g. "LINKFIX")
 *   - urlArgumentDef() — returns the ArgumentDef for "url"
 */
class AbstractShortCodeLink : public AbstractShortCode
{
public:
    static constexpr const char *ID_URL      = "url";
    static constexpr const char *ID_REL      = "rel";
    static constexpr const char *DEFAULT_REL = "dofollow";

    /** Returns {urlArgumentDef(), relDef}. */
    QList<ArgumentDef> availableArguments() const override;

    /**
     * Returns true when:
     *   - argId == ID_URL: value is non-empty after trimming
     *   - argId == ID_REL: value is non-empty after trimming
     *   - any other argId: true (unreachable after validate())
     */
    bool isArgumentValueValid(const QString &argId, const QString &value) const override;

    /**
     * Parses origContent, then appends
     *   <a href="url" rel="rel">innerContent</a>
     * to html.  When rel is absent, DEFAULT_REL ("dofollow") is used.
     * css, js, cssDoneIds and jsDoneIds are left unchanged.
     */
    void addCode(QStringView     origContent,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

protected:
    /** Returns the ArgumentDef for the "url" argument (subclass-specific). */
    virtual ArgumentDef urlArgumentDef() const = 0;
};

#endif // ABSTRACTSHORTCODELINK_H
