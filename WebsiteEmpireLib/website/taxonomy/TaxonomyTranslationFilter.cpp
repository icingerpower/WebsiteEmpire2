#include "TaxonomyTranslationFilter.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/AbstractPageBloc.h"
#include "website/taxonomy/TaxonomyDescriptor.h"

#include <QSet>

QStringList TaxonomyTranslationFilter::filterTranslatable(const AbstractEngine *engine,
                                                           const QStringList    &allTypes)
{
    if (!engine) {
        return {};
    }

    QSet<QString> translatableIds;
    const QList<const AbstractPageType *> &pageTypes = engine->getPageTypes();
    for (const AbstractPageType *pageType : pageTypes) {
        if (!pageType) {
            continue;
        }
        const QList<const AbstractPageBloc *> &blocs = pageType->getPageBlocs();
        for (const AbstractPageBloc *bloc : std::as_const(blocs)) {
            const QList<TaxonomyDescriptor> descriptors = bloc->taxonomies();
            for (const TaxonomyDescriptor &desc : descriptors) {
                if (desc.translatable) {
                    translatableIds.insert(desc.id);
                }
            }
        }
    }

    QStringList result;
    for (const QString &type : allTypes) {
        if (translatableIds.contains(type)) {
            result.append(type);
        }
    }
    return result;
}
