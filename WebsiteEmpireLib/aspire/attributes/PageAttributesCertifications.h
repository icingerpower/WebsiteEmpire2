#ifndef PAGEATTRIBUTESCERTIFICATIONS_H
#define PAGEATTRIBUTESCERTIFICATIONS_H

#include "AbstractPageAttributes.h"

class PageAttributesCertifications : public AbstractPageAttributes
{
    Q_OBJECT

public:
    static const QString ID_CERTIFICATIONS;
    static const QString ID_DESCRIPTION;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESCERTIFICATIONS_H
