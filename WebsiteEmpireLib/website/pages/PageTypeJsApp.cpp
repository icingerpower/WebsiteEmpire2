#include "PageTypeJsApp.h"

#include "website/pages/blocs/PageBlocCategory.h"

PageTypeJsApp::PageTypeJsApp(CategoryTable &categoryTable)
    : m_categoryBloc(new PageBlocCategory(categoryTable))
{
    m_blocs.append(m_categoryBloc.data());
    m_blocs.append(&m_introBloc);
    m_blocs.append(&m_jsBloc);
    m_blocs.append(&m_outroBloc);
}

PageTypeJsApp::~PageTypeJsApp() = default;

QString PageTypeJsApp::getTypeId()      const { return QLatin1String(TYPE_ID); }
QString PageTypeJsApp::getDisplayName() const { return QLatin1String(DISPLAY_NAME); }

const QList<const AbstractPageBloc *> &PageTypeJsApp::getPageBlocs() const
{
    return m_blocs;
}

DECLARE_PAGE_TYPE(PageTypeJsApp)
