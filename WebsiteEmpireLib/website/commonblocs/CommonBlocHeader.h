#ifndef COMMONBLOCHEADER_H
#define COMMONBLOCHEADER_H

#include "website/commonblocs/AbstractCommonBloc.h"

#include <QString>

/**
 * Common bloc that renders the site header fragment.
 *
 * The header displays a site title (required) and an optional subtitle.
 * It supports Global, PerTheme, and PerDomain scopes.
 *
 * addCode() generates the HTML and injects CSS using the active theme's
 * primaryColor(), secondaryColor(), and fontFamily() values.
 *
 * The repository sets content via setTitle() / setSubtitle() before calling
 * addCode() or createEditWidget().
 */
class CommonBlocHeader : public AbstractCommonBloc
{
public:
    static constexpr const char *ID          = "header";
    static constexpr const char *KEY_TITLE    = "title";
    static constexpr const char *KEY_SUBTITLE = "subtitle";

    CommonBlocHeader() = default;
    ~CommonBlocHeader() override = default;

    QString          getId()           const override;
    QString          getName()         const override;
    QList<ScopeType> supportedScopes() const override;

    /**
     * Emits a <header class="site-header"> block containing an <h1> for the
     * title and an optional <p class="site-subtitle"> for the subtitle.
     * Injects .site-header / .site-title / .site-subtitle CSS once per page
     * using the active theme's colours and font family.
     * origContent is unused — data comes from m_title / m_subtitle.
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

    void          setTitle(const QString &title);
    const QString &title() const;

    /** Subtitle is optional — empty means no subtitle element is rendered. */
    void          setSubtitle(const QString &subtitle);
    const QString &subtitle() const;

private:
    QString m_title;
    QString m_subtitle;
};

#endif // COMMONBLOCHEADER_H
