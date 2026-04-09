#include "PageBlocJs.h"
#include "website/pages/blocs/widgets/PageBlocJsWidget.h"

#include <QCoreApplication>

// =============================================================================
// getName
// =============================================================================

QString PageBlocJs::getName() const
{
    return QCoreApplication::translate("PageBlocJs", "JS App");
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocJs::load(const QHash<QString, QString> &values)
{
    // Unknown keys are silently ignored for forward compatibility.
    m_html = values.value(QLatin1String(KEY_HTML));
    m_css  = values.value(QLatin1String(KEY_CSS));
    m_js   = values.value(QLatin1String(KEY_JS));
}

void PageBlocJs::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(KEY_HTML), m_html);
    values.insert(QLatin1String(KEY_CSS),  m_css);
    values.insert(QLatin1String(KEY_JS),   m_js);
}

// =============================================================================
// addCode
// =============================================================================

void PageBlocJs::addCode(QStringView     /*origContent*/,
                          AbstractEngine & /*engine*/,
                          int              /*websiteIndex*/,
                          QString        &html,
                          QString        &css,
                          QString        &js,
                          QSet<QString>  & /*cssDoneIds*/,
                          QSet<QString>  & /*jsDoneIds*/) const
{
    html += m_html;
    css  += m_css;
    js   += m_js;
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocJs::createEditWidget()
{
    return new PageBlocJsWidget;
}
