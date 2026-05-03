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
// getAiKeyClues
// =============================================================================

QHash<QString, QString> PageBlocSocial::getAiKeyClues() const
{
    return {
        {QLatin1String(KEY_FB_TITLE),
         QCoreApplication::translate("PageBlocSocial", "Facebook title, max 60 chars")},
        {QLatin1String(KEY_FB_DESC),
         QCoreApplication::translate("PageBlocSocial", "Facebook description, max 160 chars")},
        {QLatin1String(KEY_TW_TITLE),
         QCoreApplication::translate("PageBlocSocial", "Twitter/X title, max 70 chars")},
        {QLatin1String(KEY_TW_DESC),
         QCoreApplication::translate("PageBlocSocial", "Twitter/X description, max 200 chars")},
        {QLatin1String(KEY_PT_TITLE),
         QCoreApplication::translate("PageBlocSocial", "Pinterest title, max 100 chars")},
        {QLatin1String(KEY_PT_DESC),
         QCoreApplication::translate("PageBlocSocial", "Pinterest description, max 500 chars")},
        {QLatin1String(KEY_LI_TITLE),
         QCoreApplication::translate("PageBlocSocial", "LinkedIn title, max 150 chars")},
        {QLatin1String(KEY_LI_DESC),
         QCoreApplication::translate("PageBlocSocial", "LinkedIn description, max 300 chars")},
    };
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
