#include "PaneDomains.h"
#include "ui_PaneDomains.h"

#include "../dialogs/DialogEditHosts.h"
#include "website/AbstractEngine.h"
#include "website/HostTable.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QMessageBox>
#include <QPushButton>
#include <QStyledItemDelegate>

// ---- HostComboDelegate ------------------------------------------------------
// Displays a QComboBox populated with available host names for COL_HOST.

class HostComboDelegate : public QStyledItemDelegate
{
public:
    explicit HostComboDelegate(AbstractEngine *engine, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_engine(engine) {}

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *combo = new QComboBox(parent);
        combo->addItem(QString()); // empty = no host
        const QStringList names = m_engine->availableHostNames();
        for (const auto &name : std::as_const(names)) {
            combo->addItem(name);
        }
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *combo = static_cast<QComboBox *>(editor);
        const QString current = index.data(Qt::DisplayRole).toString();
        const int idx = combo->findText(current);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    void setModelData(QWidget *editor,
                      QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        const auto *combo = static_cast<QComboBox *>(editor);
        model->setData(index, combo->currentText());
    }

private:
    AbstractEngine *m_engine;
};

// ---- PaneDomains ------------------------------------------------------------

PaneDomains::PaneDomains(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneDomains)
{
    ui->setupUi(this);
    _connectSlots();
}

PaneDomains::~PaneDomains()
{
    delete ui;
}

// ---- Public -----------------------------------------------------------------

void PaneDomains::setEngine(AbstractEngine *engine)
{
    m_engine = engine;
    ui->tableViewEngine->setModel(engine);
    ui->buttonApplyPerLang->setVisible(engine && engine->getVariations().size() > 1);
    if (engine) {
        ui->tableViewEngine->setItemDelegateForColumn(
            AbstractEngine::COL_HOST, new HostComboDelegate(engine, ui->tableViewEngine));
    }
}

void PaneDomains::setHostTable(HostTable *hostTable)
{
    m_hostTable = hostTable;
}

// ---- Slots ------------------------------------------------------------------

void PaneDomains::apply()
{
    if (!m_engine) {
        return;
    }
    const QModelIndex current = ui->tableViewEngine->currentIndex();
    if (!current.isValid()) {
        QMessageBox::warning(this, tr("No selection"), tr("Please select a cell first."));
        return;
    }
    const int col = current.column();
    const QString tmpl = ui->lineEdit->text();
    const int rows = m_engine->rowCount();
    for (int row = 0; row < rows; ++row) {
        const QString lang  = m_engine->data(m_engine->index(row, AbstractEngine::COL_LANG_CODE)).toString();
        const QString theme = m_engine->data(m_engine->index(row, AbstractEngine::COL_THEME)).toString();
        m_engine->setData(m_engine->index(row, col), _applyTemplate(tmpl, lang, theme));
    }
}

void PaneDomains::applyPerLang()
{
    if (!m_engine) {
        return;
    }
    const QModelIndex current = ui->tableViewEngine->currentIndex();
    if (!current.isValid()) {
        QMessageBox::warning(this, tr("No selection"), tr("Please select a cell first."));
        return;
    }
    const int col = current.column();
    const QString selectedLang = m_engine->data(
        m_engine->index(current.row(), AbstractEngine::COL_LANG_CODE)).toString();
    const QString tmpl = ui->lineEdit->text();
    const int rows = m_engine->rowCount();
    for (int row = 0; row < rows; ++row) {
        const QString lang = m_engine->data(m_engine->index(row, AbstractEngine::COL_LANG_CODE)).toString();
        if (lang != selectedLang) {
            continue;
        }
        const QString theme = m_engine->data(m_engine->index(row, AbstractEngine::COL_THEME)).toString();
        m_engine->setData(m_engine->index(row, col), _applyTemplate(tmpl, lang, theme));
    }
}

void PaneDomains::editHosts()
{
    if (!m_hostTable) {
        return;
    }
    DialogEditHosts dialog(m_hostTable, this);
    dialog.exec();
}

void PaneDomains::upload()
{
    // TODO later
}

void PaneDomains::download()
{
    // TODO later
}

void PaneDomains::viewCommands()
{
    // TODO later
}

// ---- Private ----------------------------------------------------------------

void PaneDomains::_connectSlots()
{
    connect(ui->buttonApply,        &QPushButton::clicked, this, &PaneDomains::apply);
    connect(ui->buttonApplyPerLang, &QPushButton::clicked, this, &PaneDomains::applyPerLang);
    connect(ui->buttonEdithosts,    &QPushButton::clicked, this, &PaneDomains::editHosts);
    connect(ui->buttonUpload,       &QPushButton::clicked, this, &PaneDomains::upload);
    connect(ui->buttonDownload,     &QPushButton::clicked, this, &PaneDomains::download);
    connect(ui->buttonViewCommands, &QPushButton::clicked, this, &PaneDomains::viewCommands);
}

QString PaneDomains::_applyTemplate(const QString &tmpl,
                                    const QString &lang,
                                    const QString &theme) const
{
    QString result = tmpl;
    result.replace(QStringLiteral("{LANG}"),  lang);
    result.replace(QStringLiteral("{THEME}"), theme);
    return result;
}
