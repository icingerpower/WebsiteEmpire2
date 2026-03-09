#ifndef PAGEATTRIBUTESFACTORY_H
#define PAGEATTRIBUTESFACTORY_H

#include "PageAttributesAddressAndContact.h"

class PageAttributesFactory : public PageAttributesAddressAndContact
{
    Q_OBJECT

public:
    static const QString ID_CATEGORY;
    static const QString ID_DESCRIPTION;
    static const QString ID_INCOME;
    static const QString ID_EMPLOYEES;
    static const QString ID_MIN_ORDER_AMOUNT;
    static const QString ID_CERTIFICATIONS;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESFACTORY_H
