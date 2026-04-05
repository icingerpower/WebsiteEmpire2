#include "PageEditorDialog.h"
#include "ui_PageEditorDialog.h"

#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/blocs/AbstractPageBloc.h"
#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"

#include <QLabel>
#include <QVBoxLayout>

// =============================================================================
// Constructor / Destructor
// =============================================================================

PageEditorDialog::PageEditorDialog(IPageRepository &repo,
                                   CategoryTable   &categoryTable,
                                   int              pageId,
                                   const QString   &editingLangCode,
                                   QWidget         *parent)
    : QDialog(parent)
    , ui(new Ui::PageEditorDialog)
    , m_repo(repo)
    , m_categoryTable(categoryTable)
    , m_pageId(pageId)
    , m_editingLangCode(editingLangCode)
{
    ui->setupUi(this);

    const bool isCreate = (pageId == -1);

    // Populate type combo.
    for (const QString &typeId : AbstractPageType::allTypeIds()) {
        auto tmp = AbstractPageType::createForTypeId(typeId, categoryTable);
        if (tmp) {
            ui->comboType->addItem(tmp->getDisplayName(), typeId);
        }
    }
    ui->comboType->setEnabled(isCreate);

    if (isCreate) {
        setWindowTitle(tr("New Page"));
        connect(ui->comboType, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &PageEditorDialog::_onTypeChanged);
        if (ui->comboType->count() > 0) {
            _onTypeChanged(0);
        }
    } else {
        setWindowTitle(tr("Edit Page"));
        const auto &rec = m_repo.findById(pageId);
        if (rec) {
            // Set combo to the stored type (disabled, read-only).
            const int idx = ui->comboType->findData(rec->typeId);
            if (idx >= 0) {
                ui->comboType->setCurrentIndex(idx);
            }
            ui->editPermalink->setText(rec->permalink);
            _loadBlocs(rec->typeId);

            // Populate widgets from stored data.
            const QHash<QString, QString> &data = m_repo.loadData(pageId);
            // Route each key to the correct bloc widget via the type's load.
            m_pageType->load(data);
            // Now sync each bloc's loaded state into its widget.
            const auto &blocs = m_pageType->getPageBlocs();
            for (int i = 0; i < blocs.size() && i < m_blocWidgets.size(); ++i) {
                QHash<QString, QString> sub;
                blocs.at(i)->save(sub);
                m_blocWidgets.at(i)->load(sub);
            }
        }
    }

    connect(ui->buttonBox, &QDialogButtonBox::accepted,
            this, &PageEditorDialog::_onAccepted);
}

PageEditorDialog::~PageEditorDialog()
{
    delete ui;
}

// =============================================================================
// Private helpers
// =============================================================================

void PageEditorDialog::_clearBlocs()
{
    // Remove all previously inserted bloc widgets from the scroll area layout.
    QLayout *lay = ui->scrollContents->layout();
    // Remove all items except the trailing spacer (last item).
    while (lay->count() > 1) {
        QLayoutItem *item = lay->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_blocWidgets.clear();
    m_pageType.reset();
}

void PageEditorDialog::_loadBlocs(const QString &typeId)
{
    _clearBlocs();
    m_pageType = AbstractPageType::createForTypeId(typeId, m_categoryTable);
    if (!m_pageType) {
        return;
    }

    QLayout *lay = ui->scrollContents->layout();
    const auto &blocs = m_pageType->getPageBlocs();
    for (int i = 0; i < blocs.size(); ++i) {
        // const_cast is safe: createEditWidget is the designated mutation path
        // and does not modify rendering state.
        AbstractPageBloc *bloc = const_cast<AbstractPageBloc *>(blocs.at(i));
        AbstractPageBlockWidget *w = bloc->createEditWidget();

        auto *container = new QWidget(ui->scrollContents);
        auto *containerLayout = new QVBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->addWidget(new QLabel(bloc->getName(), container));
        w->setParent(container);
        containerLayout->addWidget(w);

        // Insert before the trailing spacer.
        static_cast<QVBoxLayout *>(lay)->insertWidget(i, container);
        m_blocWidgets.append(w);
    }
}

// =============================================================================
// Slots
// =============================================================================

void PageEditorDialog::_onTypeChanged(int index)
{
    if (index < 0) {
        return;
    }
    const QVariant typeData = ui->comboType->itemData(index);
    const QString &typeId = typeData.toString();
    _loadBlocs(typeId);
}

void PageEditorDialog::_onAccepted()
{
    if (!m_pageType) {
        return;
    }

    const QString editPermalinkText = ui->editPermalink->text();
    const QString &permalink = editPermalinkText.trimmed();
    const QVariant currentTypeData = ui->comboType->currentData();
    const QString &typeId    = currentTypeData.toString();

    // Collect data from widgets into each bloc, then save via the type.
    const auto &blocs = m_pageType->getPageBlocs();
    for (int i = 0; i < blocs.size() && i < m_blocWidgets.size(); ++i) {
        QHash<QString, QString> sub;
        m_blocWidgets.at(i)->save(sub);
        const_cast<AbstractPageBloc *>(blocs.at(i))->load(sub);
    }

    QHash<QString, QString> data;
    m_pageType->save(data);

    if (m_pageId == -1) {
        // Create mode.
        const int newId = m_repo.create(typeId, permalink, m_editingLangCode);
        m_repo.saveData(newId, data);
    } else {
        // Edit mode — update permalink if changed, then save data.
        m_repo.updatePermalink(m_pageId, permalink);
        m_repo.saveData(m_pageId, data);
    }

    accept();
}
