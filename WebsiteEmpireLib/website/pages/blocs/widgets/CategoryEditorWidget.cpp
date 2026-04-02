#include "CategoryEditorWidget.h"
#include "ui_CategoryEditorWidget.h"

#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/attributes/CategoryTreeModel.h"

CategoryEditorWidget::CategoryEditorWidget(CategoryTable &table,
                                           bool           allowSelection,
                                           QWidget       *parent)
    : AbstractPageBlockWidget(parent)
    , ui(new Ui::CategoryEditorWidget)
    , m_table(table)
    , m_model(new CategoryTreeModel(table, this))
    , m_allowSelection(allowSelection)
{
    ui->setupUi(this);

    m_model->setSelectionEnabled(allowSelection);
    ui->treeView->setModel(m_model);
    ui->treeView->setHeaderHidden(true);
    ui->treeView->expandAll();

    connect(ui->btnAdd,    &QPushButton::clicked, this, &CategoryEditorWidget::_onAddClicked);
    connect(ui->btnRemove, &QPushButton::clicked, this, &CategoryEditorWidget::_onRemoveClicked);
    connect(&m_table, &CategoryTable::categoryAdded,
            this, &CategoryEditorWidget::_onCategoryAdded);
}

CategoryEditorWidget::~CategoryEditorWidget()
{
    delete ui;
}

// ---- AbstractPageBlockWidget ------------------------------------------------

void CategoryEditorWidget::load(const QString &origContent)
{
    if (!m_allowSelection) {
        return;
    }
    QSet<int> ids;
    const QStringList parts = origContent.split(',', Qt::SkipEmptyParts);
    for (const QString &part : std::as_const(parts)) {
        const int id = part.trimmed().toInt();
        if (id > 0) {
            ids.insert(id);
        }
    }
    m_model->setCheckedIds(ids);
}

void CategoryEditorWidget::save(QString &contentToUpdate)
{
    if (!m_allowSelection) {
        return;
    }
    const QSet<int> ids = m_model->checkedIds();
    QStringList parts;
    parts.reserve(ids.size());
    for (int id : std::as_const(ids)) {
        parts.append(QString::number(id));
    }
    parts.sort(); // stable output regardless of QSet iteration order
    contentToUpdate = parts.join(',');
}

// ---- Private slots ----------------------------------------------------------

void CategoryEditorWidget::_onAddClicked()
{
    const QModelIndex current = ui->treeView->currentIndex();
    const int parentId = current.isValid() ? current.data(Qt::UserRole).toInt() : 0;
    m_table.addCategory(tr("New category"), parentId);
    // _onCategoryAdded() handles expanding the parent and entering edit mode.
}

void CategoryEditorWidget::_onRemoveClicked()
{
    const QModelIndex current = ui->treeView->currentIndex();
    if (!current.isValid()) {
        return;
    }
    const int id = current.data(Qt::UserRole).toInt();
    m_table.removeCategory(id);
}

void CategoryEditorWidget::_onCategoryAdded(int id)
{
    const QModelIndex idx = m_model->indexForId(id);
    if (!idx.isValid()) {
        return;
    }
    const QModelIndex parentIdx = idx.parent();
    if (parentIdx.isValid()) {
        ui->treeView->expand(parentIdx);
    }
    ui->treeView->setCurrentIndex(idx);
    ui->treeView->edit(idx);
}
