#include "WidgetEditCommonBlocs.h"
#include "ui_WidgetEditCommonBlocs.h"

#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/commonblocs/AbstractCommonBlocWidget.h"
#include "website/theme/AbstractTheme.h"

WidgetEditCommonBlocs::WidgetEditCommonBlocs(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::WidgetEditCommonBlocs)
{
    ui->setupUi(this);
}

WidgetEditCommonBlocs::~WidgetEditCommonBlocs()
{
    delete ui;
}

void WidgetEditCommonBlocs::setBloc(AbstractCommonBloc *bloc, AbstractTheme *theme)
{
    if (m_editor) {
        ui->verticalLayout->removeWidget(m_editor);
        delete m_editor;
        m_editor = nullptr;
    }

    m_bloc  = bloc;
    m_theme = theme;

    if (!bloc) {
        return;
    }

    m_editor = bloc->createEditWidget();
    if (m_editor) {
        m_editor->setParent(this);
        ui->verticalLayout->addWidget(m_editor);
        m_editor->loadFrom(*bloc);
        connect(m_editor, &AbstractCommonBlocWidget::changed,
                this, &WidgetEditCommonBlocs::onAutoSave);
    }
}

void WidgetEditCommonBlocs::onAutoSave()
{
    if (m_editor && m_bloc) {
        m_editor->saveTo(*m_bloc);
    }
    if (m_theme) {
        m_theme->saveBlocsData();
    }
}
