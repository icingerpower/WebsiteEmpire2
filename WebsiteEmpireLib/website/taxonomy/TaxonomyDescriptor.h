#ifndef TAXONOMYDESCRIPTOR_H
#define TAXONOMYDESCRIPTOR_H
#include <QString>
struct TaxonomyDescriptor {
    QString id;                  // stable key, e.g. "symptoms" — used as DB table name
    QString displayName;         // human-readable, e.g. "Symptoms"
    bool    translatable = false; // opt-in: --translateCommon only translates
                                   // taxonomies with this set. Default false
                                   // preserves existing behavior for every
                                   // taxonomy declared before this field
                                   // existed (e.g. Symptoms stays untranslated).
};
#endif
