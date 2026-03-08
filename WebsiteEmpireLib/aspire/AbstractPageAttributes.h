#ifndef ABSTRACTPAGEATTRIBUTES_H
#define ABSTRACTPAGEATTRIBUTES_H

#include <QAbstractTableModel>
#include <QMap>

class AbstractPageAttributes : public QAbstractTableModel
{
    Q_OBJECT

public:
    using QAbstractTableModel::QAbstractTableModel;

    virtual QString getId() const = 0;

    static const QMap<QString, const AbstractPageAttributes *> &ALL_PAGE_ATTRIBUTES();

    class Recorder {
    public:
        Recorder(AbstractPageAttributes *pageAttributes);
    };

protected:
    static QMap<QString, const AbstractPageAttributes *> &getPageAttributes();
};

#define DECLARE_PAGE_ATTRIBUTES(NEW_CLASS) \
NEW_CLASS instance##NEW_CLASS; \
    AbstractPageAttributes::Recorder recorder##NEW_CLASS{&instance##NEW_CLASS};

#endif // ABSTRACTPAGEATTRIBUTES_H
