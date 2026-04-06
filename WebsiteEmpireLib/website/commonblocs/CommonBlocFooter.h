#ifndef COMMONBLOCFOOTER_H
#define COMMONBLOCFOOTER_H

#include "website/commonblocs/AbstractCommonBloc.h"

#include <QString>

/**
 * Common bloc that renders the site footer fragment.
 *
 * The footer displays a single line of text (typically a copyright notice).
 * It supports Global, PerTheme, and PerDomain scopes.
 *
 * addCode() generates the HTML and injects CSS using the active theme's
 * primaryColor() as the footer background.
 *
 * The repository sets content via setText() before calling addCode() or
 * createEditWidget().
 */
class CommonBlocFooter : public AbstractCommonBloc
{
public:
    static constexpr const char *ID       = "footer";
    static constexpr const char *KEY_TEXT = "text";

    CommonBlocFooter() = default;
    ~CommonBlocFooter() override = default;

    QString          getId()           const override;
    QString          getName()         const override;
    QList<ScopeType> supportedScopes() const override;

    /**
     * Emits a <footer class="site-footer"> containing a <p> with the text.
     * Injects .site-footer / .site-footer-text CSS once per page.
     * origContent is unused — data comes from m_text.
     */
    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    AbstractCommonBlocWidget *createEditWidget() override;

    void          setText(const QString &text);
    const QString &text() const;

private:
    QString m_text;
};

#endif // COMMONBLOCFOOTER_H
