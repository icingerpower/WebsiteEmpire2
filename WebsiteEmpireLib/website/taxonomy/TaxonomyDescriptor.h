#ifndef TAXONOMYDESCRIPTOR_H
#define TAXONOMYDESCRIPTOR_H
#include <QString>
struct TaxonomyDescriptor {
    QString id;          // stable key, e.g. "symptoms" — used as DB table name
    QString displayName; // human-readable, e.g. "Symptoms"
};
#endif
