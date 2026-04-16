#include "PageBlocSocial.h"
#include "website/pages/blocs/widgets/PageBlocSocialWidget.h"

#include <QCoreApplication>

// =============================================================================
// getName
// =============================================================================

QString PageBlocSocial::getName() const
{
    return QCoreApplication::translate("PageBlocSocial", "Social Media");
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocSocial::load(const QHash<QString, QString> &values)
{
    m_fbTitle = values.value(QLatin1String(KEY_FB_TITLE));
    m_fbDesc  = values.value(QLatin1String(KEY_FB_DESC));
    m_twTitle = values.value(QLatin1String(KEY_TW_TITLE));
    m_twDesc  = values.value(QLatin1String(KEY_TW_DESC));
    m_ptTitle = values.value(QLatin1String(KEY_PT_TITLE));
    m_ptDesc  = values.value(QLatin1String(KEY_PT_DESC));
    m_liTitle = values.value(QLatin1String(KEY_LI_TITLE));
    m_liDesc  = values.value(QLatin1String(KEY_LI_DESC));
}

void PageBlocSocial::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(KEY_FB_TITLE), m_fbTitle);
    values.insert(QLatin1String(KEY_FB_DESC),  m_fbDesc);
    values.insert(QLatin1String(KEY_TW_TITLE), m_twTitle);
    values.insert(QLatin1String(KEY_TW_DESC),  m_twDesc);
    values.insert(QLatin1String(KEY_PT_TITLE), m_ptTitle);
    values.insert(QLatin1String(KEY_PT_DESC),  m_ptDesc);
    values.insert(QLatin1String(KEY_LI_TITLE), m_liTitle);
    values.insert(QLatin1String(KEY_LI_DESC),  m_liDesc);
}

// =============================================================================
// addCode
// =============================================================================

void PageBlocSocial::addCode(QStringView      /*origContent*/,
                              AbstractEngine  & /*engine*/,
                              int              /*websiteIndex*/,
                              QString         & /*html*/,
                              QString         & /*css*/,
                              QString         & /*js*/,
                              QSet<QString>   & /*cssDoneIds*/,
                              QSet<QString>   & /*jsDoneIds*/) const
{
    // Social meta tags are injected into <head> by the page generator;
    // this bloc contributes no body HTML output.
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocSocial::createEditWidget()
{
    return new PageBlocSocialWidget;
}
