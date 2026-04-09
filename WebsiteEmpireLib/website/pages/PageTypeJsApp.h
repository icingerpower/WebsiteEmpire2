#ifndef PAGETYPEJSAPP_H
#define PAGETYPEJSAPP_H

#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocText.h"
#include "website/pages/blocs/PageBlocJs.h"

#include <QScopedPointer>

class CategoryTable;
class PageBlocCategory;

/**
 * A page type for self-contained interactive JavaScript applications.
 *
 * Blocs (in order):
 *   1. PageBlocCategory — category tags for the app
 *   2. PageBlocText     — introductory prose shown above the app
 *   3. PageBlocJs       — raw HTML/CSS/JS for the interactive component
 *   4. PageBlocText     — closing prose / instructions shown below the app
 *
 * Registered in the AbstractPageType registry under TYPE_ID = "js_app".
 *
 * PageBlocCategory is a QObject and is heap-allocated; the QScopedPointer
 * owns it.  The destructor is declared here and defined in the .cpp so that
 * QScopedPointer can see the full PageBlocCategory type at deletion time
 * without pulling it into this header.
 */
class PageTypeJsApp : public AbstractPageType
{
public:
    static constexpr const char *TYPE_ID      = "js_app";
    static constexpr const char *DISPLAY_NAME = "JS App";

    explicit PageTypeJsApp(CategoryTable &categoryTable);
    ~PageTypeJsApp() override;

    QString getTypeId()      const override;
    QString getDisplayName() const override;

    const QList<const AbstractPageBloc *> &getPageBlocs() const override;

private:
    QScopedPointer<PageBlocCategory> m_categoryBloc;
    PageBlocText                     m_introBloc;
    PageBlocJs                       m_jsBloc;
    PageBlocText                     m_outroBloc;
    QList<const AbstractPageBloc *>  m_blocs;
};

#endif // PAGETYPEJSAPP_H
