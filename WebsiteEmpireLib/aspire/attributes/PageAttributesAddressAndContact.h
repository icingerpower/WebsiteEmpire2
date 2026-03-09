#ifndef PAGEATTRIBUTESADDRESSANDCONTACT_H
#define PAGEATTRIBUTESADDRESSANDCONTACT_H

#include "PageAttributesAddress.h"

class PageAttributesAddressAndContact : public PageAttributesAddress
{
    Q_OBJECT

public:
    static const QString ID_EMAIL;
    static const QString ID_PHONE;
    static const QString ID_CONTACT_NAME;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESADDRESSANDCONTACT_H
