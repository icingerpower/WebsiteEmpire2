#include "AbstractPageType.h"
#include "website/pages/blocs/AbstractPageBloc.h"

void AbstractPageType::addCode(QStringView origContent,
                                QString &html, QString &css, QString &js,
                                QSet<QString> &cssDoneIds, QSet<QString> &jsDoneIds) const
{
    for (const AbstractPageBloc *bloc : getPageBlocs()) {
        bloc->addCode(origContent, html, css, js, cssDoneIds, jsDoneIds);
    }
}

const QList<const AbstractAttribute *> &AbstractPageType::getAttributes() const
{
    if (!m_attributesCached) {
        for (const AbstractPageBloc *bloc : getPageBlocs()) {
            const auto &attrs = bloc->getAttributes();
            m_cachedAttributes += attrs;
        }
        m_attributesCached = true;
    }
    return m_cachedAttributes;
}
