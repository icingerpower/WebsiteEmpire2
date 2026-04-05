#include "PageBlocTextWidget.h"
#include "ui_PageBlocTextWidget.h"

#include "website/pages/blocs/PageBlocText.h"
#include "website/shortcodes/AbstractShortCode.h"

#include <QDialog>
#include <QPushButton>
#include <QTextCursor>
#include <algorithm>
#include <memory>

PageBlocTextWidget::PageBlocTextWidget(QWidget *parent)
    : AbstractPageBlockWidget(parent)
    , ui(new Ui::PageBlocTextWidget)
{
    ui->setupUi(this);

    QList<const AbstractShortCode *> shortcodes = AbstractShortCode::ALL_SHORTCODES().values();
    std::sort(shortcodes.begin(), shortcodes.end(),
              [](const AbstractShortCode *a, const AbstractShortCode *b) {
                  return a->getButtonName() < b->getButtonName();
              });

    for (const AbstractShortCode *sc : std::as_const(shortcodes)) {
        auto *btn = new QPushButton(sc->getButtonName(), this);
        btn->setToolTip(sc->getButtonToolTip());
        // Insert before the trailing spacer (always the last item).
        ui->shortcodeBar->insertWidget(ui->shortcodeBar->count() - 1, btn);

        connect(btn, &QPushButton::clicked, this, [this, sc]() {
            std::unique_ptr<QDialog> dlg(sc->createEditDialog(this));
            if (dlg->exec() != QDialog::Accepted) {
                return;
            }
            const QString begin    = sc->getTextBegin(dlg.get());
            const QString end      = sc->getTextEnd(dlg.get());
            QTextCursor   cursor   = ui->textEdit->textCursor();
            const QString selected = cursor.selectedText();
            cursor.insertText(begin + selected + end);
            if (selected.isEmpty()) {
                cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor,
                                    end.length());
                ui->textEdit->setTextCursor(cursor);
            }
            ui->textEdit->setFocus();
        });
    }
}

PageBlocTextWidget::~PageBlocTextWidget()
{
    delete ui;
}

void PageBlocTextWidget::load(const QHash<QString, QString> &values)
{
    ui->textEdit->setPlainText(values.value(QLatin1String(PageBlocText::KEY_TEXT)));
}

void PageBlocTextWidget::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(PageBlocText::KEY_TEXT), ui->textEdit->toPlainText());
}
