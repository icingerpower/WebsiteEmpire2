#ifndef THEMEDEFAULT_H
#define THEMEDEFAULT_H

#include "website/theme/AbstractTheme.h"
#include "website/commonblocs/CommonBlocHeader.h"
#include "website/commonblocs/CommonBlocFooter.h"
#include "website/commonblocs/CommonBlocMenuTop.h"
#include "website/commonblocs/CommonBlocMenuBottom.h"
#include "website/commonblocs/CommonBlocCookies.h"
#include "website/commonblocs/CommonBlocAiDisclaimer.h"
#include "website/commonblocs/CommonBlocSearch.h"

/**
 * Built-in default theme.
 *
 * Top blocs (rendered above the page body, in order):
 *   1. CommonBlocHeader
 *   2. CommonBlocSearch  — search bar, silent when action is empty
 *   3. CommonBlocMenuTop
 *
 * Bottom blocs (rendered below the page body, in order):
 *   1. CommonBlocMenuBottom
 *   2. CommonBlocFooter
 *   3. CommonBlocCookies  — fixed overlay, always last
 *
 * Article blocs (rendered inside <main> at the bottom of article pages only):
 *   1. CommonBlocAiDisclaimer — muted italic disclaimer, silent when text empty
 *
 * Configurable params: primary_color, secondary_color, font_family,
 * font_size_base.  Values are saved to {workingDir}/default_params.ini.
 *
 * The five common blocs are held as value members — their raw pointers
 * remain valid for the lifetime of this object.
 */
class ThemeDefault : public AbstractTheme
{
    Q_OBJECT
public:
    // Normal constructor — binds the theme to a working directory.
    explicit ThemeDefault(const QDir &workingDir, QObject *parent = nullptr);

    /** Prototype constructor — for DECLARE_THEME use only. No workingDir. */
    ThemeDefault();

    AbstractTheme *create(const QDir &workingDir,
                          QObject    *parent = nullptr) const override;

    QString getId()   const override; ///< Returns "default"
    QString getName() const override; ///< Returns tr("Default")

    QList<AbstractCommonBloc *> getTopBlocs()     override;
    QList<AbstractCommonBloc *> getBottomBlocs()  override;
    QList<AbstractCommonBloc *> getArticleBlocs() override;
    QList<Param>                getParams()       const override;

private:
    CommonBlocHeader        m_header;
    CommonBlocSearch        m_search;
    CommonBlocMenuTop       m_menuTop;
    CommonBlocMenuBottom    m_menuBottom;
    CommonBlocFooter        m_footer;
    CommonBlocCookies       m_cookies;
    CommonBlocAiDisclaimer  m_disclaimer;
};

#endif // THEMEDEFAULT_H
