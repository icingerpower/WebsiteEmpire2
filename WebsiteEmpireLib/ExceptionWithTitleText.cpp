#include "ExceptionWithTitleText.h"

ExceptionWithTitleText::ExceptionWithTitleText(const QString &title, const QString &text)
    : m_errorTitle(title)
    , m_errorText(text)
{
}

void ExceptionWithTitleText::raise() const
{
    throw *this;
}

ExceptionWithTitleText *ExceptionWithTitleText::clone() const
{
    return new ExceptionWithTitleText(*this);
}

const char *ExceptionWithTitleText::what() const noexcept
{
    m_whatMsg = (m_errorTitle + ": " + m_errorText).toUtf8();
    return m_whatMsg.constData();
}
