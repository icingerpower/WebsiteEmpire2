#ifndef PAGEATTRIBUTESADDRESS_H
#define PAGEATTRIBUTESADDRESS_H

#include "AbstractPageAttributes.h"

class PageAttributesAddress : public AbstractPageAttributes
{
    Q_OBJECT

public:
    static const QString ID_COMPANY_NAME;
    static const QString ID_STREET;
    static const QString ID_POSTAL_CODE;
    static const QString ID_CITY;
    static const QString ID_STATE;
    static const QString ID_COUNTRY;

    QSharedPointer<QList<Attribute>> getAttributes() const override;
    QString getDescription() const override;
};

#endif // PAGEATTRIBUTESADDRESS_H
