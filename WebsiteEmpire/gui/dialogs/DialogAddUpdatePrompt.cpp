#include "DialogAddUpdatePrompt.h"
#include "ui_DialogAddUpdatePrompt.h"

#include <QPushButton>

DialogAddUpdatePrompt::DialogAddUpdatePrompt(
    const QList<AbstractPageType::AiUpdateTarget> &targets,
    QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogAddUpdatePrompt)
    , m_targets(targets)
{
    ui->setupUi(this);

    for (const auto &t : std::as_const(m_targets)) {
        ui->comboTarget->addItem(t.displayName);
    }

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->lineEditName, &QLineEdit::textChanged, this, &DialogAddUpdatePrompt::_validate);
    connect(ui->comboTarget, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DialogAddUpdatePrompt::_onTargetChanged);

    _validate();
    _onTargetChanged(ui->comboTarget->currentIndex());
}

DialogAddUpdatePrompt::~DialogAddUpdatePrompt()
{
    delete ui;
}

QString DialogAddUpdatePrompt::name() const
{
    return ui->lineEditName->text().trimmed();
}

QString DialogAddUpdatePrompt::saveKey() const
{
    const int idx = ui->comboTarget->currentIndex();
    if (idx < 0 || idx >= m_targets.size()) {
        return {};
    }
    const auto &t = m_targets.at(idx);
    // ArticleFormat targets use the legacy saveKey="" convention
    if (t.validator == AbstractPageBloc::AiUpdateSpec::Validator::ArticleFormat) {
        return {};
    }
    return t.prefixedKey;
}

bool DialogAddUpdatePrompt::skipIfKeyNonEmpty() const
{
    return ui->checkSkipIfSet->isChecked();
}

void DialogAddUpdatePrompt::_onTargetChanged(int index)
{
    const bool isArticle = (index >= 0 && index < m_targets.size())
        && m_targets.at(index).validator == AbstractPageBloc::AiUpdateSpec::Validator::ArticleFormat;
    // For article text rewriting, "skip if set" doesn't apply.
    ui->checkSkipIfSet->setEnabled(!isArticle);
    if (isArticle) {
        ui->checkSkipIfSet->setChecked(false);
    }
}

void DialogAddUpdatePrompt::_validate()
{
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(
        !ui->lineEditName->text().trimmed().isEmpty());
}
