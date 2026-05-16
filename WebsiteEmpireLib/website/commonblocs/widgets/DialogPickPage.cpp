#include "DialogPickPage.h"
#include "ui_DialogPickPage.h"

#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/PageTypeCategory.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QHeaderView>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>

// ---------------------------------------------------------------------------
// Column indices shared across the file
// ---------------------------------------------------------------------------

enum PageCol { COL_ID = 0, COL_TYPE = 1, COL_PERMALINK = 2, COL_LANG = 3, N_COLS = 4 };

// ---------------------------------------------------------------------------
// PageFilterProxy — file-private, filters by type and permalink substring
// ---------------------------------------------------------------------------

class PageFilterProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setTypeFilter(const QString &type)
    {
        m_typeFilter = type;
        invalidateFilter();
    }

    void setPermalinkFilter(const QString &text)
    {
        m_permalinkFilter = text;
        invalidateFilter();
    }

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        if (left.column() == COL_ID) {
            return left.data().toInt() < right.data().toInt();
        }
        return QSortFilterProxyModel::lessThan(left, right);
    }

    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        const QAbstractItemModel *src = sourceModel();
        if (!m_typeFilter.isEmpty()) {
            const QString type =
                src->data(src->index(sourceRow, COL_TYPE, sourceParent)).toString();
            if (type != m_typeFilter) {
                return false;
            }
        }
        if (!m_permalinkFilter.isEmpty()) {
            const QString permalink =
                src->data(src->index(sourceRow, COL_PERMALINK, sourceParent)).toString();
            if (!permalink.contains(m_permalinkFilter, Qt::CaseInsensitive)) {
                return false;
            }
        }
        return true;
    }

private:
    QString m_typeFilter;
    QString m_permalinkFilter;
};

// ---------------------------------------------------------------------------
// DialogPickPage
// ---------------------------------------------------------------------------

DialogPickPage::DialogPickPage(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogPickPage)
    , m_existingModel(new QStandardItemModel(0, N_COLS, this))
    , m_generatedModel(new QStandardItemModel(0, N_COLS, this))
    , m_existingProxy(new PageFilterProxy(this))
    , m_generatedProxy(new PageFilterProxy(this))
{
    ui->setupUi(this);

    m_existingModel->setHorizontalHeaderLabels(
        {tr("ID"), tr("Type"), tr("Permalink"), tr("Lang")});
    m_generatedModel->setHorizontalHeaderLabels(
        {tr("ID"), tr("Type"), tr("Permalink"), tr("Lang")});

    m_existingProxy->setSourceModel(m_existingModel);
    m_generatedProxy->setSourceModel(m_generatedModel);

    auto setupTable = [](QTableView *table, PageFilterProxy *proxy) {
        table->setModel(proxy);
        table->horizontalHeader()->setSectionResizeMode(COL_ID,        QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(COL_TYPE,      QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(COL_PERMALINK, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(COL_LANG,      QHeaderView::ResizeToContents);
        proxy->sort(COL_ID, Qt::AscendingOrder);
    };
    setupTable(ui->tableExisting,  m_existingProxy);
    setupTable(ui->tableGenerated, m_generatedProxy);

    loadPages();
    populateTypeCombo(ui->comboTypeExisting,  m_existingModel);
    populateTypeCombo(ui->comboTypeGenerated, m_generatedModel);

    connect(ui->comboTypeExisting,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        m_existingProxy->setTypeFilter(
            index == 0 ? QString() : ui->comboTypeExisting->currentText());
    });
    connect(ui->lineEditPermalinkExisting, &QLineEdit::textChanged,
            this, [this](const QString &text) {
        m_existingProxy->setPermalinkFilter(text);
    });

    connect(ui->comboTypeGenerated,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        m_generatedProxy->setTypeFilter(
            index == 0 ? QString() : ui->comboTypeGenerated->currentText());
    });
    connect(ui->lineEditPermalinkGenerated, &QLineEdit::textChanged,
            this, [this](const QString &text) {
        m_generatedProxy->setPermalinkFilter(text);
    });

    connect(ui->tableExisting->selectionModel(),  &QItemSelectionModel::selectionChanged,
            this, &DialogPickPage::updateOkButton);
    connect(ui->tableGenerated->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &DialogPickPage::updateOkButton);
    connect(ui->toolBox, &QToolBox::currentChanged,
            this, &DialogPickPage::updateOkButton);

    connect(ui->tableExisting,  &QTableView::doubleClicked,
            this, &DialogPickPage::accept);
    connect(ui->tableGenerated, &QTableView::doubleClicked,
            this, &DialogPickPage::accept);

    updateOkButton();
}

DialogPickPage::~DialogPickPage()
{
    delete ui;
}

QString DialogPickPage::selectedPermalink() const
{
    return m_selectedPermalink;
}

void DialogPickPage::accept()
{
    const int currentPage = ui->toolBox->currentIndex();
    QTableView *table      = (currentPage == 0) ? ui->tableExisting  : ui->tableGenerated;
    PageFilterProxy *proxy = (currentPage == 0) ? m_existingProxy    : m_generatedProxy;

    const QModelIndexList selection = table->selectionModel()->selectedRows();
    if (!selection.isEmpty()) {
        const QModelIndex sourceIndex = proxy->mapToSource(selection.first());
        m_selectedPermalink = proxy->sourceModel()
            ->data(proxy->sourceModel()->index(sourceIndex.row(), COL_PERMALINK))
            .toString();
    }
    QDialog::accept();
}

void DialogPickPage::loadPages()
{
    const QString path = qApp->property("workingDirPath").toString();
    if (path.isEmpty()) {
        return;
    }
    const QDir workingDir(path);
    if (!workingDir.exists(QLatin1String(PageDb::FILENAME))) {
        return;
    }

    PageDb db(workingDir);
    PageRepositoryDb repo(db);

    const QList<PageRecord> all = repo.findAll();
    for (const auto &page : std::as_const(all)) {
        if (page.sourcePageId != 0) {
            continue;
        }
        QList<QStandardItem *> row;
        row << new QStandardItem(QString::number(page.id))
            << new QStandardItem(page.typeId)
            << new QStandardItem(page.permalink)
            << new QStandardItem(page.lang);

        if (page.typeId == QLatin1String(PageTypeCategory::TYPE_ID)) {
            m_generatedModel->appendRow(row);
        } else {
            m_existingModel->appendRow(row);
        }
    }
}

void DialogPickPage::populateTypeCombo(QComboBox *combo, QStandardItemModel *model)
{
    QStringList types;
    for (int row = 0; row < model->rowCount(); ++row) {
        const QString type = model->item(row, COL_TYPE)->text();
        if (!types.contains(type)) {
            types.append(type);
        }
    }
    types.sort();
    combo->addItem(tr("All"));
    for (const auto &type : std::as_const(types)) {
        combo->addItem(type);
    }
}

void DialogPickPage::updateOkButton()
{
    const int currentPage = ui->toolBox->currentIndex();
    QTableView *table = (currentPage == 0) ? ui->tableExisting : ui->tableGenerated;
    const bool hasSelection = !table->selectionModel()->selectedRows().isEmpty();
    if (QPushButton *ok = ui->buttonBox->button(QDialogButtonBox::Ok)) {
        ok->setEnabled(hasSelection);
    }
}
