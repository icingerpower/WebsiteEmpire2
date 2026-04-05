#include "PageEditorWidget.h"
#include "ui_PageEditorWidget.h"

#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/blocs/AbstractPageBloc.h"
#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"

#include <QDateTime>
#include <QLabel>
#include <QVBoxLayout>

// =============================================================================
// Constructor / Destructor
// =============================================================================

PageEditorWidget::PageEditorWidget(IPageRepository &repo,
                                   CategoryTable   &categoryTable,
                                   int              pageId,
                                   const QString   &editingLangCode,
                                   QWidget         *parent)
    : QWidget(parent)
    , ui(new Ui::PageEditorWidget)
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
        connect(ui->comboType, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &PageEditorWidget::_onTypeChanged);
        if (ui->comboType->count() > 0) {
            _onTypeChanged(0);
        }
    } else {
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
            m_pageType->load(data);
            const auto &blocs = m_pageType->getPageBlocs();
            for (int i = 0; i < blocs.size() && i < m_blocWidgets.size(); ++i) {
                QHash<QString, QString> sub;
                blocs.at(i)->save(sub);
                m_blocWidgets.at(i)->load(sub);
            }
        }
    }

    connect(ui->btnSave, &QPushButton::clicked,
            this, &PageEditorWidget::_onSaveClicked);
}

PageEditorWidget::~PageEditorWidget()
{
    delete ui;
}

// =============================================================================
// Private helpers
// =============================================================================

void PageEditorWidget::_clearBlocs()
{
    // Deleting a child of QSplitter triggers its childEvent which removes the
    // widget from the splitter's internal list, so count() decreases on each
    // iteration.
    while (ui->splitter->count() > 0) {
        delete ui->splitter->widget(0);
    }
    m_blocWidgets.clear();
    m_pageType.reset();
}

void PageEditorWidget::_loadBlocs(const QString &typeId)
{
    _clearBlocs();
    m_pageType = AbstractPageType::createForTypeId(typeId, m_categoryTable);
    if (!m_pageType) {
        return;
    }

    const auto &blocs = m_pageType->getPageBlocs();
    for (int i = 0; i < blocs.size(); ++i) {
        // const_cast is safe: createEditWidget is the designated mutation path
        // and does not modify rendering state.
        AbstractPageBloc *bloc = const_cast<AbstractPageBloc *>(blocs.at(i));
        AbstractPageBlockWidget *w = bloc->createEditWidget();

        auto *container = new QWidget;
        auto *containerLayout = new QVBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->addWidget(new QLabel(bloc->getName(), container));
        w->setParent(container);
        containerLayout->addWidget(w);

        ui->splitter->addWidget(container);
        m_blocWidgets.append(w);
    }
}

// =============================================================================
// Slots
// =============================================================================

void PageEditorWidget::_onTypeChanged(int index)
{
    if (index < 0) {
        return;
    }
    const QVariant typeData = ui->comboType->itemData(index);
    const QString &typeId = typeData.toString();
    _loadBlocs(typeId);
}

void PageEditorWidget::_onSaveClicked()
{
    if (!m_pageType) {
        return;
    }

    const QString editPermalinkText = ui->editPermalink->text();
    const QString &permalink = editPermalinkText.trimmed();
    const QVariant currentTypeData = ui->comboType->currentData();
    const QString &typeId = currentTypeData.toString();

    // Collect data from widgets into each bloc, then serialise via the type.
    const auto &blocs = m_pageType->getPageBlocs();
    for (int i = 0; i < blocs.size() && i < m_blocWidgets.size(); ++i) {
        QHash<QString, QString> sub;
        m_blocWidgets.at(i)->save(sub);
        const_cast<AbstractPageBloc *>(blocs.at(i))->load(sub);
    }

    QHash<QString, QString> data;
    m_pageType->save(data);

    if (m_pageId == -1) {
        // Create mode: persist and transition into edit mode.
        m_pageId = m_repo.create(typeId, permalink, m_editingLangCode);
        m_repo.saveData(m_pageId, data);
        ui->comboType->setEnabled(false);
    } else {
        // Edit mode.
        m_repo.updatePermalink(m_pageId, permalink);
        m_repo.saveData(m_pageId, data);
    }

    ui->lblStatus->setText(tr("Saved at %1").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"))));
    emit saved(m_pageId);
}
